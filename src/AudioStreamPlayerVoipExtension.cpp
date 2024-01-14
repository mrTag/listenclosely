#include "AudioStreamPlayerVoipExtension.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream_generator.hpp"
#include "godot_cpp/classes/audio_stream_microphone.hpp"
#include "godot_cpp/classes/audio_stream_player.hpp"
#include "godot_cpp/classes/audio_stream_player2d.hpp"
#include "godot_cpp/classes/audio_stream_player3d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/multiplayer_api.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

#include "opus.h"

void AudioStreamPlayerVoipExtension::_bind_methods()
{
}

void AudioStreamPlayerVoipExtension::_process( double delta )
{
}

void AudioStreamPlayerVoipExtension::_ready()
{
    // TODO: add mix_rate and buffer length as a property, so that it can be set in the editor.
    // NOTE: opus has pretty strict requirements on the mix rate: 48000 or 24000 or 16000 or 12000
    // or 8000
    int mix_rate = 24000;
    float buffer_length = 0.1f;

    auto *audioserver = godot::AudioServer::get_singleton();
    if ( godot::Engine::get_singleton()->is_editor_hint() )
    {
        // we're in the editor. so this will be executed the moment the
        // AudioStreamPlayerVoipExtension gets used in any way.
        // make sure that we have a bus named "MicCapture".
        int capture_bus_index = audioserver->get_bus_index( "MicCapture" );
        if ( capture_bus_index == -1 )
        {
            audioserver->add_bus();
            capture_bus_index = audioserver->get_bus_count() - 1;
            audioserver->set_bus_name( capture_bus_index, "MicCapture" );
            audioserver->set_bus_mute( capture_bus_index, true );
            godot::Ref<godot::AudioEffectCapture> audioEffectCapture;
            audioEffectCapture.instantiate();
            audioEffectCapture->set_name( "Capture" );
            audioEffectCapture->set_buffer_length( buffer_length );
            audioserver->add_bus_effect( capture_bus_index, audioEffectCapture );
        }
        else
        {
            if ( audioserver->get_bus_effect_count( capture_bus_index ) != 1 ||
                 !audioserver->get_bus_effect( capture_bus_index, 0 )
                      ->is_class( "AudioEffectCapture" ) )
            {
                // our MicCapture bus should have exactly one "AudioEffectCapture" Effect
                godot::UtilityFunctions::printerr(
                    "AudioStreamPlayerVoipExtension detected invalid MicCapture audio bus "
                    "configuration. Attempting to fix it..." );
                for ( int i = audioserver->get_bus_effect_count( capture_bus_index ) - 1; i >= 0;
                      --i )
                {
                    audioserver->remove_bus_effect( capture_bus_index, i );
                }
                godot::Ref<godot::AudioEffectCapture> audioEffectCapture;
                audioEffectCapture.instantiate();
                audioEffectCapture->set_name( "Capture" );
                audioEffectCapture->set_buffer_length( buffer_length );
                audioserver->add_bus_effect( capture_bus_index, audioEffectCapture );
            }
        }
        return;
    }

    // AudioStreamPlayer, AudioStreamPlayer2D and AudioStreamPlayer3D don't have a common ancestor
    // that we can use here. So we have to handle all 3 seperately if we want to support all 3.
    godot::AudioStreamPlayer *parentStreamPlayer =
        cast_to<godot::AudioStreamPlayer>( get_parent() );
    godot::AudioStreamPlayer2D *parentStreamPlayer2D =
        cast_to<godot::AudioStreamPlayer2D>( get_parent() );
    godot::AudioStreamPlayer3D *parentStreamPlayer3D =
        cast_to<godot::AudioStreamPlayer3D>( get_parent() );
    if ( parentStreamPlayer == nullptr && parentStreamPlayer2D == nullptr &&
         parentStreamPlayer3D == nullptr )
    {
        godot::UtilityFunctions::printerr( "AudioStreamPlayerVoipExtension needs to have an "
                                           "AudioStreamPlayer/2D/3D as a parent to work." );
        return;
    }

    if ( is_multiplayer_authority() )
    {
        // we are the authority, that means: we have to record our microphone,
        // encode the resulting stream and send it to the others
        int opus_error = 0;
        _opus_encoder = opus_encoder_create( mix_rate, 1, OPUS_APPLICATION_VOIP, &opus_error );
        if ( opus_error != OPUS_OK )
        {
            godot::UtilityFunctions::printerr(
                "AudioStreamPlayerVoipExtension could not create Opus Encoder. Error: ",
                opus_error );
            return;
        }
        if ( parentStreamPlayer != nullptr )
        {
            parentStreamPlayer->set_stream( new godot::AudioStreamMicrophone() );
        }
        if ( parentStreamPlayer2D != nullptr )
        {
            parentStreamPlayer2D->set_stream( new godot::AudioStreamMicrophone() );
        }
        if ( parentStreamPlayer3D != nullptr )
        {
            parentStreamPlayer3D->set_stream( new godot::AudioStreamMicrophone() );
        }

        if ( parentStreamPlayer != nullptr )
        {
            parentStreamPlayer->set_bus( "MicCapture" );
        }
        if ( parentStreamPlayer2D != nullptr )
        {
            parentStreamPlayer2D->set_bus( "MicCapture" );
        }
        if ( parentStreamPlayer3D != nullptr )
        {
            parentStreamPlayer3D->set_bus( "MicCapture" );
        }

        int capture_bus_index = audioserver->get_bus_index( "MicCapture" );
        _audioEffectCapture = audioserver->get_bus_effect( capture_bus_index, 0 );

        godot::UtilityFunctions::print(
            "AudioStreamPlayerVoipExtension initialized as MicCapture successfully." );
    }
    else
    {
        // we are not the auhthority, that means: the audio stream will come to
        // us via the network from someone else, we have to decode it and put it
        // into an AudioStreamGenerator.
        int opus_error = 0;
        _opus_decoder = opus_decoder_create( mix_rate, 1, &opus_error );
        if ( opus_error != OPUS_OK )
        {
            godot::UtilityFunctions::printerr(
                "AudioStreamPlayerVoipExtension could not create Opus Decoder. Error: ",
                opus_error );
            return;
        }
        godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
        audio_stream_generator.instantiate();
        audio_stream_generator->set_buffer_length( buffer_length );
        audio_stream_generator->set_mix_rate( mix_rate );
        if ( parentStreamPlayer != nullptr )
        {
            parentStreamPlayer->set_stream( audio_stream_generator );
            _audioStreamGeneratorPlayback = parentStreamPlayer->get_stream_playback();
        }
        if ( parentStreamPlayer2D != nullptr )
        {
            parentStreamPlayer2D->set_stream( audio_stream_generator );
            _audioStreamGeneratorPlayback = parentStreamPlayer2D->get_stream_playback();
        }
        if ( parentStreamPlayer3D != nullptr )
        {
            parentStreamPlayer3D->set_stream( audio_stream_generator );
            _audioStreamGeneratorPlayback = parentStreamPlayer3D->get_stream_playback();
        }
    }
}
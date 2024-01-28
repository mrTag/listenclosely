#include "AudioStreamPlayerVoipExtension.h"

#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream_generator.hpp"
#include "godot_cpp/classes/audio_stream_microphone.hpp"
#include "godot_cpp/classes/audio_stream_player.hpp"
#include "godot_cpp/classes/audio_stream_player2d.hpp"
#include "godot_cpp/classes/audio_stream_player3d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/multiplayer_api.hpp"
#include "godot_cpp/classes/multiplayer_peer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

#include "opus.h"

void AudioStreamPlayerVoipExtension::_bind_methods()
{
    godot::ClassDB::bind_method( godot::D_METHOD( "transferOpusPacketRPC", "packet" ),
                                 &AudioStreamPlayerVoipExtension::transferOpusPacketRPC );
    godot::ClassDB::bind_method( godot::D_METHOD( "initialize" ),
                                 &AudioStreamPlayerVoipExtension::initialize );

    godot::ClassDB::bind_method( godot::D_METHOD( "get_mix_rate" ),
                                 &AudioStreamPlayerVoipExtension::get_mix_rate );
    godot::ClassDB::bind_method( godot::D_METHOD( "set_mix_rate", "mix_rate" ),
                                 &AudioStreamPlayerVoipExtension::set_mix_rate );
    ADD_PROPERTY( godot::PropertyInfo( godot::Variant::INT, "mix_rate" ), "set_mix_rate",
                  "get_mix_rate" );

    godot::ClassDB::bind_method( godot::D_METHOD( "get_buffer_length" ),
                                 &AudioStreamPlayerVoipExtension::get_buffer_length );
    godot::ClassDB::bind_method( godot::D_METHOD( "set_buffer_length", "buffer_length" ),
                                 &AudioStreamPlayerVoipExtension::set_buffer_length );
    ADD_PROPERTY( godot::PropertyInfo( godot::Variant::FLOAT, "buffer_length" ),
                  "set_buffer_length", "get_buffer_length" );

    godot::ClassDB::bind_method( godot::D_METHOD( "get_current_loudness_db" ),
                                 &AudioStreamPlayerVoipExtension::get_current_loudness );
    ADD_PROPERTY( godot::PropertyInfo( godot::Variant::FLOAT, "current_loudness_db" ), "",
                  "get_current_loudness" );
}

void AudioStreamPlayerVoipExtension::_ready()
{
    godot::Dictionary conf;
    conf["rpc_mode"] = godot::MultiplayerAPI::RPCMode::RPC_MODE_AUTHORITY;
    conf["transfer_mode"] = godot::MultiplayerPeer::TransferMode::TRANSFER_MODE_UNRELIABLE;
    conf["call_local"] = false;
    conf["channel"] = 9;
    rpc_config( "transferOpusPacketRPC", conf );
}

void AudioStreamPlayerVoipExtension::_process( double delta )
{
    // just in case we don't receive or send any samples: reduce the current_loudness
    _current_loudness -= (float)delta * _current_loudness / 2.0f;
    if ( _audioEffectCapture.is_valid() &&
         _audioEffectCapture->can_get_buffer( audio_package_duration_ms * mix_rate / 1000 ) )
    {
        godot::PackedVector2Array stereoSampleBuffer =
            _audioEffectCapture->get_buffer( audio_package_duration_ms * mix_rate / 1000 );
        _sampleBuffer.resize( stereoSampleBuffer.size() );
        _current_loudness = 0;
        for ( int i = 0; i < stereoSampleBuffer.size(); ++i )
        {
            _sampleBuffer[i] = stereoSampleBuffer[i].x;
            _current_loudness += _sampleBuffer[i] * _sampleBuffer[i];
        }
        _current_loudness = godot::Math::sqrt( _current_loudness / stereoSampleBuffer.size() );
        // we do the resizing here, so that we only use up ram, when we are actually using the
        // encoder.
        _encodeBuffer.resize( 150000 );
        int sizeOfEncodedPackage = opus_encode_float(
            _opus_encoder, _sampleBuffer.ptr(), static_cast<int>( _sampleBuffer.size() ),
            _encodeBuffer.ptrw(), static_cast<int>( _encodeBuffer.size() ) );
        if ( sizeOfEncodedPackage <= 0 )
        {
            godot::UtilityFunctions::printerr(
                "AudioStreamPlayerVoipExtension could not encode captured audio. Opus errorcode: ",
                sizeOfEncodedPackage );
            return;
        }
        _encodeBuffer.resize( sizeOfEncodedPackage );
        _runningPacketNumber += 1;
        rpc( "transferOpusPacketRPC", _runningPacketNumber, _encodeBuffer );
    }
}

void AudioStreamPlayerVoipExtension::_enter_tree()
{
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
    }
}

void AudioStreamPlayerVoipExtension::initialize()
{
    auto *audioserver = godot::AudioServer::get_singleton();

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
            parentStreamPlayer->set_bus( "MicCapture" );
            parentStreamPlayer->play();
        }
        if ( parentStreamPlayer2D != nullptr )
        {
            parentStreamPlayer2D->set_stream( new godot::AudioStreamMicrophone() );
            parentStreamPlayer2D->set_bus( "MicCapture" );
            parentStreamPlayer2D->play();
        }
        if ( parentStreamPlayer3D != nullptr )
        {
            parentStreamPlayer3D->set_stream( new godot::AudioStreamMicrophone() );
            parentStreamPlayer3D->set_bus( "MicCapture" );
            parentStreamPlayer3D->play();
        }

        int capture_bus_index = audioserver->get_bus_index( "MicCapture" );
        _audioEffectCapture = audioserver->get_bus_effect( capture_bus_index, 0 );
        _audioEffectCapture->set_buffer_length( buffer_length );

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
                "AudioStreamPlayerVoipExtension could not create Opus Decoder. Opus errorcode: ",
                opus_error );
            return;
        }
        godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
        audio_stream_generator.instantiate();
        audio_stream_generator->set_buffer_length( buffer_length );
        audio_stream_generator->set_mix_rate( audioserver->get_mix_rate() );
        if ( parentStreamPlayer != nullptr )
        {
            parentStreamPlayer->set_stream( audio_stream_generator );
            parentStreamPlayer->play();
            _audioStreamGeneratorPlayback = parentStreamPlayer->get_stream_playback();
        }
        if ( parentStreamPlayer2D != nullptr )
        {
            parentStreamPlayer2D->set_stream( audio_stream_generator );
            parentStreamPlayer2D->play();
            _audioStreamGeneratorPlayback = parentStreamPlayer2D->get_stream_playback();
        }
        if ( parentStreamPlayer3D != nullptr )
        {
            parentStreamPlayer3D->set_stream( audio_stream_generator );
            parentStreamPlayer3D->play();
            _audioStreamGeneratorPlayback = parentStreamPlayer3D->get_stream_playback();
        }

        godot::UtilityFunctions::print(
            "AudioStreamPlayerVoipExtension initialized as AudioStreamGenerator successfully." );
    }
}

void AudioStreamPlayerVoipExtension::_exit_tree()
{
    godot::UtilityFunctions::print(
        "AudioStreamPlayerVoipExtension exit_tree: cleaning up opus and audiostream" );
    if ( _opus_decoder != nullptr )
    {
        opus_decoder_destroy( _opus_decoder );
    }
    if ( _opus_encoder != nullptr )
    {
        opus_encoder_destroy( _opus_encoder );
    }
    _opus_decoder = nullptr;
    _opus_encoder = nullptr;
    _audioEffectCapture = godot::Variant();
    _audioStreamGeneratorPlayback = godot::Variant();
}

void AudioStreamPlayerVoipExtension::transferOpusPacketRPC( unsigned char packetNumber,
                                                            godot::PackedByteArray packet )
{
    // this function was called by the rpc system because a packet was sent to us
    // make sure that this instance is prepared to receive the packet!
    if ( !_audioStreamGeneratorPlayback.is_valid() || _opus_decoder == nullptr )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension received Opus Packet, but is not ready for it! "
            "(_audioStreamGeneratorPlayback or _opus_decoder is null)" );
        return;
    }
    if ( packetNumber < _runningPacketNumber )
    {
        // byte overflow. just ignore any order discrepancy for this one...
        _runningPacketNumber = packetNumber;
    }
    else if ( packetNumber != _runningPacketNumber + 1 )
    {
        godot::UtilityFunctions::print(
            "AudioStreamPlayerVoipExtension received out of order Opus Packet. packetNumber: ",
            packetNumber, " expected: ", _runningPacketNumber + 1 );
    }
    _runningPacketNumber = packetNumber;
    _sampleBuffer.resize( audio_package_duration_ms * mix_rate / 1000 );
    int numDecodedSamples =
        opus_decode_float( _opus_decoder, packet.ptr(), static_cast<int>( packet.size() ),
                           _sampleBuffer.ptrw(), audio_package_duration_ms * mix_rate / 1000, 0 );
    if ( numDecodedSamples <= 0 )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension could not decode received packet. Opus errorcode: ",
            numDecodedSamples );
        return;
    }
    if ( numDecodedSamples != audio_package_duration_ms * mix_rate / 1000 )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension Number of decoded samples doesn't match expectation!. "
            "numDecodedSamples: ",
            numDecodedSamples );
    }
    godot::PackedVector2Array bufferInStreamFormat;
    bufferInStreamFormat.resize( numDecodedSamples );
    _current_loudness = 0;
    for ( int i = 0; i < numDecodedSamples; ++i )
    {
        bufferInStreamFormat[i] = godot::Vector2( _sampleBuffer[i], _sampleBuffer[i] );
        _current_loudness += _sampleBuffer[i] * _sampleBuffer[i];
    }
    _current_loudness = godot::Math::sqrt( _current_loudness / numDecodedSamples );

    bool pushed_successfully = _audioStreamGeneratorPlayback->push_buffer( bufferInStreamFormat );
    if ( !pushed_successfully )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension could not push received audio buffer into the "
            "AudioStreamGeneratorPlayback. Free Space: ",
            _audioStreamGeneratorPlayback->get_frames_available(),
            " needed space: ", bufferInStreamFormat.size() );
        // let's try to push at least as much as possible...
        bufferInStreamFormat.resize( _audioStreamGeneratorPlayback->get_frames_available() );
        _audioStreamGeneratorPlayback->push_buffer( bufferInStreamFormat );
    }
}
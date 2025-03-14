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
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/display_server.hpp"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/variant/callable.hpp"
#include <resampler/MultiChannelResampler.h>

#include "opus.h"

void AudioStreamPlayerVoipExtension::_bind_methods()
{
    godot::ClassDB::bind_method( godot::D_METHOD( "transfer_opus_packet_rpc", "packet" ),
                                 &AudioStreamPlayerVoipExtension::transfer_opus_packet_rpc );
    godot::ClassDB::bind_method( godot::D_METHOD( "initialize" ),
                                 &AudioStreamPlayerVoipExtension::initialize );
    godot::ClassDB::bind_method( godot::D_METHOD( "process_and_send_buffer_thread" ),
                                 &AudioStreamPlayerVoipExtension::process_microphone_buffer_thread );
    godot::ClassDB::bind_method( godot::D_METHOD( "add_to_streamplayer" ),
                                 &AudioStreamPlayerVoipExtension::add_to_streamplayer );
    godot::ClassDB::bind_method( godot::D_METHOD( "add_to_streamplayer2D" ),
                                 &AudioStreamPlayerVoipExtension::add_to_streamplayer2D );
    godot::ClassDB::bind_method( godot::D_METHOD( "add_to_streamplayer3D" ),
                                 &AudioStreamPlayerVoipExtension::add_to_streamplayer3D );
    godot::ClassDB::bind_method( godot::D_METHOD( "remove_from_streamplayer" ),
                                 &AudioStreamPlayerVoipExtension::remove_from_streamplayer );
    godot::ClassDB::bind_method( godot::D_METHOD( "remove_from_streamplayer2D" ),
                                 &AudioStreamPlayerVoipExtension::remove_from_streamplayer2D );
    godot::ClassDB::bind_method( godot::D_METHOD( "remove_from_streamplayer3D" ),
                                 &AudioStreamPlayerVoipExtension::remove_from_streamplayer3D );

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
                  "get_current_loudness_db" );
}

void AudioStreamPlayerVoipExtension::_ready()
{
    godot::Dictionary conf;
    conf["rpc_mode"] = godot::MultiplayerAPI::RPCMode::RPC_MODE_AUTHORITY;
    conf["transfer_mode"] = godot::MultiplayerPeer::TransferMode::TRANSFER_MODE_UNRELIABLE_ORDERED;
    conf["call_local"] = false;
    conf["channel"] = 9;
    rpc_config( "transfer_opus_packet_rpc", conf );

    _encodeBufferMutex.instantiate();
    set_process_mode( PROCESS_MODE_ALWAYS );
    set_process( !godot::Engine::get_singleton()->is_editor_hint() );
}

void AudioStreamPlayerVoipExtension::process_microphone_buffer_thread()
{
    while ( !_cancel_process_thread )
    {
        if ( _audioEffectCapture.is_valid() &&
             _audioEffectCapture->can_get_buffer( audio_package_duration_ms * godot_mix_rate /
                                                  1000 ) )
        {
            godot::PackedVector2Array stereoSampleBuffer = _audioEffectCapture->get_buffer(
                audio_package_duration_ms * godot_mix_rate / 1000 );
            // while(_audioEffectCapture->get_frames_available() > audio_package_duration_ms *
            // godot_mix_rate / 1000)
            // {
            //     // godot::UtilityFunctions::print_verbose(
            //     //     "AudioStreamPlayerVoipExtension audioEffectCapture buffer too full,
            //     discarding frames! available frames: ",
            //     //     _audioEffectCapture->get_frames_available() );
            //     _audioEffectCapture->get_buffer(audio_package_duration_ms * godot_mix_rate /
            //     1000);
            // }
            _sampleBuffer.resize( stereoSampleBuffer.size() * 2 );
            int numSamplesInSampleBuffer = 0;
            _current_loudness = 0;
            if ( _resampler != nullptr )
            {
                int inputSamplesLeft = (int)stereoSampleBuffer.size();
                int inputIndex = 0;
                int outputIndex = 0;
                while ( inputSamplesLeft > 0 )
                {
                    if ( _resampler->isWriteNeeded() )
                    {
                        _resampler->writeNextFrame( &stereoSampleBuffer[inputIndex].x );
                        inputIndex++;
                        inputSamplesLeft--;
                    }
                    else
                    {
                        _resampler->readNextFrame( &_sampleBuffer[outputIndex] );
                        _current_loudness +=
                            _sampleBuffer[outputIndex] * _sampleBuffer[outputIndex];
                        outputIndex++;
                        numSamplesInSampleBuffer++;
                    }
                }
            }
            else
            {
                for ( int i = 0; i < stereoSampleBuffer.size(); ++i )
                {
                    _sampleBuffer[i] = stereoSampleBuffer[i].x;
                    _current_loudness += _sampleBuffer[i] * _sampleBuffer[i];
                }
                numSamplesInSampleBuffer = (int)stereoSampleBuffer.size();
            }
            _current_loudness = godot::Math::sqrt( _current_loudness /
                                                   static_cast<float>( numSamplesInSampleBuffer ) );

            _encodeBufferMutex->lock();
            int encodeBufferIndex = -1;
            if (_encodeBuffersReadyToBeSent.size() >= _encodeBuffers.size())
            {
                if (_encodeBuffers.size() < 10) {
                    _encodeBuffers.push_back( {} );
                    encodeBufferIndex = _encodeBuffers.size() - 1;
                }
            }
            else
            {
                for ( int i = 0; i < _encodeBuffers.size(); ++i )
                {
                    if (_encodeBuffersReadyToBeSent.has( i ))
                        continue;
                    encodeBufferIndex = i;
                    break;
                }
            }
            _encodeBufferMutex->unlock();
            if (encodeBufferIndex != -1)
            {
                _encodeBuffers[encodeBufferIndex].resize( 150000 );
                int sizeOfEncodedPackage = opus_encode_float(
                    _opus_encoder, _sampleBuffer.ptr(), static_cast<int>( numSamplesInSampleBuffer ),
                    _encodeBuffers[encodeBufferIndex].ptrw(), static_cast<int>( _encodeBuffers[encodeBufferIndex].size() ) );
                if ( sizeOfEncodedPackage > 0 )
                {
                    _encodeBuffers[encodeBufferIndex].resize( sizeOfEncodedPackage );
                    _encodeBufferMutex->lock();
                    _encodeBuffersReadyToBeSent.append( encodeBufferIndex );
                    _encodeBufferMutex->unlock();
                }
                else
                {
                    godot::UtilityFunctions::printerr( "AudioStreamPlayerVoipExtension could not "
                                                       "encode captured audio. Opus errorcode: ",
                                                       sizeOfEncodedPackage );
                }
            }

            // when we (as the sender!) also have players added, we can simply push our captured
            // input on those. (needed, for example so that you can hear your own voice through a
            // walkie talkie)
            for ( auto &audioStreamGeneratorPlayback : _audioStreamGeneratorPlaybacks )
            {
                bool pushed_successfully =
                    audioStreamGeneratorPlayback->push_buffer( stereoSampleBuffer );
                if ( !pushed_successfully )
                {
                    // godot::UtilityFunctions::print_verbose(
                    //     "AudioStreamPlayerVoipExtension could not push received audio buffer into
                    //     the " "AudioStreamGeneratorPlayback. Free Space: ",
                    //     audioStreamGeneratorPlayback->get_frames_available(),
                    //     " needed space: ", bufferInStreamFormat.size() );
                    // let's try to push at least as much as possible...
                    stereoSampleBuffer.resize(
                        audioStreamGeneratorPlayback->get_frames_available() );
                    audioStreamGeneratorPlayback->push_buffer( stereoSampleBuffer );
                }
            }
        }

        godot::OS::get_singleton()->delay_msec( 1 );
    }
}

void AudioStreamPlayerVoipExtension::_process( double p_delta )
{
    Node::_process( p_delta );

    _encodeBufferMutex->lock();
    while(!_encodeBuffersReadyToBeSent.is_empty())
    {
        int sendEncodeBufferIndex = _encodeBuffersReadyToBeSent[0];
        _encodeBufferMutex->unlock();

        _runningPacketNumber += 1;
        rpc("transfer_opus_packet_rpc", _runningPacketNumber,
                       _encodeBuffers[sendEncodeBufferIndex] );
        _encodeBufferMutex->lock();
        _encodeBuffersReadyToBeSent.remove_at( 0 );
    }
    _encodeBufferMutex->unlock();
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
            audioEffectCapture->set_buffer_length( buffer_length * 1.5f );
            audioserver->add_bus_effect( capture_bus_index, audioEffectCapture );
        }
        else
        {
            if ( audioserver->get_bus_effect_count( capture_bus_index ) == 0 ||
                 !audioserver->get_bus_effect( capture_bus_index, audioserver->get_bus_effect_count( capture_bus_index )-1 )
                      ->is_class( "AudioEffectCapture" ) )
            {
                // our MicCapture bus should have an "AudioEffectCapture" Effect as the last effect
                godot::UtilityFunctions::printerr(
                    "AudioStreamPlayerVoipExtension detected invalid MicCapture audio bus "
                    "configuration. Attempting to fix it..." );
                for ( int i = audioserver->get_bus_effect_count( capture_bus_index ) - 1; i >= 0;
                      --i )
                {
                    // we'll just remove all AudioEffectCapture effects...
                    if(audioserver->get_bus_effect( capture_bus_index, i )
                      ->is_class( "AudioEffectCapture" ))
                        audioserver->remove_bus_effect( capture_bus_index, i );
                }
                // and create one to add as the last one.
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

    // _debugInfoWindow = new DebugInfoWindow();
    // _debugInfoWindow->Initialize(
    //     godot::String( "Player " ) + godot::itos( get_multiplayer_authority() ) +
    //     ( is_multiplayer_authority() ? godot::String( " (me)" ) : godot::String( " (remote)" ) ) );
    // get_tree()->get_root()->add_child( _debugInfoWindow );
    // get_window()->grab_focus();

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
        // To record the microphone, we'll need a simple AudioStreamPlayer. So we just create it:
        godot::AudioStreamPlayer *micCaptureStreamPlayer = memnew( godot::AudioStreamPlayer() );
        godot::AudioStreamMicrophone *micCaptureStream = memnew( godot::AudioStreamMicrophone() );
        micCaptureStreamPlayer->set_stream( micCaptureStream );
        micCaptureStreamPlayer->set_bus( "MicCapture" );
        add_child( micCaptureStreamPlayer );
        micCaptureStreamPlayer->play();

        int capture_bus_index = audioserver->get_bus_index( "MicCapture" );
        _audioEffectCapture = audioserver->get_bus_effect( capture_bus_index, audioserver->get_bus_effect_count( capture_bus_index )-1 );
        _audioEffectCapture->set_buffer_length( buffer_length );

        godot_mix_rate = (int)audioserver->get_mix_rate();
        if ( godot_mix_rate != mix_rate )
        {
            _resampler = oboe::resampler::MultiChannelResampler::make(
                1, godot_mix_rate, mix_rate,
                oboe::resampler::MultiChannelResampler::Quality::High );
        }

        _cancel_process_thread = false;
        _process_send_buffer_thread.instantiate();
        _process_send_buffer_thread->start( godot::Callable(this, "process_and_send_buffer_thread") );

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

        godot_mix_rate = (int)audioserver->get_mix_rate();
        if ( godot_mix_rate != mix_rate )
        {
            _resampler = oboe::resampler::MultiChannelResampler::make(
                1, mix_rate, godot_mix_rate,
                oboe::resampler::MultiChannelResampler::Quality::High );
        }

        _first_packet = true;

        godot::UtilityFunctions::print(
            "AudioStreamPlayerVoipExtension initialized as AudioStreamGenerator successfully." );
    }
}
void AudioStreamPlayerVoipExtension::add_to_streamplayer(
    godot::AudioStreamPlayer* audioStreamPlayer )
{
    godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
    audio_stream_generator.instantiate();
    // resampling is done via our own resampler, if necessary
    audio_stream_generator->set_mix_rate( godot_mix_rate );
    audio_stream_generator->set_buffer_length( buffer_length );
    audioStreamPlayer->set_stream( audio_stream_generator );
    audioStreamPlayer->play();
    godot::Ref<godot::AudioStreamGeneratorPlayback> stream_playback = audioStreamPlayer->get_stream_playback();
    _audioStreamGeneratorPlaybacks.append( stream_playback );
    _audioStreamGeneratorPlaybacksOwners.push_back( audioStreamPlayer->get_instance_id() );
}
void AudioStreamPlayerVoipExtension::add_to_streamplayer2D(
    godot::AudioStreamPlayer2D* audioStreamPlayer2D )
{
    godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
    audio_stream_generator.instantiate();
    // resampling is done via our own resampler, if necessary
    audio_stream_generator->set_mix_rate( godot_mix_rate );
    audio_stream_generator->set_buffer_length( buffer_length );
    audioStreamPlayer2D->set_stream( audio_stream_generator );
    audioStreamPlayer2D->play();
    godot::Ref<godot::AudioStreamGeneratorPlayback> stream_playback = audioStreamPlayer2D->get_stream_playback();
    _audioStreamGeneratorPlaybacks.append( stream_playback );
    _audioStreamGeneratorPlaybacksOwners.push_back( audioStreamPlayer2D->get_instance_id() );
}
void AudioStreamPlayerVoipExtension::add_to_streamplayer3D(
    godot::AudioStreamPlayer3D *audioStreamPlayer3D )
{
    godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
    audio_stream_generator.instantiate();
    // resampling is done via our own resampler, if necessary
    audio_stream_generator->set_mix_rate( godot_mix_rate );
    audio_stream_generator->set_buffer_length( buffer_length );
    audioStreamPlayer3D->call("play_stream",  audio_stream_generator, 0, 0, 0 );
    godot::Ref<godot::AudioStreamGeneratorPlayback> stream_playback;
    // compatibility with godot_steam_audio extension: we need to get the "inner stream" playback
    // in that case!
    if (audioStreamPlayer3D->has_method("get_inner_stream_playback"))
        stream_playback = audioStreamPlayer3D->call("get_inner_stream_playback");
    else
        stream_playback = audioStreamPlayer3D->get_stream_playback();
    _audioStreamGeneratorPlaybacks.append( stream_playback );
    _audioStreamGeneratorPlaybacksOwners.push_back( audioStreamPlayer3D->get_instance_id() );
}

void AudioStreamPlayerVoipExtension::remove_from_streamplayer(
    godot::AudioStreamPlayer *audioStreamPlayer )
{
    auto playbackIndex = _audioStreamGeneratorPlaybacksOwners.find( audioStreamPlayer->get_instance_id() );
    if(playbackIndex == -1)
        return;
    _audioStreamGeneratorPlaybacks.remove_at( playbackIndex );
    _audioStreamGeneratorPlaybacksOwners.remove_at( playbackIndex );
}

void AudioStreamPlayerVoipExtension::remove_from_streamplayer2D(
    godot::AudioStreamPlayer2D *audioStreamPlayer2D )
{
    auto playbackIndex = _audioStreamGeneratorPlaybacksOwners.find( audioStreamPlayer2D->get_instance_id() );
    if(playbackIndex == -1)
        return;
    _audioStreamGeneratorPlaybacks.remove_at( playbackIndex );
    _audioStreamGeneratorPlaybacksOwners.remove_at( playbackIndex );
}

void AudioStreamPlayerVoipExtension::remove_from_streamplayer3D(
    godot::AudioStreamPlayer3D *audioStreamPlayer3D )
{
    auto playbackIndex = _audioStreamGeneratorPlaybacksOwners.find( audioStreamPlayer3D->get_instance_id() );
    if(playbackIndex == -1)
        return;
    _audioStreamGeneratorPlaybacks.remove_at( playbackIndex );
    _audioStreamGeneratorPlaybacksOwners.remove_at( playbackIndex );
}

void AudioStreamPlayerVoipExtension::_exit_tree()
{
    godot::UtilityFunctions::print(
        "AudioStreamPlayerVoipExtension exit_tree: cleaning up opus and audiostream" );
    if ( _process_send_buffer_thread.is_valid() && _process_send_buffer_thread->is_alive() )
    {
        _cancel_process_thread = true;
        _process_send_buffer_thread->wait_to_finish();
        _process_send_buffer_thread.unref();
    }
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
    if ( _resampler != nullptr )
    {
        delete _resampler;
        _resampler = nullptr;
    }
    _audioStreamGeneratorPlaybacks.clear();
}

bool AudioStreamPlayerVoipExtension::decode_and_push_to_players( const uint8_t *packet_data,
                                                                 int64_t packet_size )
{
    int numDecodedSamples =
        opus_decode_float( _opus_decoder, packet_data, static_cast<int>( packet_size ),
                           _sampleBuffer.ptrw(), audio_package_duration_ms * mix_rate / 1000, 0 );
    if ( numDecodedSamples <= 0 )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension could not decode received packet. Opus errorcode: ",
            numDecodedSamples );
        return false;
    }
    godot::PackedVector2Array bufferInStreamFormat;
    int numSamplesInBuffer = 0;
    if ( _resampler != nullptr )
    {
        int inputSamplesLeft = (int)numDecodedSamples;
        int inputIndex = 0;
        int outputIndex = 0;
        while ( inputSamplesLeft > 0 )
        {
            if ( _resampler->isWriteNeeded() )
            {
                _resampler->writeNextFrame( &_sampleBuffer[inputIndex] );
                inputIndex++;
                inputSamplesLeft--;
            }
            else
            {
                float frame;
                _resampler->readNextFrame( &frame );
                bufferInStreamFormat.append( godot::Vector2( frame, frame ) );
                _current_loudness += frame * frame;
                outputIndex++;
                numSamplesInBuffer++;
            }
        }
    }
    else
    {
        bufferInStreamFormat.resize( numDecodedSamples );
        _current_loudness = 0;
        for ( int i = 0; i < numDecodedSamples; ++i )
        {
            bufferInStreamFormat[i] = godot::Vector2( _sampleBuffer[i], _sampleBuffer[i] );
            _current_loudness += _sampleBuffer[i] * _sampleBuffer[i];
        }
        numSamplesInBuffer = numDecodedSamples;
    }
    _current_loudness =
        godot::Math::sqrt( _current_loudness / static_cast<float>( numSamplesInBuffer ) );

    for ( auto &audioStreamGeneratorPlayback : _audioStreamGeneratorPlaybacks )
    {
        bool pushed_successfully =
            audioStreamGeneratorPlayback->push_buffer( bufferInStreamFormat );
        if ( !pushed_successfully )
        {
            // godot::UtilityFunctions::print_verbose(
            //     "AudioStreamPlayerVoipExtension could not push received audio buffer into the "
            //     "AudioStreamGeneratorPlayback. Free Space: ",
            //     audioStreamGeneratorPlayback->get_frames_available(),
            //     " needed space: ", bufferInStreamFormat.size() );
            // let's try to push at least as much as possible...
            bufferInStreamFormat.resize( audioStreamGeneratorPlayback->get_frames_available() );
            audioStreamGeneratorPlayback->push_buffer( bufferInStreamFormat );
        }
    }

    // _debugInfoWindow->SetString( "bytes", godot::itos(packet.size() ));
    // _debugInfoWindow->SetString( "num out of order", godot::itos( _num_out_of_order ) );
    // _debugInfoWindow->SetString( "buffer size", godot::itos(
    //     static_cast<int>( buffer_length * static_cast<float>( mix_rate ) ) -
    //     _audioStreamGeneratorPlayback->get_frames_available()) );
    // _debugInfoWindow->AddToGraph( "loudness", _current_loudness);
    return true;
}

void AudioStreamPlayerVoipExtension::transfer_opus_packet_rpc( unsigned char packetNumber,
                                                            const godot::PackedByteArray &packet )
{
    // this function was called by the rpc system because a packet was sent to us
    // make sure that this instance is prepared to receive the packet!
    if ( _opus_decoder == nullptr )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension received Opus Packet, but is not ready for it! "
            "(_audioStreamGeneratorPlayback or _opus_decoder is null)" );
        return;
    }
    int missed_packets = 0;
    while (missed_packets < 10 && ++_runningPacketNumber != packetNumber) {
        missed_packets++;
    }
    if ( !_first_packet && missed_packets > 0 ) {
        // godot::UtilityFunctions::print_verbose("AudioStreamPlayerVoipExtension missed ", missed_packets, " packets." );
        _num_out_of_order += missed_packets;

        // we have to push the amount of missing packages, so that the opus state is not messed up!
        for ( int i = 0; i < missed_packets; ++i ) {
            decode_and_push_to_players( nullptr, 0);
        }
    }
    _first_packet = false;
    _runningPacketNumber = packetNumber;
    _sampleBuffer.resize( 2 * audio_package_duration_ms * mix_rate / 1000 );
    const uint8_t *packet_data = packet.ptr();
    int64_t packet_size = packet.size();

    decode_and_push_to_players( packet_data, packet_size );
}
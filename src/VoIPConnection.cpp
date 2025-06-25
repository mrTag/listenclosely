#include "VoIPConnection.h"

#include "opus.h"
#include "resampler/MultiChannelResampler.h"

#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_microphone.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/audio_stream_player.hpp"
#include "godot_cpp/classes/audio_stream_player2d.hpp"
#include "godot_cpp/classes/audio_stream_player3d.hpp"
#include "godot_cpp/classes/multiplayer_api.hpp"
#include "godot_cpp/classes/multiplayer_peer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/display_server.hpp"
#include "godot_cpp/variant/callable.hpp"

const int VOIP_CHANNEL = 11;

void VoIPConnection::_bind_methods()
{
    godot::ClassDB::bind_method(godot::D_METHOD("initialize", "multiplayer_peer"), 
                               &VoIPConnection::initialize);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("play_peer_on_audio_stream_player", "peer_id", "audio_stream_player"),
                               &VoIPConnection::play_peer_on_audio_stream_player);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("play_peer_on_audio_stream_player_2d", "peer_id", "audio_stream_player"),
                               &VoIPConnection::play_peer_on_audio_stream_player_2d);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("play_peer_on_audio_stream_player_3d", "peer_id", "audio_stream_player"),
                               &VoIPConnection::play_peer_on_audio_stream_player_3d);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("stop_peer_on_audio_stream_player", "audio_stream_player"),
                               &VoIPConnection::stop_peer_on_audio_stream_player);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("stop_all_audio_stream_players_for_peer", "peer_id"),
                               &VoIPConnection::stop_all_audio_stream_players_for_peer);

    godot::ClassDB::bind_method(godot::D_METHOD("capture_encode_send_thread_loop"),
                               &VoIPConnection::capture_encode_send_thread_loop);
                               
    godot::ClassDB::bind_method(godot::D_METHOD("receive_decode_thread_loop"),
                               &VoIPConnection::receive_decode_thread_loop);

    godot::ClassDB::bind_method(godot::D_METHOD("peer_disconnected"),
                               &VoIPConnection::peer_disconnected);
}

void VoIPConnection::capture_encode_send_thread_loop()
{
    godot::PackedFloat32Array float32_buffer;
    godot::PackedByteArray byte_buffer;
    uint8_t packet_number = 0;
    while (!close_threads.load( ))
    {
        // capture from the microphone
        if ( sending_peer._audio_effect_capture.is_valid() &&
             sending_peer._audio_effect_capture->can_get_buffer( audio_package_duration_ms * godot_mix_rate /
                                                  1000 ) )
        {
            godot::PackedVector2Array stereoSampleBuffer = sending_peer._audio_effect_capture->get_buffer(
                audio_package_duration_ms * godot_mix_rate / 1000 );

            // having audio stream players for the sending side is rare, but can happen
            // for things like walkie-talkies or intercoms, where the player could hear themselves...
            sending_audio_stream_vectors_mutex->lock();
            for ( auto &audioStreamGeneratorPlayback : sending_peer.audio_stream_generator_playbacks )
            {
                bool pushed_successfully =
                    audioStreamGeneratorPlayback->push_buffer( stereoSampleBuffer );
                if ( !pushed_successfully )
                {
                    godot::UtilityFunctions::print_verbose(
                        "VoIPConnection could not push received audio buffer into the "
                        "AudioStreamGeneratorPlayback. Free Space: ",
                        audioStreamGeneratorPlayback->get_frames_available(),
                        " needed space: ", stereoSampleBuffer.size() );
                }
            }
            sending_audio_stream_vectors_mutex->unlock();

            // opus needs the samples in a float32 buffer, we just reserve more than enough here
            float32_buffer.resize( stereoSampleBuffer.size() * 2 );
            int numSamplesInSampleBuffer = 0;
            if ( sending_peer._resampler != nullptr )
            {
                int inputSamplesLeft = (int)stereoSampleBuffer.size();
                int inputIndex = 0;
                int outputIndex = 0;
                while ( inputSamplesLeft > 0 )
                {
                    if ( sending_peer._resampler->isWriteNeeded() )
                    {
                        sending_peer._resampler->writeNextFrame( &stereoSampleBuffer[inputIndex].x );
                        inputIndex++;
                        inputSamplesLeft--;
                    }
                    else
                    {
                        sending_peer._resampler->readNextFrame( &float32_buffer[outputIndex] );
                        outputIndex++;
                        numSamplesInSampleBuffer++;
                    }
                }
            }
            else
            {
                for ( int i = 0; i < stereoSampleBuffer.size(); ++i )
                {
                    float32_buffer[i] = stereoSampleBuffer[i].x;
                }
                numSamplesInSampleBuffer = (int)stereoSampleBuffer.size();
            }

            // the encoded amount of data should be smaller than unencoded, but we
            // reserve double the amount, just to be safe (it is not much, and it won't
            // be allocated again after the first loop)
            byte_buffer.resize( numSamplesInSampleBuffer * 4 * 2 );
            // the first byte in our package is reserved for our package number!
            byte_buffer[0] = packet_number++;

            int sizeOfEncodedPackage = opus_encode_float(
                    sending_peer._opus_encoder, float32_buffer.ptr(), numSamplesInSampleBuffer,
                    byte_buffer.ptrw() + 1, static_cast<int>( byte_buffer.size() - 1 ) );
            if ( sizeOfEncodedPackage > 0 )
            {
                byte_buffer.resize( sizeOfEncodedPackage + 1 );
                multiplayer_peer_mutex->lock();
                multiplayer_peer->set_target_peer( 0 );
                multiplayer_peer->set_transfer_channel( VOIP_CHANNEL );
                multiplayer_peer->set_transfer_mode( godot::MultiplayerPeer::TRANSFER_MODE_UNRELIABLE );
                multiplayer_peer->put_packet( byte_buffer );
                multiplayer_peer_mutex->unlock();
            }
            else
            {
                godot::UtilityFunctions::printerr( "VoIPConnection could not "
                                                   "encode captured audio. Opus errorcode: ",
                                                   sizeOfEncodedPackage );
            }
        }
        godot::OS::get_singleton()->delay_msec( 1 );
    }
}

void decode(
    const uint8_t *packet_data,
    int64_t packet_size,
    OpusDecoder *opus_decoder,
    int frame_size,
    oboe::resampler::MultiChannelResampler * resampler,
    godot::PackedFloat32Array& float32_buffer,
    godot::PackedVector2Array& stream_buffer)
{
    stream_buffer.clear();
    int numDecodedSamples =
        opus_decode_float( opus_decoder, packet_data, static_cast<int>( packet_size ),
                           float32_buffer.ptrw(), frame_size, 0 );
    if ( numDecodedSamples <= 0 )
    {
        godot::UtilityFunctions::printerr(
            "VoIPConnection could not decode received packet. Opus errorcode: ",
            numDecodedSamples );
        return;
    }
    if ( resampler != nullptr )
    {
        int inputSamplesLeft = numDecodedSamples;
        int inputIndex = 0;
        int outputIndex = 0;
        while ( inputSamplesLeft > 0 )
        {
            if ( resampler->isWriteNeeded() )
            {
                resampler->writeNextFrame( &float32_buffer[inputIndex] );
                inputIndex++;
                inputSamplesLeft--;
            }
            else
            {
                float frame;
                resampler->readNextFrame( &frame );
                stream_buffer.append( godot::Vector2( frame, frame ) );
                outputIndex++;
            }
        }
    }
    else
    {
        stream_buffer.resize( numDecodedSamples );
        for ( int i = 0; i < numDecodedSamples; ++i )
        {
            stream_buffer[i] = godot::Vector2( float32_buffer[i], float32_buffer[i] );
        }
    }
}

void VoIPConnection::receive_decode_thread_loop()
{
    godot::PackedFloat32Array float32_buffer;
    float32_buffer.resize( 2 * audio_package_duration_ms * mix_rate / 1000 );
    godot::PackedVector2Array stream_buffer;
    while (!close_threads.load( ))
    {
        // poll the multiplayer_peer
        multiplayer_peer_mutex->lock();
        multiplayer_peer->poll();
        // the rest of the multiplayer peer functions don't need the mutex
        multiplayer_peer_mutex->unlock();
        // while there are packets, get them
        while ( multiplayer_peer->get_available_packet_count() > 0 )
        {
            if (multiplayer_peer->get_packet_channel() != VOIP_CHANNEL)
            {
                continue;
            }
            int64_t packet_peer = multiplayer_peer->get_packet_peer();
            godot::PackedByteArray packet = multiplayer_peer->get_packet();
            // check if we have a receiving_peer for them
            receiving_peers_mutex->lock();
            VoIPReceivingPeer *receiving_peer = get_receiving_peer_for_id( packet_peer );
            if ( receiving_peer != nullptr )
            {
                // since the packets can arrive in any order, we'll store them in a queue
                // and process this queue independent of the packet receiving
                receiving_peer->queued_packets.push_back( packet );
            }
            receiving_peers_mutex->unlock();
        }

        // the processing of the packets happens independently of receiving
        uint64_t now = godot::Time::get_singleton()->get_ticks_msec();
        const int buffer_urgency_threshold_ms = 10;
        for ( int i = 0; i < receiving_peers.size(); ++i )
        {
            receiving_peers_mutex->lock();
            VoIPReceivingPeer *receiving_peer = &receiving_peers.write[i];
            // we need to initialize the expected packet number (first packet)
            // or reset it, when we skipped too many packets
            if (!receiving_peer->received_first_packet || receiving_peer->skipped_packets > 5)
            {
                receiving_peer->skipped_packets = 0;
                if (!receiving_peer->queued_packets.is_empty())
                {
                    receiving_peer->received_first_packet = true;
                    receiving_peer->expected_packet_number = receiving_peer->queued_packets[0][0];
                }
                else
                {
                    // when the decoder hasn't had any packet to process, there is no
                    // sense in feeding it nullptr packets (also: leads to crash!)
                    continue;
                }
            }
            bool processed_packet = true;
            while (processed_packet)
            {
                bool stream_needs_packet_urgently = receiving_peer->stream_has_packets_until < now - buffer_urgency_threshold_ms;
                processed_packet = false;
                for ( int j = 0; j < receiving_peer->queued_packets.size(); ++j )
                {
                    if ( receiving_peer->queued_packets[j][0] == receiving_peer->expected_packet_number )
                    {
                        godot::PackedByteArray packet = receiving_peer->queued_packets[j];
                        receiving_peer->queued_packets.remove_at( j );
                        receiving_peer->expected_packet_number++;
                        receiving_peer->stream_has_packets_until += audio_package_duration_ms;
                        auto opus_decoder = receiving_peer->opus_decoder;
                        auto resampler = receiving_peer->_resampler;
                        // unlock the mutex for the decoding time...
                        receiving_peers_mutex->unlock();
                        decode(packet.ptr() + 1, packet.size() - 1,
                            opus_decoder, audio_package_duration_ms * mix_rate / 1000,
                            resampler, float32_buffer, stream_buffer);
                        processed_packet = true;
                        receiving_peers_mutex->lock();
                        break;
                    }
                }
                if (!processed_packet && stream_needs_packet_urgently)
                {
                    // we need a packet urgently, but the correct packet is not there
                    // so we'll have to decode one nullptr packet in the meantime and
                    // increase the expected_packet_number accordingly
                    receiving_peer->expected_packet_number++;
                    receiving_peer->stream_has_packets_until += audio_package_duration_ms;
                    auto opus_decoder = receiving_peer->opus_decoder;
                    auto resampler = receiving_peer->_resampler;
                    // unlock the mutex for the decoding time...
                    receiving_peers_mutex->unlock();
                    decode(nullptr, 0,
                        opus_decoder, audio_package_duration_ms * mix_rate / 1000,
                        resampler, float32_buffer, stream_buffer);
                    godot::UtilityFunctions::print_verbose(
                        "VoIPConnection didn't receive packet in time, skipping one packet.");
                    processed_packet = true;
                    receiving_peer->skipped_packets++;
                    receiving_peers_mutex->lock();
                }
                if (processed_packet)
                {
                    receiving_audio_stream_vectors_mutex->lock();
                    for ( auto &audioStreamGeneratorPlayback : receiving_peer->audio_stream_generator_playbacks )
                    {
                        bool pushed_successfully =
                            audioStreamGeneratorPlayback->push_buffer( stream_buffer );
                        if ( !pushed_successfully )
                        {
                            godot::UtilityFunctions::print_verbose(
                                "VoIPConnection could not push received audio buffer into the "
                                "AudioStreamGeneratorPlayback. Free Space: ",
                                audioStreamGeneratorPlayback->get_frames_available(),
                                " needed space: ", stream_buffer.size() );
                        }
                    }
                    receiving_audio_stream_vectors_mutex->unlock();
                }
            }
            // skipped packets have to be dropped from the queue
            for ( int j = receiving_peer->queued_packets.size() - 1; j >= 0; --j)
            {
                int packet_num = receiving_peer->queued_packets[j][0];
                if ( packet_num < receiving_peer->expected_packet_number && receiving_peer->expected_packet_number - packet_num < 127 )
                {
                    // we can drop this packet, since it's too old
                    receiving_peer->queued_packets.remove_at( j );
                    godot::UtilityFunctions::print_verbose(
                                "VoIPConnection dropped packet, likely because we had to "
                                "skip it earlier, because it arrived too late.");
                }
            }
            receiving_peers_mutex->unlock();
        }
        godot::OS::get_singleton()->delay_msec( 1 );
    }
}

void VoIPConnection::_exit_tree()
{
    godot::UtilityFunctions::print_verbose(
        "VoIPConnection exit_tree: cleaning up opus and audiostream" );
    close_threads.store(true);
    if (capture_encode_send_thread.is_valid())
    {
        capture_encode_send_thread->wait_to_finish();
        capture_encode_send_thread.unref();
    }
    if (receive_decode_thread.is_valid())
    {
        receive_decode_thread->wait_to_finish();
        receive_decode_thread.unref();
    }

    if ( sending_peer._opus_encoder != nullptr )
    {
        opus_encoder_destroy( sending_peer._opus_encoder );
        sending_peer._opus_encoder = nullptr;
    }
    sending_peer._audio_effect_capture = godot::Variant();
    delete sending_peer._resampler;
    sending_peer._resampler = nullptr;
    sending_peer.audio_stream_generator_playbacks.clear();
    sending_peer.audio_stream_generator_playbacks_owners.clear();

    for (const auto & receiving_peer : receiving_peers)
    {
        if (receiving_peer.opus_decoder != nullptr)
        {
            opus_decoder_destroy( receiving_peer.opus_decoder );
        }
        delete receiving_peer._resampler;
    }
    receiving_peers.clear();
}

void VoIPConnection::initialize( godot::Ref<godot::MultiplayerPeer> multiplayer_peer_param )
{
    receiving_peers_mutex.instantiate();
    receiving_audio_stream_vectors_mutex.instantiate();
    sending_audio_stream_vectors_mutex.instantiate();
    multiplayer_peer_mutex.instantiate();

    multiplayer_peer = multiplayer_peer_param;
    auto audioserver = godot::AudioServer::get_singleton();
    godot_mix_rate = static_cast<int>( audioserver->get_mix_rate() );

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
        if ( audioserver->get_bus_effect_count( capture_bus_index ) == 0 ||
             !audioserver
                  ->get_bus_effect( capture_bus_index,
                                    audioserver->get_bus_effect_count( capture_bus_index ) - 1 )
                  ->is_class( "AudioEffectCapture" ) )
        {
            // our MicCapture bus should have an "AudioEffectCapture" Effect as the last effect
            godot::UtilityFunctions::printerr(
                "AudioStreamPlayerVoipExtension detected invalid MicCapture audio bus "
                "configuration. Attempting to fix it..." );
            for ( int i = audioserver->get_bus_effect_count( capture_bus_index ) - 1; i >= 0; --i )
            {
                // we'll just remove all AudioEffectCapture effects...
                if ( audioserver->get_bus_effect( capture_bus_index, i )
                         ->is_class( "AudioEffectCapture" ) )
                {
                    audioserver->remove_bus_effect( capture_bus_index, i );
                }
            }
            // and create one to add as the last one.
            godot::Ref<godot::AudioEffectCapture> audioEffectCapture;
            audioEffectCapture.instantiate();
            audioEffectCapture->set_name( "Capture" );
            audioEffectCapture->set_buffer_length( buffer_length );
            audioserver->add_bus_effect( capture_bus_index, audioEffectCapture );
        }
    }

    int opus_error = 0;
    sending_peer._opus_encoder =
        opus_encoder_create( mix_rate, 1, OPUS_APPLICATION_VOIP, &opus_error );
    if ( opus_error != OPUS_OK )
    {
        godot::UtilityFunctions::printerr(
            "AudioStreamPlayerVoipExtension could not create Opus Encoder. Error: ", opus_error );
        return;
    }
    // To record the microphone, we'll need a simple AudioStreamPlayer. So we just create it:
    godot::AudioStreamPlayer *micCaptureStreamPlayer = memnew( godot::AudioStreamPlayer() );
    godot::AudioStreamMicrophone *micCaptureStream = memnew( godot::AudioStreamMicrophone() );
    micCaptureStreamPlayer->set_stream( micCaptureStream );
    micCaptureStreamPlayer->set_bus( "MicCapture" );
    add_child( micCaptureStreamPlayer );
    micCaptureStreamPlayer->play();

    sending_peer._audio_effect_capture = audioserver->get_bus_effect(
        capture_bus_index, audioserver->get_bus_effect_count( capture_bus_index ) - 1 );
    sending_peer._audio_effect_capture->set_buffer_length( buffer_length );

    godot_mix_rate = (int)audioserver->get_mix_rate();
    if ( godot_mix_rate != mix_rate )
    {
        sending_peer._resampler = oboe::resampler::MultiChannelResampler::make(
            1, godot_mix_rate, mix_rate, oboe::resampler::MultiChannelResampler::Quality::High );
    }

    // start both threads, as we'll need them for sure!
    capture_encode_send_thread.instantiate();
    capture_encode_send_thread->start( godot::Callable( this, "capture_encode_send_thread_loop" ) );
    receive_decode_thread.instantiate();
    receive_decode_thread->start( godot::Callable( this, "receive_decode_thread_loop" ) );

    multiplayer_peer->connect( "peer_disconnected", godot::Callable( this, "peer_disconnected" ) );

    godot::UtilityFunctions::print_verbose(
        "VoIPConnection MicCapture and opus encoder initialized successfully." );
}

void VoIPConnection::peer_disconnected( int64_t peer_id )
{
    godot::UtilityFunctions::print_verbose( "VoIPConnection peer disconnected: ", peer_id, " removing all VoIP remnants for this peer." );
    stop_all_audio_stream_players_for_peer( peer_id );
    receiving_peers_mutex->lock();
    for ( int i = 0; i < receiving_peers.size(); ++i )
    {
        if (receiving_peers[i].peer_id == peer_id)
        {
            receiving_peers.remove_at( i );
            break;
        }
    }
    receiving_peers_mutex->unlock();
}

template <typename AudioStreamPlayerClass>
godot::Ref<godot::AudioStreamGeneratorPlayback> create_playback( AudioStreamPlayerClass *player,
                                                                 int godot_mix_rate,
                                                                 float buffer_length )
{
    godot::Ref<godot::AudioStreamGenerator> audio_stream_generator;
    audio_stream_generator.instantiate();
    // resampling is done via our own resampler, if necessary
    audio_stream_generator->set_mix_rate( godot_mix_rate );
    audio_stream_generator->set_buffer_length( buffer_length );
    if (player->has_method( "play_stream" ))
    {
        player->call( "play_stream", audio_stream_generator, 0, 0, 0 );
    }
    else
    {
        player->set_stream( audio_stream_generator );
        player->play();
    }
    godot::Ref<godot::AudioStreamGeneratorPlayback> stream_playback;
    // compatibility with godot_steam_audio extension: we need to get the "inner stream" playback
    // in that case!
    if ( player->has_method( "get_inner_stream_playback" ) )
    {
        stream_playback = player->call( "get_inner_stream_playback" );
    }
    else
    {
        stream_playback = player->get_stream_playback();
    }
    if ( !stream_playback.is_valid() )
    {
        godot::UtilityFunctions::printerr("VoIPConnection: Could not create stream playback!");
    }
    else
    {
        // fill the stream_playback buffer with buffer_length silence
        godot::PackedVector2Array silence_buffer;
        silence_buffer.resize( buffer_length * godot_mix_rate );
        silence_buffer.fill( godot::Vector2( 0, 0 ) );
        stream_playback->push_buffer( silence_buffer );
    }
    return stream_playback;
}

VoIPConnection::VoIPReceivingPeer * VoIPConnection::get_receiving_peer_for_id( int64_t peer_id )
{
    VoIPReceivingPeer *receiving_peer = nullptr;
    for ( int i = 0; i < receiving_peers.size(); ++i )
    {
        if ( receiving_peers[i].peer_id == peer_id )
        {
            receiving_peer = &receiving_peers.write[i];
            break;
        }
    }
    if ( receiving_peer == nullptr )
    {
        VoIPReceivingPeer new_peer;
        new_peer.peer_id = peer_id;
        // we'll have to start with a full buffer (of silence). otherwise it will run into
        // buffer underruns right away.
        new_peer.stream_has_packets_until = godot::Time::get_singleton()->get_ticks_msec() + buffer_length * 1000;
        int opus_error = 0;
        new_peer.opus_decoder = opus_decoder_create( mix_rate, 1, &opus_error );
        if ( opus_error != OPUS_OK )
        {
            godot::UtilityFunctions::printerr(
                "AudioStreamPlayerVoipExtension could not create Opus Decoder. Opus errorcode: ",
                opus_error );
            return nullptr;
        }

        if ( godot_mix_rate != mix_rate )
        {
            new_peer._resampler = oboe::resampler::MultiChannelResampler::make(
                1, mix_rate, godot_mix_rate,
                oboe::resampler::MultiChannelResampler::Quality::High );
        }

        receiving_peers.push_back( new_peer );
        receiving_peer = &receiving_peers.write[receiving_peers.size() - 1];

        godot::UtilityFunctions::print( "Created opus decoder for peer ", peer_id,
                                        " successfully." );
    }
    return receiving_peer;
}
void VoIPConnection::play_peer_on_audio_stream_player(
    int64_t peer_id, godot::AudioStreamPlayer *audio_stream_player )
{
    if (!multiplayer_peer.is_valid())
    {
        return;
    }
    if ( peer_id == multiplayer_peer->get_unique_id() )
    {
        auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
        sending_audio_stream_vectors_mutex->lock();
        sending_peer.audio_stream_generator_playbacks.push_back( playback );
        sending_peer.audio_stream_generator_playbacks_owners.push_back(
            audio_stream_player->get_instance_id() );
        sending_audio_stream_vectors_mutex->unlock();
        return;
    }

    receiving_peers_mutex->lock();
    VoIPReceivingPeer *receiving_peer = get_receiving_peer_for_id( peer_id );
    receiving_peers_mutex->unlock();
    if ( receiving_peer == nullptr )
    {
        return;
    }

    auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
    receiving_audio_stream_vectors_mutex->lock();
    receiving_peer->audio_stream_generator_playbacks.push_back( playback );
    receiving_peer->audio_stream_generator_playbacks_owners.push_back(audio_stream_player->get_instance_id());
    receiving_audio_stream_vectors_mutex->unlock();
}

void VoIPConnection::play_peer_on_audio_stream_player_2d(
    int64_t peer_id, godot::AudioStreamPlayer2D *audio_stream_player )
{
    if (!multiplayer_peer.is_valid())
    {
        return;
    }
    if ( peer_id == multiplayer_peer->get_unique_id() )
    {
        auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
        sending_audio_stream_vectors_mutex->lock();
        sending_peer.audio_stream_generator_playbacks.push_back( playback );
        sending_peer.audio_stream_generator_playbacks_owners.push_back(
            audio_stream_player->get_instance_id() );
        sending_audio_stream_vectors_mutex->unlock();
        return;
    }

    receiving_peers_mutex->lock();
    VoIPReceivingPeer *receiving_peer = get_receiving_peer_for_id( peer_id );
    receiving_peers_mutex->unlock();
    if ( receiving_peer == nullptr )
    {
        return;
    }

    auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
    receiving_audio_stream_vectors_mutex->lock();
    receiving_peer->audio_stream_generator_playbacks.push_back( playback );
    receiving_peer->audio_stream_generator_playbacks_owners.push_back(audio_stream_player->get_instance_id());
    receiving_audio_stream_vectors_mutex->unlock();
}

void VoIPConnection::play_peer_on_audio_stream_player_3d(
    int64_t peer_id, godot::AudioStreamPlayer3D* audio_stream_player )
{
    if (!multiplayer_peer.is_valid())
    {
        return;
    }
    if ( peer_id == multiplayer_peer->get_unique_id() )
    {
        auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
        sending_audio_stream_vectors_mutex->lock();
        sending_peer.audio_stream_generator_playbacks.push_back( playback );
        sending_peer.audio_stream_generator_playbacks_owners.push_back(
            audio_stream_player->get_instance_id() );
        sending_audio_stream_vectors_mutex->unlock();
        return;
    }

    receiving_peers_mutex->lock();
    VoIPReceivingPeer *receiving_peer = get_receiving_peer_for_id( peer_id );
    receiving_peers_mutex->unlock();
    if ( receiving_peer == nullptr )
    {
        return;
    }

    auto playback = create_playback( audio_stream_player, godot_mix_rate, buffer_length );
    receiving_audio_stream_vectors_mutex->lock();
    receiving_peer->audio_stream_generator_playbacks.push_back( playback );
    receiving_peer->audio_stream_generator_playbacks_owners.push_back(audio_stream_player->get_instance_id());
    receiving_audio_stream_vectors_mutex->unlock();
}

void VoIPConnection::stop_peer_on_audio_stream_player( godot::Object *audio_stream_player )
{
    uint64_t instance_id = audio_stream_player->get_instance_id();
    auto sending_playback_index = sending_peer.audio_stream_generator_playbacks_owners.find( instance_id );
    if (sending_playback_index != -1)
    {
        sending_audio_stream_vectors_mutex->lock();
        sending_peer.audio_stream_generator_playbacks.remove_at( sending_playback_index );
        sending_peer.audio_stream_generator_playbacks_owners.remove_at( sending_playback_index );
        sending_audio_stream_vectors_mutex->unlock();
        return;
    }
    for ( int i = 0; i < receiving_peers.size(); ++i )
    {
        auto receiving_playback_index = receiving_peers[i].audio_stream_generator_playbacks_owners.find( instance_id );
        if (receiving_playback_index != -1)
        {
            receiving_audio_stream_vectors_mutex->lock();
            receiving_peers.write[i].audio_stream_generator_playbacks.remove_at( receiving_playback_index );
            receiving_peers.write[i].audio_stream_generator_playbacks_owners.remove_at( receiving_playback_index );
            receiving_audio_stream_vectors_mutex->unlock();
            return;
        }
    }
}

void VoIPConnection::stop_all_audio_stream_players_for_peer( int64_t peer_id )
{
    if (multiplayer_peer.is_valid() && peer_id == multiplayer_peer->get_unique_id())
    {
        sending_audio_stream_vectors_mutex->lock();
        sending_peer.audio_stream_generator_playbacks.clear();
        sending_peer.audio_stream_generator_playbacks_owners.clear();
        sending_audio_stream_vectors_mutex->unlock();
        return;
    }
    for ( int i = 0; i < receiving_peers.size(); ++i )
    {
        if (receiving_peers[i].peer_id == peer_id)
        {
            receiving_audio_stream_vectors_mutex->lock();
            receiving_peers.write[i].audio_stream_generator_playbacks.clear();
            receiving_peers.write[i].audio_stream_generator_playbacks_owners.clear();
            receiving_audio_stream_vectors_mutex->unlock();
            return;
        }
    }
}
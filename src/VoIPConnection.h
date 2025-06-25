#ifndef VOIPCONNECTION_H
#define VOIPCONNECTION_H

#include "godot_cpp/classes/audio_effect_capture.hpp"
#include "godot_cpp/classes/audio_stream_generator_playback.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/thread.hpp"
#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/templates/local_vector.hpp"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>

struct OpusEncoder;
struct OpusDecoder;
namespace oboe
{
    namespace resampler
    {
        class MultiChannelResampler;
    }
}


class VoIPConnection : public godot::Node
{
    GDCLASS( VoIPConnection, godot::Node )

protected:
    static void _bind_methods();

    // NOTE: opus has pretty strict requirements for the mix rate:
    // 48000 or 24000 or 16000 or 12000 or 8000
    int mix_rate = 24000;
    int godot_mix_rate = 48000;
    float buffer_length = 0.1f;
    int audio_package_duration_ms = 40;

    struct VoIPReceivingPeer
    {
        int64_t peer_id;

        uint8_t expected_packet_number = 0;
        uint8_t skipped_packets = 0;
        bool received_first_packet = false;
        godot::Vector<godot::PackedByteArray> queued_packets;

        OpusDecoder *opus_decoder = nullptr;
        oboe::resampler::MultiChannelResampler * _resampler = nullptr;
        uint64_t stream_has_packets_until;

        // there can be multiple audiostream players per peer, so these are vectors:
        godot::Vector<godot::Ref<godot::AudioStreamGeneratorPlayback>> audio_stream_generator_playbacks;
        godot::Vector<uint64_t> audio_stream_generator_playbacks_owners;
    };
    godot::Vector<VoIPReceivingPeer> receiving_peers;
    godot::Ref<godot::Mutex> receiving_peers_mutex;
    // this mutex protects changing the audio_stream vectors of all the receiving peers
    // since they won't be changed that often anyways.
    godot::Ref<godot::Mutex> receiving_audio_stream_vectors_mutex;

    struct VoIPSendingPeer
    {
        OpusEncoder *_opus_encoder = nullptr;
        oboe::resampler::MultiChannelResampler * _resampler = nullptr;
        godot::Ref<godot::AudioEffectCapture> _audio_effect_capture;
        // even when sending, there still can be audiostream players (walkie talkie, intercom...)
        godot::Vector<godot::Ref<godot::AudioStreamGeneratorPlayback>> audio_stream_generator_playbacks;
        godot::Vector<uint64_t> audio_stream_generator_playbacks_owners;
    };
    VoIPSendingPeer sending_peer;
    godot::Ref<godot::Mutex> sending_audio_stream_vectors_mutex;

    void capture_encode_send_thread_loop();
    void receive_decode_thread_loop();

    godot::Ref<godot::Thread> capture_encode_send_thread;
    godot::Ref<godot::Thread> receive_decode_thread;
    std::atomic_bool close_threads = false;

    godot::Ref<godot::MultiplayerPeer> multiplayer_peer;
    godot::Ref<godot::Mutex> multiplayer_peer_mutex;
    void peer_disconnected( int64_t peer_id );
public:
    void _exit_tree() override;
    void initialize( godot::Ref<godot::MultiplayerPeer> multiplayer_peer );

    VoIPReceivingPeer *get_receiving_peer_for_id( int64_t peer_id );
    void play_peer_on_audio_stream_player(
        int64_t peer_id, godot::AudioStreamPlayer* audio_stream_player );
    void play_peer_on_audio_stream_player_2d(
        int64_t peer_id, godot::AudioStreamPlayer2D* audio_stream_player );
    void play_peer_on_audio_stream_player_3d( int64_t peer_id,
                                              godot::AudioStreamPlayer3D *audio_stream_player );

    void stop_peer_on_audio_stream_player(godot::Object* audio_stream_player);
    void stop_all_audio_stream_players_for_peer(int64_t peer_id);
};



#endif //VOIPCONNECTION_H

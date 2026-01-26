#ifndef VOIPCONNECTION_H
#define VOIPCONNECTION_H

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
    float buffer_length = 0.05f;
    int audio_package_duration_ms = 20;

    struct VoIPReceivingPeer
    {
        int64_t peer_id;

        uint8_t expected_packet_number = 0;
        uint8_t skipped_packets = 0;
        bool received_first_packet = false;
        godot::Vector<godot::PackedByteArray> queued_packets;
        godot::Vector<uint64_t> queued_packets_received_times;

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
        int64_t peer_id;

        OpusEncoder *_opus_encoder = nullptr;
        oboe::resampler::MultiChannelResampler * _resampler = nullptr;
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
    std::atomic<int> sending_bandwidth = 0.0f;
    std::atomic<int> receiving_bandwidth = 0.0f;
    std::atomic<int> send_thread_iteration_duration = 0.0f;
    std::atomic<int> receive_thread_iteration_duration = 0.0f;
    std::atomic<float> microphone_loudness_db = 0.0f;
    std::atomic<float> microphone_peak_db = 0.0f;
    std::atomic<float> microphone_gain = 1.0f;

    godot::Ref<godot::MultiplayerPeer> multiplayer_peer;
    godot::Ref<godot::Mutex> multiplayer_peer_mutex;
    void peer_disconnected( int64_t peer_id );
public:
    void _exit_tree() override;
    void initialize( godot::Ref<godot::MultiplayerPeer> multiplayer_peer );

    float get_buffer_length() const { return buffer_length; }
    void set_buffer_length( float p_buffer_length ) { buffer_length = p_buffer_length; }

    int get_audio_package_duration_ms() const { return audio_package_duration_ms; }
    void set_audio_package_duration_ms(int duration) { audio_package_duration_ms = duration; }

    float get_microphone_loudness_db() const { return microphone_loudness_db.load(); }
    void set_microphone_loudness_db(float value) { microphone_loudness_db.store(value); }
    float get_microphone_peak_db() const { return microphone_peak_db.load(); }
    void set_microphone_peak_db(float value) { microphone_peak_db.store(value); }
    float get_microphone_gain() const { return microphone_gain.load(); }
    void set_microphone_gain(float value) { microphone_gain.store(value); }

    void lock_multiplayer_peer() { if (multiplayer_peer_mutex.is_valid()) multiplayer_peer_mutex->lock(); }
    void unlock_multiplayer_peer() { if (multiplayer_peer_mutex.is_valid()) multiplayer_peer_mutex->unlock(); }

    VoIPReceivingPeer *get_receiving_peer_for_id( int64_t peer_id );
    void play_peer_on_audio_stream_player(
        int64_t peer_id, godot::AudioStreamPlayer* audio_stream_player );
    void play_peer_on_audio_stream_player_2d(
        int64_t peer_id, godot::AudioStreamPlayer2D* audio_stream_player );
    void play_peer_on_audio_stream_player_3d( int64_t peer_id,
                                              godot::AudioStreamPlayer3D *audio_stream_player );

    void stop_peer_on_audio_stream_player(godot::Object* audio_stream_player);
    void stop_all_audio_stream_players_for_peer(int64_t peer_id);

    int get_number_of_receiving_peers() const { return receiving_peers.size(); }
    godot::String get_receiving_peer_debug_string(int peer_index ) const;
    godot::String get_sending_debug_string() const;
    int get_sending_bandwidth() const { return sending_bandwidth; }
    int get_receiving_bandwidth() const { return receiving_bandwidth; }
    int get_send_thread_iteration_duration() const { return send_thread_iteration_duration; }
    int get_receive_thread_iteration_duration() const { return receive_thread_iteration_duration; }
};



#endif //VOIPCONNECTION_H

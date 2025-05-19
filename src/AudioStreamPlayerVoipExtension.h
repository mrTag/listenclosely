#ifndef AUDIOSTREAMPLAYERVOIPEXTENSION_H
#define AUDIOSTREAMPLAYERVOIPEXTENSION_H

#include "godot_cpp/classes/audio_effect_capture.hpp"
#include "godot_cpp/classes/audio_stream_generator_playback.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/thread.hpp"
#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/templates/local_vector.hpp"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>

struct OpusEncoder;
struct OpusDecoder;
namespace oboe
{
    namespace resampler
    {
        class MultiChannelResampler;
    }
}

class AudioStreamPlayerVoipExtension : public godot::Node
{
    GDCLASS( AudioStreamPlayerVoipExtension, godot::Node )

protected:
    static void _bind_methods();

private:
    // NOTE: opus has pretty strict requirements on the mix rate: 48000 or 24000 or 16000 or 12000
    // or 8000
    int mix_rate = 24000;
    int godot_mix_rate = 48000;
    float buffer_length = 0.1f;
    int audio_package_duration_ms = 40;

    OpusDecoder *_opus_decoder = nullptr;
    godot::Vector<godot::Ref<godot::AudioStreamGeneratorPlayback>> _audioStreamGeneratorPlaybacks;
    godot::Vector<uint64_t> _audioStreamGeneratorPlaybacksOwners;

    OpusEncoder *_opus_encoder = nullptr;
    godot::Ref<godot::AudioEffectCapture> _audioEffectCapture;

    godot::Ref<godot::Thread> _process_send_buffer_thread;
    godot::LocalVector<godot::PackedByteArray> _encodeBuffers;
    godot::PackedInt32Array _encodeBuffersReadyToBeSent;
    godot::Ref<godot::Mutex> _encodeBufferMutex;

    godot::PackedFloat32Array _sampleBuffer;
    unsigned char _runningPacketNumber;
    bool _first_packet = true;
    float _current_loudness;
    bool _cancel_process_thread;

    int _num_out_of_order = 0;
    oboe::resampler::MultiChannelResampler * _resampler = nullptr;
public:
    void process_microphone_buffer_thread();
    void _process( double p_delta ) override;
    void _enter_tree() override;
    void _exit_tree() override;
    bool decode_and_push_to_players( const uint8_t *packet_data, int64_t packet_size );
    void _ready() override;

    void transfer_opus_packet_rpc( unsigned char packetNumber, const godot::PackedByteArray &packet );
    void initialize();
    void add_to_streamplayer(godot::AudioStreamPlayer* audioStreamPlayer);
    void add_to_streamplayer2D(godot::AudioStreamPlayer2D* audioStreamPlayer2D);
    void add_to_streamplayer3D(godot::AudioStreamPlayer3D* audioStreamPlayer3D);
    void remove_from_streamplayer(godot::AudioStreamPlayer* audioStreamPlayer);
    void remove_from_streamplayer2D(godot::AudioStreamPlayer2D* audioStreamPlayer2D);
    void remove_from_streamplayer3D(godot::AudioStreamPlayer3D* audioStreamPlayer3D);

    [[nodiscard]] int get_mix_rate() const
    {
        return mix_rate;
    }
    void set_mix_rate( const int new_mix_rate )
    {
        mix_rate = new_mix_rate;
    }
    [[nodiscard]] float get_buffer_length() const
    {
        return buffer_length;
    }
    void set_buffer_length( const float new_buffer_length )
    {
        buffer_length = new_buffer_length;
    }
    [[nodiscard]] float get_current_loudness() const
    {
        return _current_loudness;
    }
};

#endif // AUDIOSTREAMPLAYERVOIPEXTENSION_H

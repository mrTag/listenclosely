#ifndef AUDIOSTREAMPLAYERVOIPEXTENSION_H
#define AUDIOSTREAMPLAYERVOIPEXTENSION_H

#include "godot_cpp/classes/audio_effect_capture.hpp"
#include "godot_cpp/classes/audio_stream_generator_playback.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/templates/local_vector.hpp"

#include "DebugInfoWindow.h"

struct OpusEncoder;
struct OpusDecoder;

class AudioStreamPlayerVoipExtension : public godot::Node
{
    GDCLASS( AudioStreamPlayerVoipExtension, godot::Node )

protected:
    static void _bind_methods();

private:
    // NOTE: opus has pretty strict requirements on the mix rate: 48000 or 24000 or 16000 or 12000
    // or 8000
    int mix_rate = 24000;
    float buffer_length = 0.1f;
    int audio_package_duration_ms = 40;

    OpusDecoder *_opus_decoder = nullptr;
    OpusEncoder *_opus_encoder = nullptr;
    godot::Ref<godot::AudioStreamGeneratorPlayback> _audioStreamGeneratorPlayback;
    godot::Ref<godot::AudioEffectCapture> _audioEffectCapture;
    godot::PackedFloat32Array _sampleBuffer;
    godot::PackedByteArray _encodeBuffer;
    unsigned char _runningPacketNumber;
    float _current_loudness;

    int _num_out_of_order = 0;
    DebugInfoWindow* _debugInfoWindow;
public:
    void _process( double delta ) override;
    void _enter_tree() override;
    void _exit_tree() override;
    void _ready() override;

    void transfer_opus_packet_rpc( unsigned char packetNumber, const godot::PackedByteArray &packet );
    void initialize();

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

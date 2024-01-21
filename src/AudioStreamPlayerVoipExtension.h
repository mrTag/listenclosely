#ifndef AUDIOSTREAMPLAYERVOIPEXTENSION_H
#define AUDIOSTREAMPLAYERVOIPEXTENSION_H

#include "godot_cpp/classes/audio_effect_capture.hpp"
#include "godot_cpp/classes/audio_stream_generator_playback.hpp"
#include "godot_cpp/classes/node.hpp"

struct OpusEncoder;
struct OpusDecoder;

class AudioStreamPlayerVoipExtension : public godot::Node
{
    GDCLASS( AudioStreamPlayerVoipExtension, godot::Node )

protected:
    static void _bind_methods();

private:
    OpusDecoder *_opus_decoder = nullptr;
    OpusEncoder *_opus_encoder = nullptr;
    godot::Ref<godot::AudioStreamGeneratorPlayback> _audioStreamGeneratorPlayback;
    godot::Ref<godot::AudioEffectCapture> _audioEffectCapture;
    godot::PackedFloat32Array _sampleBuffer;
    godot::PackedByteArray _encodeBuffer;

public:
    void _process( double delta ) override;
    void _enter_tree() override;
    void _exit_tree() override;
    void _ready() override;

    void transferOpusPacketRPC( godot::PackedByteArray packet );
};

#endif // AUDIOSTREAMPLAYERVOIPEXTENSION_H

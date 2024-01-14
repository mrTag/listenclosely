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
    OpusDecoder *_opus_decoder;
    OpusEncoder *_opus_encoder;
    godot::Ref<godot::AudioStreamGeneratorPlayback> _audioStreamGeneratorPlayback;
    godot::Ref<godot::AudioEffectCapture> _audioEffectCapture;

public:
    void _process( double delta ) override;
    void _ready() override;
};

#endif // AUDIOSTREAMPLAYERVOIPEXTENSION_H

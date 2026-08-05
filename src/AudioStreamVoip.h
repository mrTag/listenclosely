#ifndef AUDIOSTREAMVOIP_H
#define AUDIOSTREAMVOIP_H

#include "RingBuffer.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/core/defs.hpp>

#include <atomic>

class AudioStreamVoipPlayback : public godot::AudioStreamPlayback
{
    GDCLASS( AudioStreamVoipPlayback, godot::AudioStreamPlayback )

protected:
    static void _bind_methods() {}

    bool active = false;
    uint64_t mixed = 0;
    bool fill_with_zero = true;
    godot::RingBuffer<godot::AudioFrame> ring_buffer;

    // _start/_stop run on any thread; only _mix may move the read position, so it
    // performs the reset.
    std::atomic<bool> reset_requested{ false };

    // Latency trim. Producer (mic) and consumer both run at realtime, so a backlog
    // never drains on its own and becomes permanent standing latency. Correction is
    // gradual: a few frames per block is a sub-percent, inaudible time compression,
    // where dropping a whole excess at once would click.
    //
    // Only enabled for local self-monitor playbacks; on the receiving path the jitter
    // buffer already governs depth.
    int target_latency_frames = 0;
    int catch_up_hysteresis = 0;
    int catch_up_permille = 10;   // max frames dropped per _mix, in ‰ of the block
    int hard_trim_excess = 0;     // 0 = never; else drop-all + fade escape hatch
    bool catching_up = false;
    uint32_t num_catch_up_frames = 0;
    uint32_t num_hard_trims = 0;

    // Blends from last_frame into the new samples after a hard trim. -1 = inactive.
    godot::AudioFrame last_frame = { 0, 0 };
    int resume_fade_pos = -1;
    static constexpr int RESUME_FADE_LEN = 64;

    void trim_latency( int p_frames );

public:
    AudioStreamVoipPlayback();
    // Setup only: reallocates, so call before the playback is shared with the
    // producer/consumer threads.
    void set_buffer_size( int p_frames );
    void push_frames( const godot::AudioFrame *p_frames, int p_count );
    bool push_buffer( const godot::PackedVector2Array& p_buffer );
    int get_free_buffer_size() const;
    int get_available_buffer_size() const;
    void set_fill_with_zero(bool fwz) { fill_with_zero = fwz; }
    bool get_fill_with_zero() const { return fill_with_zero; }

    // Standing occupancy the ring may keep; 0 disables trimming. Must stay well
    // below the ring capacity.
    void set_target_latency_frames( int p_frames );
    int get_target_latency_frames() const { return target_latency_frames; }
    void set_catch_up_permille( int p_permille ) { catch_up_permille = godot::MAX( 1, p_permille ); }
    int get_catch_up_permille() const { return catch_up_permille; }
    void set_hard_trim_excess( int p_frames ) { hard_trim_excess = godot::MAX( 0, p_frames ); }
    int get_hard_trim_excess() const { return hard_trim_excess; }
    uint32_t get_num_catch_up_frames() const { return num_catch_up_frames; }
    uint32_t get_num_hard_trims() const { return num_hard_trims; }

    void _start( double p_from_pos ) override;
    void _stop() override;
    bool _is_playing() const override;
    double _get_playback_position() const override;
    int32_t _mix( godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames ) override;

};

class AudioStreamVoip : public godot::AudioStream
{
    GDCLASS( AudioStreamVoip, godot::AudioStream )

protected:
    static void _bind_methods() {}

public:
    godot::Ref<godot::AudioStreamPlayback> _instantiate_playback() const override;
    godot::String _get_stream_name() const override { return "AudioStreamVoip"; }
};

#endif //AUDIOSTREAMVOIP_H

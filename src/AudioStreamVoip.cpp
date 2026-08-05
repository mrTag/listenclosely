#include "AudioStreamVoip.h"

#include <cstring>

// --- AudioStreamVoipPlayback ---

AudioStreamVoipPlayback::AudioStreamVoipPlayback()
{
    ring_buffer.resize( godot::nearest_shift( 1024 ) );
    ring_buffer.clear();
}

void AudioStreamVoipPlayback::set_buffer_size( int p_frames )
{
    // nearest_shift() rounds an exact power of two up an octave, so the ring can be
    // up to 2x the request. That headroom absorbs bursts; latency is bounded by
    // set_target_latency_frames().
    ring_buffer.resize( godot::nearest_shift( p_frames ) );
    ring_buffer.clear();
}

void AudioStreamVoipPlayback::set_target_latency_frames( int p_frames )
{
    target_latency_frames = godot::MAX( 0, p_frames );
    // The ring swings by up to one producer chunk between consumer pulls.
    catch_up_hysteresis = target_latency_frames / 4;
    // Escape hatch for a backlog too large to catch up on gradually; 0 disables it.
    hard_trim_excess = target_latency_frames * 2;
}

void AudioStreamVoipPlayback::push_frames( const godot::AudioFrame *p_frames, int p_count )
{
    int to_push = godot::MIN(ring_buffer.space_left(), p_count);
    if (to_push > 0)
        ring_buffer.write( p_frames, to_push );
}

bool AudioStreamVoipPlayback::push_buffer( const godot::PackedVector2Array &p_buffer )
{
    for (const godot::Vector2 v : p_buffer)
    {
        if (ring_buffer.space_left() < 1)
            return false;
        ring_buffer.write( {v.x, v.y } );
    }
    return true;
}

int AudioStreamVoipPlayback::get_free_buffer_size() const
{
    return ring_buffer.space_left();
}

int AudioStreamVoipPlayback::get_available_buffer_size() const
{
    return ring_buffer.data_left();
}

void AudioStreamVoipPlayback::_start( double p_from_pos )
{
    reset_requested.store( true, std::memory_order_release );
    active = true;
}

void AudioStreamVoipPlayback::_stop()
{
    active = false;
    reset_requested.store( true, std::memory_order_release );
}

bool AudioStreamVoipPlayback::_is_playing() const
{
    return active;
}

double AudioStreamVoipPlayback::_get_playback_position() const
{
    return mixed;
}

void AudioStreamVoipPlayback::trim_latency( int p_frames )
{
    if (target_latency_frames <= 0)
        return;

    int excess = ring_buffer.data_left() - target_latency_frames;

    if (hard_trim_excess > 0 && excess > hard_trim_excess)
    {
        num_catch_up_frames += ring_buffer.advance_read( excess );
        num_hard_trims++;
        catching_up = false;
        resume_fade_pos = 0;
        return;
    }

    if (excess > catch_up_hysteresis)
        catching_up = true;
    else if (excess <= 0)
        catching_up = false;

    if (catching_up && excess > 0)
    {
        int max_drop = godot::MAX( 1, p_frames * catch_up_permille / 1000 );
        num_catch_up_frames += ring_buffer.advance_read( godot::MIN( excess, max_drop ) );
    }
}

int32_t AudioStreamVoipPlayback::_mix( godot::AudioFrame *p_buffer, float p_rate_scale, int32_t p_frames )
{
    if (reset_requested.exchange( false, std::memory_order_acq_rel ))
    {
        ring_buffer.clear();
        catching_up = false;
        resume_fade_pos = -1;
    }
    trim_latency( p_frames );

    int available = ring_buffer.data_left();
    int to_mix = godot::MIN(available, p_frames);

    if ( to_mix > 0 )
    {
        ring_buffer.read(  p_buffer, to_mix );

        if (resume_fade_pos >= 0)
        {
            int fade_end = godot::MIN( to_mix, RESUME_FADE_LEN - resume_fade_pos );
            for (int i = 0; i < fade_end; i++)
            {
                float t = (float)(resume_fade_pos + i) / (float)RESUME_FADE_LEN;
                p_buffer[i].left = last_frame.left * (1.0f - t) + p_buffer[i].left * t;
                p_buffer[i].right = last_frame.right * (1.0f - t) + p_buffer[i].right * t;
            }
            resume_fade_pos += fade_end;
            if (resume_fade_pos >= RESUME_FADE_LEN)
                resume_fade_pos = -1;
        }

        last_frame = p_buffer[to_mix - 1];
    }
    mixed += to_mix;

    if (fill_with_zero)
    {
        for (int i = to_mix; i < p_frames; i++)
        {
            p_buffer[i] = {0,0};
        }

        return p_frames;
    }
    return to_mix;
}

// --- AudioStreamVoip ---

godot::Ref<godot::AudioStreamPlayback> AudioStreamVoip::_instantiate_playback() const
{
    godot::Ref<AudioStreamVoipPlayback> playback;
    playback.instantiate();
    return playback;
}

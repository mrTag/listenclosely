#include "DebugInfoWindow.h"

#include <godot_cpp/classes/h_box_container.hpp>

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/time.hpp>

void DebugInfoWindow::updateGraphs(  )
{
    uint64_t graphEndTime = godot::Time::get_singleton()->get_ticks_msec();
    uint64_t graphStartTime = graphEndTime - static_cast<uint64_t>(_graphTimeDuration * 1000.0f);


    for(auto& graphKV : _graphData)
    {
        float currentMax = graphKV.value.MaxValue;
        float currentMin = graphKV.value.MinValue;

        for ( int i = graphKV.value.FloatValues.size()-1; i >= 0; --i )
        {
            const auto& floatValue = graphKV.value.FloatValues[i];
            if(floatValue.Time > graphStartTime)
            {
                currentMax = godot::Math::max( currentMax, floatValue.Value );
                currentMin = godot::Math::min( currentMin, floatValue.Value );
            }
            else
            {
                // remove the values that are not valid inside the graph anymore...
                graphKV.value.FloatValues.remove_at( i );
            }
        }

        graphKV.value.MaxValue = currentMax;
        graphKV.value.MinValue = currentMin;

        godot::Vector2 graphSize = graphKV.value.DrawArea->get_size();
        float valueRangeInPixel = graphSize.y / (currentMax - currentMin);
        float timeRangeInPixel = graphSize.x / (_graphTimeDuration * 1000.0f);

        graphKV.value.DrawArea->Polyline.clear();
        for (const auto& timedValue : graphKV.value.FloatValues )
        {
            graphKV.value.DrawArea->Polyline.append( godot::Vector2(
                (timedValue.Time - graphStartTime) * timeRangeInPixel,
                (timedValue.Value - currentMin) * valueRangeInPixel
            ) );
        }

        graphKV.value.DrawArea->queue_redraw();
    }
}

void DebugInfoWindow::Initialize(const godot::String& debug_title, float graph_time_duration )
{
    set_process( true );
    set_process_mode( PROCESS_MODE_ALWAYS );
    set_title( debug_title );
    set_initial_position( WINDOW_INITIAL_POSITION_CENTER_OTHER_SCREEN );
    set_size( godot::Vector2i(500,500) );
    _graphTimeDuration = graph_time_duration;

    godot::ColorRect* bg = new godot::ColorRect();
    bg->set_anchors_preset( godot::Control::PRESET_FULL_RECT );
    bg->set_anchor( godot::SIDE_RIGHT, 1.0f );
    bg->set_anchor( godot::SIDE_BOTTOM, 1.0f );
    bg->set_h_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
    bg->set_v_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
    bg->set_color( godot::Color(0.2f, 0.2f, 0.2f, 1) );
    add_child( bg );

    _content = new godot::VBoxContainer();
    _content->set_anchors_preset( godot::Control::PRESET_FULL_RECT );
    _content->set_anchor( godot::SIDE_RIGHT, 1.0f );
    _content->set_anchor( godot::SIDE_BOTTOM, 1.0f );
    _content->set_h_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
    _content->set_v_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
    add_child( _content );

    _hFlowContent = new godot::HFlowContainer();
    _hFlowContent->add_theme_constant_override( "h_separation", 20 );
    _content->add_child( _hFlowContent );


}

void DebugInfoWindow::SetString( const godot::String &id, const godot::String &stringValue )
{
    auto stringDataIter = _stringData.find( id );
    if(stringDataIter != _stringData.end())
    {
        stringDataIter->value->set_text( stringValue );
    }
    else
    {
        godot::HBoxContainer* stringParent = new godot::HBoxContainer();
        stringParent->set_custom_minimum_size( godot::Vector2(225.0f, 0) );

        godot::Label* label = new godot::Label();
        label->set_text( id );
        stringParent->add_child( label );

        godot::Label* valueLabel = new godot::Label();
        valueLabel->set_h_size_flags( godot::Control::SizeFlags::SIZE_EXPAND | godot::Control::SizeFlags::SIZE_FILL );
        valueLabel->set_horizontal_alignment( godot::HORIZONTAL_ALIGNMENT_RIGHT );
        valueLabel->set_text( stringValue );
        stringParent->add_child( valueLabel );

        _hFlowContent->add_child( stringParent );
        _stringData.insert( id, valueLabel );
    }
}

void DebugInfoWindow::SetPercentage( const godot::String &id, float percentage )
{
    auto percentageDataIter = _percentageData.find( id );
    if(percentageDataIter != _percentageData.end())
    {
        percentageDataIter->value->set_value( percentage );
    }
    else
    {
        godot::ProgressBar* progressBar = new godot::ProgressBar();
        progressBar->set_custom_minimum_size( godot::Vector2(255, 0) );
        progressBar->set_v_size_flags( godot::Control::SizeFlags::SIZE_FILL );
        progressBar->set_show_percentage( false );

        godot::Label* label = new godot::Label();
        label->set_text( id );
        progressBar->add_child( label );

        _hFlowContent->add_child( progressBar );
        _percentageData.insert( id, progressBar );
    }
}

void DebugInfoWindow::AddToGraph( const godot::String &id, float value )
{
    auto graphIter = _graphData.find( id );
    if(graphIter != _graphData.end())
    {
        graphIter->value.FloatValues.push_back( {godot::Time::get_singleton()->get_ticks_msec(), value} );
    }
    else
    {
        PolyLineControl* graphDrawArea = new PolyLineControl();
        graphDrawArea->set_custom_minimum_size( godot::Vector2(0, 100) );

        godot::ColorRect* bg = new godot::ColorRect();
        bg->set_draw_behind_parent( true );
        bg->set_anchors_preset( godot::Control::PRESET_FULL_RECT );
        bg->set_anchor( godot::SIDE_RIGHT, 1.0f );
        bg->set_anchor( godot::SIDE_BOTTOM, 1.0f );
        bg->set_h_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
        bg->set_v_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
        bg->set_color( godot::Color(1.0f, 1.0f, 1.0f, 0.1f) );
        graphDrawArea->add_child( bg );

        godot::Label* label = new godot::Label();
        label->set_anchors_preset( godot::Control::PRESET_TOP_WIDE );
        label->set_anchor( godot::SIDE_RIGHT, 1.0 );
        label->set_offset( godot::SIDE_BOTTOM, 23.0 );
        label->set_h_grow_direction( godot::Control::GROW_DIRECTION_BOTH );
        label->set_horizontal_alignment( godot::HORIZONTAL_ALIGNMENT_CENTER );
        label->set_text( id );
        graphDrawArea->add_child( label );

        _content->add_child( graphDrawArea );
        _graphData.insert( id, {
            {{godot::Time::get_singleton()->get_ticks_msec(), value}},
            godot::Math::min( value, 0.0f ),
            godot::Math::max(value, 0.0f),
            graphDrawArea
        } );
    }
}
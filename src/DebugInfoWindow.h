#ifndef DEBUGINFOLAYER_H
#define DEBUGINFOLAYER_H


#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/h_flow_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// super simple helper class to just draw a polyline in the _draw...
class PolyLineControl : public godot::Control
{
    GDCLASS( PolyLineControl, godot::Control )


public:
    static void _bind_methods()
    {
        // no need to do anything from gdscript for the moment (only c++ for now...)
    }

    void _process( double delta ) override
    {
        queue_redraw();
    }
    void _ready() override
    {
        set_process( true );
    }

    godot::PackedVector2Array Polyline;
    void _draw() override
    {
        if(Polyline.size() > 1)
            draw_polyline( Polyline, godot::Color(1,1,1,1) );
    }
};


class DebugInfoWindow : public godot::Window
{
    GDCLASS( DebugInfoWindow, godot::Window )

public:
    static void _bind_methods()
    {
        // no need to do anything from gdscript for the moment (only c++ for now...)
    }

    void updateGraphs( );


    float _graphTimeDuration;
    godot::VBoxContainer* _content;
    godot::HFlowContainer* _hFlowContent;

    godot::HashMap<godot::String, godot::Label*> _stringData;
    godot::HashMap<godot::String, godot::ProgressBar*> _percentageData;

    struct TimedFloatValue
    {
        uint64_t Time;
        float Value;
    };
    struct GraphData
    {
        godot::LocalVector<TimedFloatValue> FloatValues;
        float MinValue;
        float MaxValue;
        PolyLineControl* DrawArea;
    };
    godot::HashMap<godot::String, GraphData> _graphData;

public:
    void Initialize(const godot::String& title, float graph_time_duration = 2);

    void SetString(const godot::String& id, const godot::String& stringValue);
    void SetPercentage(const godot::String& id, float percentage);
    void AddToGraph(const godot::String& id, float value);
};



#endif //DEBUGINFOLAYER_H

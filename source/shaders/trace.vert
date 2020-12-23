attribute float a_dataTime;
attribute float a_dataY;

varying float v_index;

uniform vec3 u_color;
varying vec4 v_color;

uniform vec2 u_pan;
uniform vec2 u_scale;
uniform vec2 u_magnify;

uniform float u_offset;
uniform float u_scaleTrace;

uniform float u_time;
uniform float u_windowSize;

uniform float u_traceSelected;



//const float scrollBarPos = 2.0 * mod(u_time,u_windowSize)/u_windowSize - 1.0; // From -1 to 1
//const float windowLeftTime = u_time - mod(u_time,u_windowSize);

void main(void)
{

    float scrollBarPos = 2.0 * mod(u_time,u_windowSize)/u_windowSize - 1.0;
    float windowLeftTime = u_time - mod(u_time,u_windowSize);

    vec2 pos;
    v_index = 1.0;
    v_color = vec4(u_color, 1.0);

    pos.y = a_dataY;
    // map to between -1 and scrollBarPos
    pos.x = scrollBarPos - 2.0 * (u_time - a_dataTime)/u_windowSize;

    // This is time that should be moved to the right of the moving bar
    if ((a_dataTime < windowLeftTime) && (a_dataTime > (u_time - u_windowSize))) {
        pos.x += 2.0;
        v_index = 2.0;
    }

    // This is outside of window
    if (a_dataTime < (u_time - u_windowSize))
        v_index = 3.0;

    vec2 position_tr = u_scale * (u_magnify * pos * vec2(1.0, u_scaleTrace) + vec2(0.0, u_offset) + u_pan);

    if (u_traceSelected == 1.0)
        v_color.rgb = mix(v_color.rgb, vec3(1.0), 0.8);
    else if (u_traceSelected == -1.0)
        v_color = mix(v_color, vec4(vec3(0.0), 1), 0.8);

    gl_Position = vec4(position_tr, 0.0, 1.0);
//    gl_Position = vec4(pos, 0.0, 1.0);
}

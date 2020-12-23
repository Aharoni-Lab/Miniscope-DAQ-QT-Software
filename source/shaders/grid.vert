attribute highp vec2 a_position;
attribute highp float a_index;
varying highp float v_index;

uniform highp vec3 u_color;
varying highp vec3 v_color;

uniform highp vec2 u_pan;
uniform highp vec2 u_scale;
uniform highp vec2 u_magnify;

uniform highp float u_spacing;

void main(void) {
    vec2 position_tr = u_scale * (u_magnify * a_position + u_pan);
    gl_Position = vec4(position_tr, 0.0, 1.0);

    v_color = u_color;
    if (fract(a_index) > 0.1)
        v_color = v_color * vec3(0.4);
    v_index = a_index / u_spacing;
}

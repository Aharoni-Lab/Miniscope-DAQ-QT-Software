attribute vec2 a_position;

uniform vec3 u_color;
varying vec3 v_color;

uniform vec2 u_pan;
uniform vec2 u_scale;
uniform vec2 u_magnify;

uniform float u_time;
uniform float u_windowSize;

void main() {
    float scrollBarPos = 2 * mod(u_time,u_windowSize)/u_windowSize - 1; // From -1 to 1

    vec2 pos = a_position;
    pos.x = scrollBarPos;
    pos.x = u_scale.x * (u_magnify.x * pos.x + u_pan.x);
    gl_Position = vec4(pos, 0.0, 1.0);
    v_color = u_color;
}

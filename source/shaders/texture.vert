attribute vec3 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;
void main (void) {
    // Pass tex coords
    v_texcoord = a_texcoord;
    // Calculate position
    gl_Position = vec4(a_position, 1.0);
}

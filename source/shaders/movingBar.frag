varying vec3 v_color;
varying float v_index;

void main() {
    gl_FragColor = vec4(v_color, 1.0);
}

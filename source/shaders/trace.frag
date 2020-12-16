varying vec4 v_color;
varying float v_index;

void main() {
    gl_FragColor = v_color;
    if (((fract(v_index) > .00001) && (fract(v_index) < .99999)) || v_index > 2.0)
        gl_FragColor.a = 0.0;
}

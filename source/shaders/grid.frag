varying highp vec3 v_color;
varying highp float v_index;

void main() {
    gl_FragColor = vec4(v_color, 0.3);
    if ((fract(v_index) > .00001) && (fract(v_index) < .99999))
        gl_FragColor.a = 0.;

}

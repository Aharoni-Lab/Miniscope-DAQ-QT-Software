uniform sampler2D texture;
varying vec2 v_texcoord;

uniform float u_displayType;
uniform float u_occMax;

void main() {
    gl_FragColor = texture2D(texture, v_texcoord);
    if (u_displayType == 0.0f) {
        float temp = gl_FragColor.r;
        gl_FragColor.r = gl_FragColor.b;
        gl_FragColor.b = temp;
    }
    else if (u_displayType == 1.0f) {
        float val = gl_FragColor.r * 255.0 + gl_FragColor.g * 65536.0;
        val = val / u_occMax;
        gl_FragColor = vec4(vec3(val), 1.0);
    }
}

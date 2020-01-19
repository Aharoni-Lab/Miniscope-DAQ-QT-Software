uniform sampler2D texture;
varying highp vec2 v_texcoord;
uniform lowp float alpha;
uniform lowp float beta;
uniform lowp float showSaturation;
void main() {
   gl_FragColor = texture2D(texture, v_texcoord);
   if (gl_FragColor.b >= .99 && showSaturation == 1.0)
       gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);
   else
       gl_FragColor.rgb = (gl_FragColor.rgb - beta)/(alpha - beta);
   lowp float temp = gl_FragColor.r;
   gl_FragColor.r = gl_FragColor.b;
   gl_FragColor.b = temp;
}

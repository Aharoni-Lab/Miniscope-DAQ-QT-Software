uniform sampler2D texture;
varying highp vec2 v_texcoord;
uniform lowp float alpha;
uniform lowp float beta;
uniform lowp float showSaturation;
uniform lowp float lutMode;   // 0 grayscale, 1 green, 2 red, 3 inferno

// Polynomial approximation of the Inferno colormap (Matt Zucker,
// "Simple Analytic Approximations to the CIE colormaps").
vec3 inferno(float t) {
    vec3 c0 = vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    vec3 c1 = vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    vec3 c2 = vec3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    vec3 c3 = vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    vec3 c4 = vec3(77.162935699427, -33.40235894210092, -81.80730925738993);
    vec3 c5 = vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    vec3 c6 = vec3(25.13112622477341, -12.24266895238567, -23.07032500287172);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

void main() {
   gl_FragColor = texture2D(texture, v_texcoord);
   if (gl_FragColor.b >= .99 && showSaturation == 1.0) {
       gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);   // saturation highlight: red
   }
   else {
       gl_FragColor.rgb = (gl_FragColor.rgb - beta)/(alpha - beta);
       highp float t = clamp(gl_FragColor.g, 0.0, 1.0);
       if (lutMode == 1.0) {
           // GFP/GCaMP-like green (~530 nm): dark -> green -> light green/white.
           gl_FragColor = vec4(t * t, t, t * t * t, 1.0);
       }
       else if (lutMode == 2.0) {
           // Red indicators (jRGECO/RCaMP/tdTomato): dark -> red -> light red/white.
           gl_FragColor = vec4(t, t * t, t * t * t, 1.0);
       }
       else if (lutMode == 3.0) {
           // Perceptually-uniform intensity map.
           gl_FragColor = vec4(clamp(inferno(t), 0.0, 1.0), 1.0);
       }
       else {
           // Grayscale (BGR -> RGB swap for color behaviour cams).
           lowp float temp = gl_FragColor.r;
           gl_FragColor.r = gl_FragColor.b;
           gl_FragColor.b = temp;
       }
   }
}

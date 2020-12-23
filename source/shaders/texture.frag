uniform sampler2D u_texture1;
varying vec2 v_texcoord;
void main()
{
    gl_FragColor = texture2D(u_texture1, v_texcoord.st);
}

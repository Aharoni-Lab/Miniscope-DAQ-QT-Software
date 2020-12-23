attribute highp vec2 position;
attribute highp vec2 texcoord;
varying highp vec2 v_texcoord;

void main(void)
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_texcoord = texcoord;
}

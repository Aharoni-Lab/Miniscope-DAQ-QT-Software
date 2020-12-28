attribute vec3 a_position;

attribute float a_color;
varying float v_color;

attribute float a_index;
varying float v_index;

attribute float a_pValue;
varying float v_pValue;

varying float v_pastID;

uniform float u_pointSize;
void main(void)
{

    gl_PointSize = u_pointSize;

    v_color = a_color;
    v_index = a_index/1.0;
    v_pValue = a_pValue;
    v_pastID = a_position.z;
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
}

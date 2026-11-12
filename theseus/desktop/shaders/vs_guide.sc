$input  a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_color0
$output v_texcoord0, v_shape, v_edge, v_color0

// The guide's quads (ui_overlay.cpp), already in window pixels from the top
// left. u_guideScale.xy turns pixels into clip space.

#include <bgfx_shader.sh>

uniform vec4 u_guideScale;

void main()
{
	gl_Position = vec4(a_position.xy * u_guideScale.xy + vec2(-1.0, 1.0), 0.0, 1.0);
	v_texcoord0 = a_texcoord0;
	v_shape     = a_texcoord1;
	v_edge      = vec4(a_texcoord2, 0.0, 0.0);
	v_color0    = a_color0;
}

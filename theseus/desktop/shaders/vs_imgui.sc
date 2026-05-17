$input  a_position, a_normal, a_color0, a_color1, a_texcoord0
$output v_color0, v_texcoord0

// Dear ImGui vertex shader, bgfx backend. ImGui's vertex data:
//   ImVec2 pos / ImVec2 uv / ImU32 col  (20 bytes per vertex).
// Our bgfx layout binds POSITION as a 2-float attribute (z auto-pads
// to 0 in the shader's a_position.z slot), COLOR0 as u8x4 normalized,
// TEXCOORD0 as 2-float. An orthographic projection uniform converts
// from screen-space (top-left origin) to clip-space per ImGui's
// expectation.

#include <bgfx_shader.sh>

uniform mat4 u_ImguiProj;

void main()
{
	gl_Position = mul(u_ImguiProj, vec4(a_position.xy, 0.0, 1.0));
	v_color0    = a_color0;
	v_texcoord0 = a_texcoord0;

	// Preserve unused shared attributes so shaderc keeps the entry-
	// point signature consistent with vs_ff / vs_blit / vs_simple.
	v_color0.rgb += a_normal * 0.0;
	v_color0     += a_color1 * 0.0;
}

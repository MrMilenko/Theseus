$input  a_position, a_normal, a_color0, a_color1, a_texcoord0
$output v_color0, v_texcoord0

// Fullscreen textured quad vertex shader for bgfx-side blits
// (boot anim, CRT post-process, media player frame display). Vertices
// are passed in NDC clip-space directly so no projection matrix is
// needed. Touches every shared attribute so shaderc keeps a slot map
// consistent with the FF emulator's vs_ff.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = vec4(a_position, 1.0);
	v_color0    = vec4(1.0, 1.0, 1.0, 1.0);
	v_texcoord0 = a_texcoord0;

	// No-op preserves so shaderc's HLSL DCE doesn't drop these
	// attributes from the entry-point signature and break the
	// shared varying.def slot map.
	v_color0.rgb += a_normal * 0.0;
	v_color0     += a_color0 * 0.0;
	v_color0     += a_color1 * 0.0;
}

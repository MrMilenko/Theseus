$input  a_position, a_normal, a_color0, a_color1, a_texcoord0
$output v_color0

// Minimal vertex-color passthrough shader. First .sc/.bin port that
// validated the bgfx shaderc cross-compile pipeline end to end.
// Touches every attribute declared in varying.def.sc with a no-op
// add so shaderc's HLSL transpiler keeps them in the entry-point
// signature. (Dropping any attribute from main makes its varying
// vanish from the function and downstream references compile-error.)

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0 = a_color0;
	// No-op references to keep shaderc from dropping these from the
	// transpiled HLSL function signature.
	v_color0.rgb += a_normal    * 0.0;
	v_color0.rg  += a_texcoord0 * 0.0;
	v_color0     += a_color1    * 0.0;
}

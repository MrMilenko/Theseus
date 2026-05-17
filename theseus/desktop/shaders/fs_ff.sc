$input v_color0, v_texcoord0

// Fixed-function emulator fragment shader. Port of the GLSL ubershader
// in d3d8_sdl.h (around line 1218). Texture-combiner flag set comes
// in as packed vec4s; same logic as the GL version.

#include <bgfx_shader.sh>

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);

// .x = fragColorOp (0=v_Color, 1=texture, 2=multiply, 3=add)
// .y = fragAlphaOp (0=v_Color.a, 1=tex.a, 2=multiply)
// .z = hasTex1     (0/1)
// .w = tex1AlphaOp (0=none, 1=multiply alpha by tex1.a)
uniform vec4 u_FfFragFlags1;
// .x = tex1ColorOp (0=none, 1=add tex1.rgb)
// .y = alphaRef    (>0 enables alpha test)
uniform vec4 u_FfFragFlags2;

void main()
{
	int colorOp    = int(u_FfFragFlags1.x);
	int alphaOp    = int(u_FfFragFlags1.y);
	int hasTex1    = int(u_FfFragFlags1.z);
	int t1AlphaOp  = int(u_FfFragFlags1.w);
	int t1ColorOp  = int(u_FfFragFlags2.x);
	float alphaRef = u_FfFragFlags2.y;

	vec4 tex = texture2D(s_tex0, v_texcoord0);

	vec3 color;
	if      (colorOp == 0) color = v_color0.rgb;
	else if (colorOp == 1) color = tex.rgb;
	else if (colorOp == 2) color = v_color0.rgb * tex.rgb;
	else                   color = v_color0.rgb + tex.rgb; // D3DTOP_ADD

	float alpha;
	if      (alphaOp == 0) alpha = v_color0.a;
	else if (alphaOp == 1) alpha = tex.a;
	else                   alpha = v_color0.a * tex.a;

	if (hasTex1 != 0) {
		vec4 tex1v = texture2D(s_tex1, v_texcoord0);
		if (t1AlphaOp == 1) alpha = alpha * tex1v.a;
		if (t1ColorOp == 1) color = clamp(color + tex1v.rgb, 0.0, 1.0);
	}

	vec4 finalColor = vec4(color, alpha);
	if (alphaRef > 0.0 && finalColor.a < alphaRef) discard;
	gl_FragColor = finalColor;
}

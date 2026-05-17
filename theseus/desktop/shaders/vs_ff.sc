$input  a_position, a_normal, a_color0, a_color1, a_texcoord0
$output v_color0, v_texcoord0

// Fixed-function emulator vertex shader. bgfx .sc port of the GLSL
// ubershader in d3d8_sdl.h (around line 1109). Branches on packed
// flag uniforms instead of using D3D8 fixed-function pipeline state.
//
// Uniform packing notes: bgfx prefers vec4/mat4 over scalar uniforms
// because backend uniform-buffer alignment requirements vary. Ints
// from the original GLSL are packed into u_FfFlags1 / u_FfFlags2
// and read back via .x / .y / .z / .w (each compared to floats since
// bgfx uniforms transport as floats).

#include <bgfx_shader.sh>

uniform mat4 u_FfWVP;
uniform mat4 u_FfWorldView;
uniform mat4 u_FfNormalInv;
uniform vec4 u_FfFalloffFront;
uniform vec4 u_FfFalloffDelta;
uniform vec4 u_FfTFactor;
uniform vec4 u_FfMatDiffuse;
// .x = vertexMode (0=3D, 1=XYZRHW)
// .y = colorSource (0=falloff, 1=diffuse, 2=tfactor, 3=white, 4=matDiffuse)
// .z = alphaSource (0=from_color, 1=tfactor, 2=diffuse, 3=opaque)
// .w = normalType (0=none, 1=float3; packed not in this port)
uniform vec4 u_FfFlags1;
// .x = envMapMode (0=off, 1=spherical)
// .y = vertexAlphaMul (0/1; multiply final alpha by diffuse alpha)
// .z = alphaMul (scalar)
// .w = unused
uniform vec4 u_FfFlags2;
// .xy = viewport width/height (for XYZRHW path)
uniform vec4 u_FfViewportSize;

// D3DFVF_NORMPACKED3: 11:11:10 normal in 4 bytes. Layout is normalized,
// scale to recover bytes.
vec3 unpack_normal_111110(vec4 bytes)
{
	int b0 = int(bytes.r * 255.0 + 0.5);
	int b1 = int(bytes.g * 255.0 + 0.5);
	int b2 = int(bytes.b * 255.0 + 0.5);
	int b3 = int(bytes.a * 255.0 + 0.5);
	int packed_x = (b0 | (b1 << 8))            & 0x7FF;
	int packed_y = ((b1 >> 3) | (b2 << 5))     & 0x7FF;
	int packed_z = ((b2 >> 6) | (b3 << 2))     & 0x3FF;
	if (packed_x >= 1024) packed_x -= 2048;
	if (packed_y >= 1024) packed_y -= 2048;
	if (packed_z >= 512)  packed_z -= 1024;
	return vec3(float(packed_x) / 1023.0,
	            float(packed_y) / 1023.0,
	            float(packed_z) / 511.0);
}

void main()
{
	int vertexMode  = int(u_FfFlags1.x);
	int colorSource = int(u_FfFlags1.y);
	int alphaSource = int(u_FfFlags1.z);
	int normalType  = int(u_FfFlags1.w);
	int envMapMode  = int(u_FfFlags2.x);
	int vertAlphaMul= int(u_FfFlags2.y);
	float alphaMul  = u_FfFlags2.z;

	// XYZRHW path not ported yet (shaderc HLSL transpile choke). HUD
	// overlays will need a dedicated variant.
	gl_Position = mul(u_FfWVP, vec4(a_position, 1.0));

	vec3 nrm = vec3(0.0, 0.0, 1.0);
	if (normalType == 1) nrm = vec3(a_normal);
	else if (normalType == 2) nrm = unpack_normal_111110(a_color1);

	// Falloff color (matches Xbox effect.vsh):
	// color = sideColor + (frontColor - sideColor) * abs(view dot normal)
	vec4 falloffColor = vec4(0.5, 0.5, 0.5, 1.0);
	if (colorSource == 0 && vertexMode == 0) {
		vec3 viewPos = mul(u_FfWorldView, vec4(a_position, 1.0)).xyz;
		vec3 viewNrm = mul(u_FfNormalInv, vec4(nrm, 0.0)).xyz;
		float plen = length(viewPos);
		float nlen = length(viewNrm);
		if (plen > 1e-6) viewPos = viewPos / plen;
		if (nlen > 1e-6) viewNrm = viewNrm / nlen;
		float viewDot = abs(dot(viewNrm, viewPos));
		falloffColor = clamp(u_FfFalloffFront + u_FfFalloffDelta * viewDot, 0.0, 1.0);
	}

	// D3DCOLOR is ARGB stored as BGRA in vertex data; swizzle back.
	vec4 a_color0_local = vec4(a_color0);
	vec4 diffuseRGBA = a_color0_local.bgra;

	vec4 color;
	if      (colorSource == 0) color = falloffColor;
	else if (colorSource == 1) color = diffuseRGBA;
	else if (colorSource == 2) color = u_FfTFactor;
	else if (colorSource == 3) color = vec4(1.0, 1.0, 1.0, 1.0);
	else                       color = u_FfMatDiffuse;

	float alpha = color.a;
	if      (alphaSource == 1) alpha = u_FfTFactor.a;
	else if (alphaSource == 2) alpha = diffuseRGBA.a;
	else if (alphaSource == 3) alpha = 1.0;
	alpha = alpha * alphaMul;
	if (vertAlphaMul != 0) alpha = alpha * diffuseRGBA.a;

	v_color0    = vec4(color.rgb, alpha);
	v_texcoord0 = vec2(a_texcoord0);

	// Spherical env map: UVs from view-space normal.
	if (envMapMode == 1 && vertexMode == 0) {
		vec3 vn = normalize(mul(u_FfNormalInv, vec4(nrm, 0.0)).xyz);
		v_texcoord0 = vn.xy * 0.5 + 0.5;
	}

	// No-op touch so shaderc keeps a_color1 in the entry-point signature.
	v_color0 += a_color1 * 0.0;
}

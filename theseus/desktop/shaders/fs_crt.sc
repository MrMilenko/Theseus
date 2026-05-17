$input v_color0, v_texcoord0

// CRT post-process. Reads the offscreen scene target via s_crtScene and
// writes the filtered result to the backbuffer. Parameters packed into
// three vec4s so the uniform set is one upload per frame.
//
// u_CrtParams1: x=scanlineIntensity, y=curvature, z=phosphorMask, w=vignette
// u_CrtParams2: x=bloom, y=flicker, z=colorBleed, w=brightness
// u_CrtParams3: x=time, y=resolution.x, z=resolution.y, w=unused

#include <bgfx_shader.sh>

SAMPLER2D(s_crtScene, 0);

uniform vec4 u_CrtParams1;
uniform vec4 u_CrtParams2;
uniform vec4 u_CrtParams3;

vec2 distort(vec2 uv, float k)
{
	vec2 cc = uv - 0.5;
	float r2 = dot(cc, cc);
	return uv + cc * r2 * k;
}

void main()
{
	float scanlineIntensity = u_CrtParams1.x;
	float curvature         = u_CrtParams1.y;
	float phosphorMask      = u_CrtParams1.z;
	float vignette          = u_CrtParams1.w;
	float bloom             = u_CrtParams2.x;
	float flicker           = u_CrtParams2.y;
	float colorBleed        = u_CrtParams2.z;
	float brightness        = u_CrtParams2.w;
	float time              = u_CrtParams3.x;
	vec2  resolution        = u_CrtParams3.yz;

	vec2 uv = v_texcoord0;

	if (curvature > 0.0) {
		uv = distort(uv, curvature * 0.3);
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
			gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
	}

	vec3 color;
	if (colorBleed > 0.0) {
		float offset = colorBleed / resolution.x;
		color.r = texture2D(s_crtScene, vec2(uv.x + offset, uv.y)).r;
		color.g = texture2D(s_crtScene, uv).g;
		color.b = texture2D(s_crtScene, vec2(uv.x - offset, uv.y)).b;
	} else {
		color = texture2D(s_crtScene, uv).rgb;
	}

	if (bloom > 0.0) {
		vec3 bloomColor = vec3(0.0, 0.0, 0.0);
		float ps = 1.5 / resolution.x;
		float pt = 1.5 / resolution.y;
		bloomColor += texture2D(s_crtScene, uv + vec2(-ps, -pt)).rgb;
		bloomColor += texture2D(s_crtScene, uv + vec2( ps, -pt)).rgb;
		bloomColor += texture2D(s_crtScene, uv + vec2(-ps,  pt)).rgb;
		bloomColor += texture2D(s_crtScene, uv + vec2( ps,  pt)).rgb;
		bloomColor *= 0.25;
		color = mix(color, max(color, bloomColor), bloom);
	}

	if (scanlineIntensity > 0.0) {
		float scanline = sin(uv.y * resolution.y * 3.14159) * 0.5 + 0.5;
		scanline = pow(scanline, 1.5);
		color *= mix(1.0, scanline, scanlineIntensity * 0.5);
	}

	if (phosphorMask > 0.0) {
		int px = int(gl_FragCoord.x) - 3 * (int(gl_FragCoord.x) / 3);
		vec3 mask = vec3(1.0, 1.0, 1.0);
		if      (px == 0) mask = vec3(1.0, 1.0 - phosphorMask * 0.5, 1.0 - phosphorMask * 0.5);
		else if (px == 1) mask = vec3(1.0 - phosphorMask * 0.5, 1.0, 1.0 - phosphorMask * 0.5);
		else              mask = vec3(1.0 - phosphorMask * 0.5, 1.0 - phosphorMask * 0.5, 1.0);
		color *= mask;
	}

	if (flicker > 0.0) {
		float flick = 1.0 - flicker * 0.03 * sin(time * 15.0);
		color *= flick;
	}

	if (vignette > 0.0) {
		vec2 vig = uv * (1.0 - uv);
		float v = pow(vig.x * vig.y * 16.0, vignette * 0.3);
		color *= v;
	}

	color *= brightness;

	gl_FragColor = vec4(color, 1.0);
}

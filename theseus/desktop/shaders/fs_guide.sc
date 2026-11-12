$input v_texcoord0, v_shape, v_edge, v_color0

// The guide's quads. v_shape is the point from the shape's middle and its
// half size, v_edge the corner radius and outline thickness (0 filled).
// A radius of -4 marks the plasma, a texture coordinate of 0 or more a glyph.

#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);
SAMPLER2D(s_plasma, 1);

uniform vec4 u_plasmaShift;
uniform vec4 u_plasmaThread;

void main()
{
	// Sampled up front so no texture read sits in a branch.
	float glyph = texture2D(s_atlas, v_texcoord0).r;
	vec4 t0 = texture2D(s_plasma, v_texcoord0);
	vec4 t1 = texture2D(s_plasma, v_texcoord0 * u_plasmaShift.z + u_plasmaShift.xy);

	if (v_edge.x < -3.5)
	{
		vec2 q = abs(v_shape.xy) - v_shape.zw + vec2_splat(v_edge.y);
		float d = length(max(q, vec2_splat(0.0))) + min(max(q.x, q.y), 0.0) - v_edge.y;
		float a0 = t0.a * dot(t0.rgb, vec3_splat(0.3333));
		float a1 = t1.a * dot(t1.rgb, vec3_splat(0.3333));
		float thread = pow(clamp(1.0 - abs(a0 - a1) * 3.0, 0.0, 1.0), 8.0);
		vec3 color = v_color0.rgb * (0.8 + 0.6 * a0 * a1) + u_plasmaThread.rgb * thread * 0.18;
		gl_FragColor = vec4(color, v_color0.a * clamp(0.5 - d, 0.0, 1.0) * (0.92 + 0.08 * thread));
		return;
	}

	float alpha;
	if (v_texcoord0.x >= 0.0)
	{
		alpha = glyph;
	}
	else
	{
		vec2 q = abs(v_shape.xy) - v_shape.zw + vec2_splat(v_edge.x);
		float d = length(max(q, vec2_splat(0.0))) + min(max(q.x, q.y), 0.0) - v_edge.x;
		alpha = clamp(0.5 - d, 0.0, 1.0);
		if (v_edge.y > 0.0)
			alpha *= clamp(d + v_edge.y + 0.5, 0.0, 1.0);
	}
	gl_FragColor = vec4(v_color0.rgb, v_color0.a * alpha);
}

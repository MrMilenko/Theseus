$input v_color0, v_texcoord0

// Companion to vs_blit. Pure texture sample, modulated by vertex
// color (which vs_blit pins to white, so this is a passthrough).
// Used for fullscreen-quad blits in bgfx mode (boot anim, etc).

#include <bgfx_shader.sh>

SAMPLER2D(s_blit, 0);

void main()
{
	gl_FragColor = texture2D(s_blit, v_texcoord0) * v_color0;
}

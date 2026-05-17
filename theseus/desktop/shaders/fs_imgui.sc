$input v_color0, v_texcoord0

// Dear ImGui fragment shader, bgfx backend. Modulates the font/glyph
// atlas (or any ImTextureID-bound texture) by the per-vertex tint. The
// font atlas pixels are stored as RGBA8 where only the A channel
// carries the glyph mask (RGB = white); for image draws the texture
// already carries full RGBA.

#include <bgfx_shader.sh>

SAMPLER2D(s_imgui, 0);

void main()
{
	gl_FragColor = texture2D(s_imgui, v_texcoord0) * v_color0;
}

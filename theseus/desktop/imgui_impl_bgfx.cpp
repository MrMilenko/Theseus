// imgui_impl_bgfx.cpp
//
// Mirrors imgui_impl_opengl3.cpp's surface for bgfx. Reference:
// theseus/desktop/imgui/imgui_impl_opengl3.cpp.
//
// Render state per ImGui_ImplOpenGL3_SetupRenderState (around line 414
// in imgui_impl_opengl3.cpp): alpha-blend SRC_ALPHA / 1-SRC_ALPHA RGB
// + ONE / 1-SRC_ALPHA alpha, no cull, no depth test, scissor on, fill.
// Orthographic projection in screen-space pixels -> clip-space using
// the same matrix the GL backend builds for ImGui::GetDrawData()'s
// DisplayPos / DisplaySize. Each ImDrawCmd is a setScissor + draw of
// (ElemCount) indices at (IdxOffset, VtxOffset).

#include "imgui_impl_bgfx.h"
#include "imgui.h"

#include <bgfx/bgfx.h>

#include <cstdio>
#include <cstring>

namespace {

struct BgfxImguiBackend {
	bgfx::ProgramHandle      program     = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle      uProj       = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle      sTexture    = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle      fontTex     = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout       layout;
	unsigned char            viewId      = 0;
	bool                     initialized = false;
};

static BgfxImguiBackend g_bd;

// Mirror sdl_main.cpp's theseus_bgfx_load_shader without exposing it
// via a header; same pattern.
static bgfx::ShaderHandle LoadShader(const char* name)
{
	const char* sub = "metal";
	switch (bgfx::getRendererType()) {
		case bgfx::RendererType::Direct3D11: sub = "dx11";  break;
		case bgfx::RendererType::Metal:      sub = "metal"; break;
		case bgfx::RendererType::Vulkan:     sub = "spirv"; break;
		case bgfx::RendererType::OpenGL:     sub = "glsl";  break;
		default: break;
	}
	char path[512];
	snprintf(path, sizeof(path), "Data/shaders/%s/%s.bin", sub, name);
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "imgui_impl_bgfx: shader not found: %s\n", path);
		return BGFX_INVALID_HANDLE;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	const bgfx::Memory* mem = bgfx::alloc((uint32_t)sz + 1);
	fread(mem->data, 1, sz, f);
	mem->data[sz] = '\0';
	fclose(f);
	bgfx::ShaderHandle sh = bgfx::createShader(mem);
	bgfx::setName(sh, name);
	return sh;
}

} // anonymous namespace

bool ImGui_ImplBgfx_CreateFontsTexture()
{
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels = nullptr;
	int w = 0, h = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	if (!pixels || w <= 0 || h <= 0) return false;

	if (bgfx::isValid(g_bd.fontTex)) bgfx::destroy(g_bd.fontTex);
	g_bd.fontTex = bgfx::createTexture2D(
		(uint16_t)w, (uint16_t)h, false, 1,
		bgfx::TextureFormat::RGBA8,
		BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
		bgfx::copy(pixels, (uint32_t)(w * h * 4)));
	io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_bd.fontTex.idx);
	return true;
}

void ImGui_ImplBgfx_DestroyFontsTexture()
{
	if (bgfx::isValid(g_bd.fontTex)) {
		bgfx::destroy(g_bd.fontTex);
		g_bd.fontTex = BGFX_INVALID_HANDLE;
	}
	ImGui::GetIO().Fonts->SetTexID(0);
}

bool ImGui_ImplBgfx_Init(unsigned char viewId)
{
	g_bd.viewId = viewId;

	bgfx::ShaderHandle vsh = LoadShader("vs_imgui");
	bgfx::ShaderHandle fsh = LoadShader("fs_imgui");
	if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) return false;

	g_bd.program  = bgfx::createProgram(vsh, fsh, true);
	if (!bgfx::isValid(g_bd.program)) {
		fprintf(stderr, "imgui_impl_bgfx: program link failed\n");
		return false;
	}

	g_bd.uProj    = bgfx::createUniform("u_ImguiProj", bgfx::UniformType::Mat4);
	g_bd.sTexture = bgfx::createUniform("s_imgui",     bgfx::UniformType::Sampler);

	// ImDrawVert: ImVec2 pos / ImVec2 uv / ImU32 col.
	// Match bytes via Position 2-float + TexCoord0 2-float +
	// Color0 u8x4 normalized. Note: the shared varying.def.sc has
	// a_position as vec3, but bgfx fills the missing z component
	// with 0 when the layout's Position has fewer components.
	g_bd.layout.begin()
		.add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
		.end();

	g_bd.initialized = true;

	// Build the font atlas eagerly so any "missing texture" errors
	// fire here rather than mid-frame on first NewFrame.
	ImGuiIO& io = ImGui::GetIO();
	io.BackendRendererName = "imgui_impl_bgfx";
	if (!ImGui_ImplBgfx_CreateFontsTexture()) {
		fprintf(stderr, "imgui_impl_bgfx: font texture creation failed\n");
	}
	return true;
}

void ImGui_ImplBgfx_Shutdown()
{
	ImGui_ImplBgfx_DestroyFontsTexture();
	if (bgfx::isValid(g_bd.sTexture)) bgfx::destroy(g_bd.sTexture);
	if (bgfx::isValid(g_bd.uProj))    bgfx::destroy(g_bd.uProj);
	if (bgfx::isValid(g_bd.program))  bgfx::destroy(g_bd.program);
	g_bd = BgfxImguiBackend();
}

void ImGui_ImplBgfx_NewFrame()
{
	if (!bgfx::isValid(g_bd.fontTex)) ImGui_ImplBgfx_CreateFontsTexture();
}

void ImGui_ImplBgfx_RenderDrawData(ImDrawData* draw_data)
{
	if (!g_bd.initialized || !draw_data) return;
	int fb_w = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_h = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_w <= 0 || fb_h <= 0) return;

	// Same ortho the GL backend builds: see imgui_impl_opengl3.cpp
	// around line 456. Row-major in CPU memory; bgfx interprets as
	// column-major just like GL with transpose=GL_FALSE does, so the
	// shader's mul(M, p) lands the same clip-space coords.
	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
	float ortho[16] = {
		2.f/(R-L),       0.f,             0.f,    0.f,
		0.f,             2.f/(T-B),       0.f,    0.f,
		0.f,             0.f,            -1.f,    0.f,
		(R+L)/(L-R),     (T+B)/(B-T),     0.f,    1.f,
	};
	bgfx::setUniform(g_bd.uProj, ortho);

	const uint64_t baseState =
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_BLEND_FUNC_SEPARATE(
			BGFX_STATE_BLEND_SRC_ALPHA,
			BGFX_STATE_BLEND_INV_SRC_ALPHA,
			BGFX_STATE_BLEND_ONE,
			BGFX_STATE_BLEND_INV_SRC_ALPHA);

	const ImVec2 clip_off   = draw_data->DisplayPos;
	const ImVec2 clip_scale = draw_data->FramebufferScale;

	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		uint32_t numVtx = (uint32_t)cmd_list->VtxBuffer.Size;
		uint32_t numIdx = (uint32_t)cmd_list->IdxBuffer.Size;
		if (numVtx == 0 || numIdx == 0) continue;

		// Transient buffers: one upload per cmd list, drawn via
		// per-cmd index sub-ranges below. Bail if the bgfx transient
		// arena is full -- pathological case, drop the frame's
		// remaining ImGui rather than crash.
		if (bgfx::getAvailTransientVertexBuffer(numVtx, g_bd.layout) < numVtx) break;
		if (bgfx::getAvailTransientIndexBuffer(numIdx) < numIdx) break;
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer  tib;
		bgfx::allocTransientVertexBuffer(&tvb, numVtx, g_bd.layout);
		bgfx::allocTransientIndexBuffer(&tib, numIdx);
		memcpy(tvb.data, cmd_list->VtxBuffer.Data, numVtx * sizeof(ImDrawVert));
		memcpy(tib.data, cmd_list->IdxBuffer.Data, numIdx * sizeof(ImDrawIdx));

		for (int ci = 0; ci < cmd_list->CmdBuffer.Size; ci++) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[ci];
			if (pcmd->UserCallback) {
				if (pcmd->UserCallback != ImDrawCallback_ResetRenderState)
					pcmd->UserCallback(cmd_list, pcmd);
				continue;
			}

			ImVec2 cmin = ImVec2((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
			                     (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
			ImVec2 cmax = ImVec2((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
			                     (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
			if (cmax.x <= cmin.x || cmax.y <= cmin.y) continue;
			if (cmin.x < 0) cmin.x = 0;
			if (cmin.y < 0) cmin.y = 0;
			if (cmax.x > (float)fb_w) cmax.x = (float)fb_w;
			if (cmax.y > (float)fb_h) cmax.y = (float)fb_h;

			bgfx::TextureHandle tex;
			tex.idx = (uint16_t)(uintptr_t)pcmd->TextureId;
			if (!bgfx::isValid(tex)) tex = g_bd.fontTex;

			bgfx::setState(baseState);
			bgfx::setScissor(
				(uint16_t)cmin.x,
				(uint16_t)cmin.y,
				(uint16_t)(cmax.x - cmin.x),
				(uint16_t)(cmax.y - cmin.y));
			bgfx::setTexture(0, g_bd.sTexture, tex);
			bgfx::setVertexBuffer(0, &tvb, pcmd->VtxOffset, numVtx - pcmd->VtxOffset);
			bgfx::setIndexBuffer(&tib, pcmd->IdxOffset, pcmd->ElemCount);
			bgfx::submit(g_bd.viewId, g_bd.program);
		}
	}
}

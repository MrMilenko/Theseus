// boot_anim.cpp: cold-boot animation playback. Brings up a fresh libmpv
// instance, points it at the boot video, renders fullscreen until the
// file ends or the user skips with Esc/Enter/Space. On exit the mpv
// context is torn down completely so the dashboard's own libmpv instance
// (CDVDPlayer / CMediaPlayer) can initialize cleanly afterward.
//
// The bundled video (xboxfs/C/UIX Configs/xbox_boot.mp4) is a 1080p60
// capture of the original Xbox 2001 boot animation. Source:
//   https://www.youtube.com/watch?v=oADANrDGhoQ
// Original boot animation (c) Microsoft / Pipeworks Software 2001.

#include "boot_anim.h"

#include <SDL.h>

// OpenGL 3.2 Core Profile -- match media_player.cpp's per-platform pattern.
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static void* GetProcAddr(void* /*ctx*/, const char* name)
{
	return (void*)SDL_GL_GetProcAddress(name);
}

bool BootAnim_PlayAndWait(SDL_Window* win, const char* path)
{
	if (!win || !path || !*path) return false;

	struct stat st;
	if (stat(path, &st) != 0) {
		fprintf(stderr, "[boot_anim] file not found: %s\n", path);
		return false;
	}

	mpv_handle* mpv = mpv_create();
	if (!mpv) return false;

	mpv_set_option_string(mpv, "vo",         "libmpv");
	mpv_set_option_string(mpv, "hwdec",      "no");      // software decode keeps macOS GL core happy
	mpv_set_option_string(mpv, "keep-open",  "no");      // exit-eof so END_FILE fires
	mpv_set_option_string(mpv, "video",      "yes");
	mpv_set_option_string(mpv, "audio",      "yes");
	mpv_set_option_string(mpv, "ao",         "coreaudio");
	mpv_set_option_string(mpv, "volume",     "100");
	mpv_set_option_string(mpv, "mute",       "no");
	mpv_set_option_string(mpv, "osc",        "no");
	mpv_set_option_string(mpv, "terminal",   "no");
	mpv_set_option_string(mpv, "msg-level",  "all=error");

	if (mpv_initialize(mpv) < 0) {
		mpv_destroy(mpv);
		return false;
	}

	mpv_opengl_init_params glInit = { GetProcAddr, nullptr };
	int advanced = 1;
	mpv_render_param createParams[] = {
		{ MPV_RENDER_PARAM_API_TYPE,           (void*)MPV_RENDER_API_TYPE_OPENGL },
		{ MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
		{ MPV_RENDER_PARAM_ADVANCED_CONTROL,   &advanced },
		{ MPV_RENDER_PARAM_INVALID,            nullptr }
	};

	mpv_render_context* gl = nullptr;
	if (mpv_render_context_create(&gl, mpv, createParams) < 0) {
		mpv_destroy(mpv);
		return false;
	}

	const char* loadCmd[] = { "loadfile", path, nullptr };
	if (mpv_command(mpv, loadCmd) < 0) {
		mpv_render_context_free(gl);
		mpv_destroy(mpv);
		return false;
	}

	// Fixed FBO sized to the source video. Avoids mid-stream FBO recreation
	// when mpv's reported dimensions become available -- that race was
	// crashing the GL render path on Apple Silicon.
	const int kFboW = 1920;
	const int kFboH = 1080;
	GLuint    fbo    = 0;
	GLuint    fboTex = 0;
	glGenTextures(1, &fboTex);
	glBindTexture(GL_TEXTURE_2D, fboTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFboW, kFboH, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, fboTex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	bool running    = true;
	bool sawAnyDraw = false;

	while (running) {
		// Pump SDL events: skip on Esc/Enter/Space, exit on window close.
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				running = false;
			} else if (e.type == SDL_KEYDOWN &&
			           (e.key.keysym.sym == SDLK_ESCAPE ||
			            e.key.keysym.sym == SDLK_RETURN ||
			            e.key.keysym.sym == SDLK_SPACE)) {
				running = false;
			}
		}

		// Pump mpv events; END_FILE = playback finished.
		while (true) {
			mpv_event* ev = mpv_wait_event(mpv, 0.0);
			if (!ev || ev->event_id == MPV_EVENT_NONE) break;
			if (ev->event_id == MPV_EVENT_END_FILE) {
				running = false;
				break;
			}
		}
		if (!running) break;

		// Reset GL state to defaults (mpv expects a clean slate).
		glUseProgram(0);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);
		glActiveTexture(GL_TEXTURE0);

		// Only render when mpv has a new frame -- matches the media_player
		// pattern. Calling render unconditionally leaves the FBO empty.
		uint64_t flags = mpv_render_context_update(gl);
		if (flags & MPV_RENDER_UPDATE_FRAME) {
			mpv_opengl_fbo fboParam = { (int)fbo, kFboW, kFboH, 0 };
			int flipY = 1;
			mpv_render_param drawParams[] = {
				{ MPV_RENDER_PARAM_OPENGL_FBO, &fboParam },
				{ MPV_RENDER_PARAM_FLIP_Y,     &flipY },
				{ MPV_RENDER_PARAM_INVALID,    nullptr }
			};
			mpv_render_context_render(gl, drawParams);
		}

		// Display the FBO texture via ImGui. Direct glBlitFramebuffer to
		// FBO 0 doesn't work on Apple Silicon because FBO 0 is logical-
		// sized while SDL_GL_GetDrawableSize returns native pixels --
		// the destination rect ends up clipped off-screen. ImGui handles
		// the coordinate space correctly through SDL2 + OpenGL.
		int winW = 0, winH = 0;
		SDL_GetWindowSize(win, &winW, &winH);
		if (winW <= 0 || winH <= 0) { winW = 1; winH = 1; }

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		int dW = 0, dH = 0;
		SDL_GL_GetDrawableSize(win, &dW, &dH);
		glViewport(0, 0, dW > 0 ? dW : winW, dH > 0 ? dH : winH);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		float srcAR = (float)kFboW / (float)kFboH;
		float dstAR = (float)winW / (float)winH;
		float drawW, drawH;
		if (dstAR > srcAR) { drawH = (float)winH; drawW = drawH * srcAR; }
		else               { drawW = (float)winW; drawH = drawW / srcAR; }
		float drawX = (winW - drawW) * 0.5f;
		float drawY = (winH - drawH) * 0.5f;

		// Flipped UVs so ImGui's top-down draw matches mpv's bottom-up FBO
		// content -- same convention as media_ui.cpp.
		ImGui::GetForegroundDrawList()->AddImage(
			(ImTextureID)(intptr_t)fboTex,
			ImVec2(drawX, drawY),
			ImVec2(drawX + drawW, drawY + drawH),
			ImVec2(0, 1), ImVec2(1, 0),
			IM_COL32_WHITE);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		sawAnyDraw = true;
		SDL_GL_SwapWindow(win);
	}

	mpv_render_context_free(gl);
	mpv_terminate_destroy(mpv);
	if (fboTex) glDeleteTextures(1, &fboTex);
	if (fbo)    glDeleteFramebuffers(1, &fbo);
	return sawAnyDraw;
}

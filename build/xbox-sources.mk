# Xbox source lists. Shared by build/Makefile and build/Makefile.xbox so the
# two never drift. Paths are relative to $(THESEUS_SRC).

SRCS_ENGINE = \
	engine/array.cpp \
	engine/debug.cpp \
	engine/dotfield_node.cpp \
	engine/locale_node.cpp \
	engine/math_node.cpp \
	engine/settings_nodes.cpp \
	engine/xap_compile.cpp \
	engine/minimp3_decode.cpp

# Shared sources compiled for BOTH Xbox and Desktop
SRCS_SHARED = \
	shared/activefile.cpp \
	shared/date_node.cpp \
	shared/file_ops.cpp \
	shared/file_util.cpp \
	shared/keyboard_node.cpp \
	shared/music_collection.cpp \
	shared/ntiosvc.cpp \
	shared/overlay_alert.cpp \
	shared/recovery_node.cpp \
	shared/savegame_grid.cpp \
	shared/screen.cpp \
	shared/settingsfile.cpp \
	shared/string_util.cpp \
	shared/timezone.cpp \
	shared/title_collection.cpp \
	shared/util.cpp \
	shared/frame_profiler.cpp \
	shared/xap_vm.cpp \
	shared/xbe.cpp

# Xbox-only shared files. Desktop provides replacements or stubs.
SRCS_SHARED_XBOX_ONLY = \
	shared/audio_system.cpp \
	shared/discord.cpp \
	shared/harddrive.cpp

SRCS_RENDER = \
	render/animation_nodes.cpp \
	render/asset_loader.cpp \
	render/autosmooth.cpp \
	render/camera.cpp \
	render/hud_node.cpp \
	render/materials.cpp \
	render/node_system.cpp \
	render/scene_groups.cpp \
	render/shape_render.cpp \
	render/starfield.cpp \
	render/text.cpp \
	render/tmap_system.cpp \
	render/widget_draw.cpp \
	render/widget_layer.cpp

SRCS_XBOX = \
	xbox/config.cpp \
	xbox/disc_management.cpp \
	xbox/dsound_manager.cpp \
	xbox/fps_widget.cpp \
	xbox/ftp_widget.cpp \
	xbox/iso_widget.cpp \
	xbox/input.cpp \
	xbox/live_accounts.cpp \
	xbox/main.cpp \
	xbox/memory_monitor.cpp \
	xbox/memutil.cpp \
	xbox/modchip.cpp \
	xbox/network.cpp \
	xbox/overlay.cpp \
	xbox/panic_screen.cpp \
	xbox/perf_widget.cpp \
	xbox/theseus_launcher.cpp \
	xbox/title_scanner.cpp \
	xbox/widget_config.cpp \
	xbox/xbox_live.cpp \
	xbox/xip_archive.cpp

SRCS_TOOLBOX = \
	toolbox/drive.cpp \
	toolbox/driveManager.cpp \
	toolbox/fileSystem.cpp \
	toolbox/ftpServer.cpp \
	toolbox/socketUtility.cpp \
	toolbox/stringUtility.cpp \
	toolbox/utils.cpp

ALL_SRCS = $(SRCS_ENGINE) $(SRCS_SHARED) $(SRCS_SHARED_XBOX_ONLY) $(SRCS_RENDER) $(SRCS_XBOX) $(SRCS_TOOLBOX)

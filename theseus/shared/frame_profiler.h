// frame_profiler.h: where the frame goes. One set of phase timers and
// counters for both builds. The perf widget, the fps widget and desktop
// --graphics-debug all read the same snapshot.
//
// Numbers are averaged over a 250ms window. Counters are split so scene
// draws never include the HUD that's measuring them.

#pragma once

enum FramePhase
{
	FP_ADVANCE,
	FP_EVENTS,        // desktop: SDL event pump
	FP_TICKS,         // desktop: media, milkdrop, xCloud
	FP_SCENE_SETUP,   // projection, lights, backdrop
	FP_SCENE_TREE,    // g_pObject->Render()
	FP_TREE_MATERIAL, //   inside the tree: appearance / material setup per shape
	FP_TREE_WORLD,    //   inside the tree: falloff shader constants per world change
	FP_TREE_DRAW,     //   inside the tree: geometry render per shape
	FP_DIP,           //   the device DrawIndexedPrimitive call alone, anywhere
	FP_SCENE_END,
	FP_WIDGETS,
	FP_WIDGET_TICK,   //   inside widgets: TickWidgets
	FP_UP_DRAW,       //   HUD / overlay DrawPrimitiveUP calls alone; the rest is text building
	FP_OVERLAY,
	FP_IMGUI,         // desktop
	FP_PRESENT,
	FP_GPU_WAIT,      // Xbox: CPU blocked on the GPU. Desktop: bgfx render wait, vsync included.
	FP_PHASE_COUNT
};

enum FrameCounter
{
	FC_DRAW_SCENE,    // CMesh / CMeshRef
	FC_DRAW_SUBSET,   // D3DX mesh draws (text nodes)
	FC_DRAW_SOLID,    // HUD rects
	FC_DRAW_TEXT,     // HUD font flushes
	FC_NODE_VISITS,
	FC_NODE_SKIPS,
	FC_UPDATE_WORLD,
	FC_STATE_CALLS,   // render state, texture stage state, texture
	FC_STATE_REPEATS, // ...that set the value already there
	FC_DISK_QUERIES,
	FC_TRIANGLES,     // primitives submitted through TheseusDrawIndexedPrimitive
	FC_INDICES,       // ...and the indices that implies
	FC_COUNTER_COUNT
};

struct FrameMeshTop
{
	TCHAR label[24];
	float tris;       // per frame
};

struct FrameSnapshot
{
	float fps;
	float frameMs;
	float phaseMs[FP_PHASE_COUNT];
	float count[FC_COUNTER_COUNT];   // per frame

	// Desktop, from bgfx. Zero on Xbox and when GPU timing is off.
	float gpuMs;
	float hostDraws;
	float transientVbKB;
	float transientIbKB;

	// XAP compile on Inline load.
	float lastLoadMs;
	float maxLoadMs;

	// The three shapes submitting the most triangles.
	FrameMeshTop topMesh[3];
};

extern int g_frameCount[FC_COUNTER_COUNT];
inline void Frame_Count(FrameCounter c, int n = 1) { g_frameCount[c] += n; }

void Frame_Begin();
void Frame_End();
void Frame_PhaseBegin(FramePhase p);
void Frame_PhaseEnd(FramePhase p);
void Frame_Read(FrameSnapshot* out);

// Anything showing GPU numbers asks every frame; it switches itself off
// the first frame nobody asks.
void Frame_RequestGpuTiming();

long long Frame_Now();
float Frame_Ms(long long ticks);
void Frame_NoteLoad(float ms);

// Which shapes carry the triangles. key identifies the shape, label names it.
void Frame_TallyMesh(const void* key, const TCHAR* label, int triangles);

#ifdef THESEUS_DESKTOP
void Frame_AddHostStats(float gpuMs, float gpuWaitMs, int draws, int transientVbBytes, int transientIbBytes);
#endif

struct FramePhaseScope
{
	FramePhase m_phase;
	FramePhaseScope(FramePhase p) : m_phase(p) { Frame_PhaseBegin(p); }
	~FramePhaseScope() { Frame_PhaseEnd(m_phase); }
};

// joystick.cpp: desktop CJoystick input. Maps SDL2 GameController
// buttons and keyboard keys to dashboard script callbacks (OnADown,
// OnMoveUp, etc.). Counterpart to xbox/input.cpp.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include <SDL.h>

#define REPEAT_DELAY 0.3f
#define REPEAT_INTERVAL 0.12f

#define KS_BEGIN 0x0001
#define KS_VERIFIED 0x0002
#define KS_END 0x0004

float g_xaxis, g_yaxis;

extern bool g_bWireframe;
extern void CycleLanguage();

bool g_bInputEnable = true;

// ============================================================================
// JoySnapshot -- unified controller + keyboard state
// ============================================================================

struct JoySnapshot {
    bool analogDown[8];   // A,B,X,Y,Black,White,LTrig,RTrig (after threshold)
    bool digitalDown[8];  // DPadU,D,L,R,Start,Back,LThumb,RThumb
    float thumbLX, thumbLY, thumbRX, thumbRY;  // after dead zone
};

class CJoystick : public CNode
{
    DECLARE_NODE(CJoystick, CNode)

public:
    CJoystick();
    ~CJoystick();

    bool m_bNeedsInit;

    bool m_isBound;
    float m_frequency;

    float m_hat;

    float m_xaxis;
    float m_yaxis;

    float m_xaxis2;
    float m_yaxis2;

    float m_a;
    float m_b;
    float m_x;
    float m_y;
    float m_black;
    float m_white;
    float m_left;
    float m_right;
    float m_leftThumb;
    float m_rightThumb;
    float m_start;
    float m_back;

    char *m_secretKey;
    size_t m_secretKeyLength;
    size_t m_secretKeyCurrentIndex;
    bool m_enableSecretKey;
    bool m_eatSecretKey;

    bool m_enableRemote;
    bool m_enableGamepad;

    void Advance(float nSeconds);
    bool OnSetProperty(const PRD *pprd, const void *pvValue);

    void Bind();

    void CheckDevice();

    void EnableGlobalInput(int bEnable);

    void CallFunction(const char *szFunc, bool bRemote = false);

    static SDL_GameController* c_controller;

    static CJoystick *c_pPreviousBoundJoystick;
    static CJoystick *c_pBoundJoystick;

    JoySnapshot m_prevState;
    XTIME m_nextRepeatTime;
    float m_repeatInterval;

    XTIME m_timeNextUpdate;

    uint32_t m_prevMediaKeys; // bitmask for edge detection

    void OnMoveUp();
    void OnMoveDown();
    void OnADown();
    void OnBDown();

    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()

protected:
    int CheckSecretKeySequence(char key);
    void ProcessSecretKeySequence(int flags);
    static void PollSDL(SDL_GameController* gc, JoySnapshot* out);
    void ProcessMediaKeys();
};

SDL_GameController* CJoystick::c_controller = NULL;

CJoystick *CJoystick::c_pBoundJoystick = NULL;
CJoystick *CJoystick::c_pPreviousBoundJoystick = NULL;

IMPLEMENT_NODE("Joystick", CJoystick, CNode)

START_NODE_PROPS(CJoystick, CNode)
NODE_PROP(pt_boolean, CJoystick, isBound)
NODE_PROP(pt_number, CJoystick, frequency)
NODE_PROP(pt_number, CJoystick, hat)
NODE_PROP(pt_number, CJoystick, xaxis)
NODE_PROP(pt_number, CJoystick, yaxis)
NODE_PROP(pt_number, CJoystick, xaxis2)
NODE_PROP(pt_number, CJoystick, yaxis2)
NODE_PROP(pt_number, CJoystick, a)
NODE_PROP(pt_number, CJoystick, b)
NODE_PROP(pt_number, CJoystick, x)
NODE_PROP(pt_number, CJoystick, y)
NODE_PROP(pt_number, CJoystick, black)
NODE_PROP(pt_number, CJoystick, white)
NODE_PROP(pt_number, CJoystick, left)
NODE_PROP(pt_number, CJoystick, right)
NODE_PROP(pt_number, CJoystick, leftThumb)
NODE_PROP(pt_number, CJoystick, rightThumb)
NODE_PROP(pt_number, CJoystick, start)
NODE_PROP(pt_number, CJoystick, back)
NODE_PROP(pt_string, CJoystick, secretKey)
NODE_PROP(pt_boolean, CJoystick, enableSecretKey)
NODE_PROP(pt_boolean, CJoystick, enableGamepad)
NODE_PROP(pt_boolean, CJoystick, enableRemote)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CJoystick
START_NODE_FUN(CJoystick, CNode)
NODE_FUN_VI(EnableGlobalInput)
NODE_FUN_VV(OnMoveUp)
NODE_FUN_VV(OnMoveDown)
NODE_FUN_VV(OnADown)
NODE_FUN_VV(OnBDown)
END_NODE_FUN()

CJoystick::CJoystick() : m_frequency(20.0f),
                         m_hat(-1.0f),
                         m_xaxis(0.0f),
                         m_yaxis(0.0f),
                         m_xaxis2(0.0f),
                         m_yaxis2(0.0f),
                         m_a(0.0f),
                         m_b(0.0f),
                         m_x(0.0f),
                         m_y(0.0f),
                         m_black(0.0f),
                         m_white(0.0f),
                         m_left(0.0f),
                         m_right(0.0f),
                         m_leftThumb(0.0f),
                         m_rightThumb(0.0f),
                         m_start(0.0f),
                         m_back(0.0f),
                         m_secretKey(NULL),
                         m_secretKeyLength(0),
                         m_secretKeyCurrentIndex(0),
                         m_enableSecretKey(false),
                         m_eatSecretKey(false),
                         m_enableRemote(true),
                         m_enableGamepad(true),
                         m_isBound(false),
                         m_prevMediaKeys(0)
{
    m_bNeedsInit = true;
    m_nextRepeatTime = 0.0f;
    m_repeatInterval = 0.0f;
    memset(&m_prevState, 0, sizeof(m_prevState));

    if (c_pBoundJoystick == NULL)
        Bind();
}

CJoystick::~CJoystick()
{
    if (c_pPreviousBoundJoystick == this)
        c_pPreviousBoundJoystick = NULL;

    if (c_pBoundJoystick == this)
        c_pBoundJoystick = c_pPreviousBoundJoystick;

    delete[] m_secretKey;
}

void CJoystick::CallFunction(const char *szFunc, bool bRemote /* = false */)
{
    if (c_pBoundJoystick == this)
    {
        if (bRemote && !m_enableRemote)
        {
            TRACE("Ignoring input from remote\n");
            return;
        }
        // On desktop, always accept gamepad/keyboard input (no IR remote)
        ::CallFunction(this, szFunc);
    }
}
void BindJoystick(CNode *pJoystickNode)
{
    if (pJoystickNode == NULL)
    {
        CJoystick::c_pBoundJoystick = NULL;
        CJoystick::c_pPreviousBoundJoystick = NULL;
        return;
    }

    if (!pJoystickNode->IsKindOf(NODE_CLASS(CJoystick)))
        return;

    ((CJoystick *)pJoystickNode)->Bind();
}
void CJoystick::OnMoveUp()
{
}

void CJoystick::OnMoveDown()
{
}

void CJoystick::OnADown()
{
}

void CJoystick::OnBDown()
{
}

void CJoystick::Bind()
{
    c_pPreviousBoundJoystick = c_pBoundJoystick;
    c_pBoundJoystick = this;

    if (!m_bNeedsInit)
    {
        CheckDevice();
        PollSDL(c_controller, &m_prevState);
    }
}

bool CJoystick::OnSetProperty(const PRD *pprd, const void *pvValue)
{
    if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_isBound))
    {
        if (*(bool *)pvValue)
        {
            Bind();
        }
        else if (c_pBoundJoystick == this)
        {
            c_pBoundJoystick = NULL;

            if (c_pPreviousBoundJoystick != NULL)
                c_pPreviousBoundJoystick->Bind();
        }
    }
    else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_secretKey))
    {
        // Delete the previous one if exists
        delete[] m_secretKey;
        m_secretKey = NULL;
        m_secretKeyLength = 0;

        // Cancel if we are in the middle of sequence checking
        if (m_secretKeyCurrentIndex)
            ProcessSecretKeySequence(KS_END);

        m_secretKeyCurrentIndex = 0;

        char *pszNewKey = *(char **)pvValue;
        if (pszNewKey)
        {
            m_secretKeyLength = strlen(pszNewKey);
            m_secretKey = new char[m_secretKeyLength + 1];
            strcpy(m_secretKey, pszNewKey);
            m_enableSecretKey = true;
        }
    }
    else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_enableSecretKey))
    {
        m_enableSecretKey = *(bool *)pvValue;

        if (!m_enableSecretKey)
        {
            // Cancel if we are in the middle of sequence checking
            if (m_secretKeyCurrentIndex && m_secretKeyCurrentIndex < m_secretKeyLength)
                ProcessSecretKeySequence(KS_END);
        }

        m_secretKeyCurrentIndex = 0;
    }
    else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_eatSecretKey))
    {
        m_eatSecretKey = *(bool *)pvValue;
        return false;
    }
    else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_enableGamepad))
    {
        m_enableGamepad = *(bool *)pvValue;
        return false;
    }
    else if ((intptr_t)pprd->pbOffset == MEMBER_OFFSET(m_enableRemote))
    {
        m_enableRemote = *(bool *)pvValue;
        return false;
    }

    return true;
}

// ============================================================================
// Controller Detection
// ============================================================================
void CJoystick::CheckDevice()
{
    // Check if current controller was disconnected
    if (c_controller && !SDL_GameControllerGetAttached(c_controller))
    {
        fprintf(stdout, "[Input] Controller disconnected\n");
        SDL_GameControllerClose(c_controller);
        c_controller = NULL;
    }

    // Try to find a controller if we don't have one
    if (!c_controller)
    {
        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            if (SDL_IsGameController(i))
            {
                c_controller = SDL_GameControllerOpen(i);
                if (c_controller)
                {
                    fprintf(stdout, "[Input] SDL controller opened: %s\n",
                            SDL_GameControllerName(c_controller));
                    break;
                }
            }
        }
    }
}

void CJoystick::EnableGlobalInput(int bEnable)
{
    g_bInputEnable = (bEnable != 0);
}

// ============================================================================
// Input Polling
// ============================================================================
void CJoystick::PollSDL(SDL_GameController* gc, JoySnapshot* out)
{
    memset(out, 0, sizeof(*out));

    // --- Physical controller ---
    if (gc)
    {
        // Face buttons -> analogDown[0..3] = A,B,X,Y
        out->analogDown[0] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A);
        out->analogDown[1] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B);
        out->analogDown[2] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
        out->analogDown[3] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y);
        // Black(RB), White(LB) -> analogDown[4,5]
        out->analogDown[4] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        out->analogDown[5] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        // Triggers -> analogDown[6,7]
        Sint16 lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        out->analogDown[6] = (lt > 8000);
        out->analogDown[7] = (rt > 8000);

        // Digital buttons -> digitalDown[0..7] = DPadU,D,L,R,Start,Back,LThumb,RThumb
        out->digitalDown[0] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP);
        out->digitalDown[1] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        out->digitalDown[2] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        out->digitalDown[3] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        out->digitalDown[4] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
        out->digitalDown[5] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK);
        out->digitalDown[6] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK);
        out->digitalDown[7] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK);

        // Thumbsticks (SDL Y is inverted vs Xbox)
        float lx = (float)SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = (float)(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY)) / 32767.0f;
        float rx = (float)SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = (float)(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY)) / 32767.0f;

        // Dead zone
        if (lx > -0.9f && lx < 0.9f) lx = 0.0f;
        if (ly > -0.9f && ly < 0.9f) ly = 0.0f;
        if (rx > -0.9f && rx < 0.9f) rx = 0.0f;
        if (ry > -0.9f && ry < 0.9f) ry = 0.0f;

        out->thumbLX = lx;
        out->thumbLY = ly;
        out->thumbRX = rx;
        out->thumbRY = ry;
    }

    // --- Keyboard overlay (OR on top) ---
    extern bool ImGui_WantsKeyboard();
    extern SDL_Window* g_pSDLWindow;
    extern bool g_bUseOnScreenKeyboard;
    extern void* g_pActiveKeyboard_void();  // CKeyboard* erased to dodge fwd-decl wrangling

    bool imguiActive = ImGui_WantsKeyboard();
    SDL_Window* focusedWin = SDL_GetKeyboardFocus();
    bool mainHasFocus = (focusedWin == g_pSDLWindow);
    // Suppress kb->gamepad mapping while an on-screen keyboard popup is taking
    // physical input -- otherwise typing 'q' fires LT, 'e' fires RT, backspace
    // fires both Backspace and B (back/cancel), etc.
    bool dashKbActive = (g_pActiveKeyboard_void() != NULL) && !g_bUseOnScreenKeyboard;

    // Escape always acts as B (back/cancel) when a dashboard keyboard popup is
    // up -- otherwise there's no keyboard-only way out, since Backspace is now
    // routed into the text buffer instead of the gamepad layer.
    if (mainHasFocus && !imguiActive && dashKbActive)
    {
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_ESCAPE]) out->analogDown[1] = true;  // B
    }

    if (mainHasFocus && !imguiActive && !dashKbActive)
    {
        const Uint8* keys = SDL_GetKeyboardState(NULL);

        // Analog: A,B,X,Y,Black,White,LTrig,RTrig
        if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) out->analogDown[0] = true;  // A
        if (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_ESCAPE]) out->analogDown[1] = true;  // B
        if (keys[SDL_SCANCODE_X]) out->analogDown[2] = true;  // X
        if (keys[SDL_SCANCODE_Y]) out->analogDown[3] = true;  // Y
        if (keys[SDL_SCANCODE_GRAVE]) out->analogDown[4] = true;  // Black
        if (keys[SDL_SCANCODE_TAB]) out->analogDown[5] = true;  // White
        if (keys[SDL_SCANCODE_Q]) out->analogDown[6] = true;  // LTrig
        if (keys[SDL_SCANCODE_E]) out->analogDown[7] = true;  // RTrig

        // Digital: DPadU,D,L,R,Start,Back,LThumb,RThumb
        if (keys[SDL_SCANCODE_UP]) out->digitalDown[0] = true;
        if (keys[SDL_SCANCODE_DOWN]) out->digitalDown[1] = true;
        if (keys[SDL_SCANCODE_LEFT]) out->digitalDown[2] = true;
        if (keys[SDL_SCANCODE_RIGHT]) out->digitalDown[3] = true;
        // Don't map keyboard to Start -- Enter is A only (Start+A = double OnADown)
        if (keys[SDL_SCANCODE_BACKSPACE]) out->digitalDown[5] = true;  // Back
        if (keys[SDL_SCANCODE_Z]) out->digitalDown[6] = true;  // LThumb
        if (keys[SDL_SCANCODE_C]) out->digitalDown[7] = true;  // RThumb

        // WASD -> left stick
        if (out->thumbLX == 0.0f)
        {
            if (keys[SDL_SCANCODE_A]) out->thumbLX = -1.0f;
            else if (keys[SDL_SCANCODE_D]) out->thumbLX = 1.0f;
        }
        if (out->thumbLY == 0.0f)
        {
            if (keys[SDL_SCANCODE_W]) out->thumbLY = 1.0f;
            else if (keys[SDL_SCANCODE_S]) out->thumbLY = -1.0f;
        }
    }
}

// ============================================================================
// Media Keys (DVD player controls mapped to keyboard)
// ============================================================================
void CJoystick::ProcessMediaKeys()
{
    extern bool ImGui_WantsKeyboard();
    extern SDL_Window* g_pSDLWindow;

    if (ImGui_WantsKeyboard()) return;
    SDL_Window* focusedWin = SDL_GetKeyboardFocus();
    if (focusedWin != g_pSDLWindow) return;

    const Uint8* keys = SDL_GetKeyboardState(NULL);

    // Build current bitmask
    uint32_t cur = 0;
    if (keys[SDL_SCANCODE_LEFTBRACKET])  cur |= (1 << 0);  // [ = OnReverse
    if (keys[SDL_SCANCODE_RIGHTBRACKET]) cur |= (1 << 1);  // ] = OnForward
    if (keys[SDL_SCANCODE_I])            cur |= (1 << 2);  // I = OnInfo
    if (keys[SDL_SCANCODE_SEMICOLON])    cur |= (1 << 3);  // ; = OnPlay
    if (keys[SDL_SCANCODE_APOSTROPHE])   cur |= (1 << 4);  // ' = OnPause
    if (keys[SDL_SCANCODE_0])            cur |= (1 << 5);
    if (keys[SDL_SCANCODE_1])            cur |= (1 << 6);
    if (keys[SDL_SCANCODE_2])            cur |= (1 << 7);
    if (keys[SDL_SCANCODE_3])            cur |= (1 << 8);
    if (keys[SDL_SCANCODE_4])            cur |= (1 << 9);
    if (keys[SDL_SCANCODE_5])            cur |= (1 << 10);
    if (keys[SDL_SCANCODE_6])            cur |= (1 << 11);
    if (keys[SDL_SCANCODE_7])            cur |= (1 << 12);
    if (keys[SDL_SCANCODE_8])            cur |= (1 << 13);
    if (keys[SDL_SCANCODE_9])            cur |= (1 << 14);

    // Fire on rising edges only
    uint32_t pressed = cur & ~m_prevMediaKeys;
    m_prevMediaKeys = cur;

    if (pressed & (1 << 0))  CallFunction("OnReverse");
    if (pressed & (1 << 1))  CallFunction("OnForward");
    if (pressed & (1 << 2))  CallFunction("OnInfo");
    if (pressed & (1 << 3))  CallFunction("OnPlay");
    if (pressed & (1 << 4))  CallFunction("OnPause");

    // Digit keys
    static const char* digitFuncs[] = {"On0","On1","On2","On3","On4","On5","On6","On7","On8","On9"};
    for (int i = 0; i < 10; i++)
    {
        if (pressed & (1 << (5 + i)))
            CallFunction(digitFuncs[i]);
    }
}

// ============================================================================
// Advance -- main input processing loop
// ============================================================================
static bool EqualSnapshots(const JoySnapshot* a, const JoySnapshot* b)
{
    for (int i = 0; i < 8; i++)
        if (a->analogDown[i] != b->analogDown[i]) return false;
    for (int i = 0; i < 8; i++)
        if (a->digitalDown[i] != b->digitalDown[i]) return false;
    if (a->thumbLX != b->thumbLX || a->thumbLY != b->thumbLY) return false;
    if (a->thumbRX != b->thumbRX || a->thumbRY != b->thumbRY) return false;
    return true;
}

void CJoystick::Advance(float nSeconds)
{
    int k;

    CNode::Advance(nSeconds);

    if (!g_bInputEnable)
        return;

    if (this != c_pBoundJoystick)
        return;

    CheckDevice();

    if (m_bNeedsInit)
    {
        PollSDL(c_controller, &m_prevState);
        m_timeNextUpdate = TheseusGetNow();
        m_bNeedsInit = false;
    }

    if (m_timeNextUpdate > TheseusGetNow())
        return;

    m_timeNextUpdate = TheseusGetNow() + 1.0f / m_frequency;

    ProcessMediaKeys();

    // Poll current state
    JoySnapshot cur;
    PollSDL(c_controller, &cur);

    if (!EqualSnapshots(&cur, &m_prevState))
    {
        if (ResetScreenSaver())
        {
            // Screen saver was active, ignore this change
            m_prevState = cur;
            return;
        }
    }

    g_xaxis = g_yaxis = 0.0f;

    bool bWasCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;

    // Thumbsticks
    m_xaxis = cur.thumbLX;
    m_yaxis = cur.thumbLY;
    m_xaxis2 = cur.thumbRX;
    m_yaxis2 = cur.thumbRY;

    // D-Pad fallback when stick is centered
    if (m_xaxis == 0.0f && m_yaxis == 0.0f)
    {
        if (cur.digitalDown[0])      m_yaxis = 1.0f;   // Up
        else if (cur.digitalDown[1]) m_yaxis = -1.0f;  // Down
        if (cur.digitalDown[2])      m_xaxis = -1.0f;  // Left
        else if (cur.digitalDown[3]) m_xaxis = 1.0f;   // Right
    }

    g_xaxis += m_xaxis;
    g_yaxis += m_yaxis;

    // Stick + D-Pad typomatic repeat
    {
        if (m_xaxis == 0.0f && m_yaxis == 0.0f)
        {
            m_nextRepeatTime = 0;
            m_repeatInterval = REPEAT_DELAY;
        }
        else if (TheseusGetNow() >= m_nextRepeatTime)
        {
            if (m_xaxis != 0.0f)
            {
                k = CheckSecretKeySequence(m_xaxis < 0.0f ? 'L' : 'R');
                CallFunction(m_xaxis < 0.0f ? "OnMoveLeft" : "OnMoveRight");
                ProcessSecretKeySequence(k);
            }

            if (m_yaxis != 0.0f)
            {
                k = CheckSecretKeySequence(m_yaxis < 0.0f ? 'D' : 'U');
                CallFunction(m_yaxis < 0.0f ? "OnMoveDown" : "OnMoveUp");
                ProcessSecretKeySequence(k);
            }

            m_nextRepeatTime = TheseusGetNow() + m_repeatInterval;
            m_repeatInterval = REPEAT_INTERVAL;
        }
    }

    // Analog button transitions (A,B,X,Y,Black,White,LTrig,RTrig)
    for (int i = 0; i < 8; i++)
    {
        bool bIsDown = cur.analogDown[i];
        bool bWasDown = m_prevState.analogDown[i];

        if (bIsDown != bWasDown)
        {
            static const char *rgszDown[] =
                {
                    "OnADown",
                    "OnBDown",
                    "OnXDown",
                    "OnYDown",
                    "OnBlackDown",
                    "OnWhiteDown",
                    "OnLeftDown",
                    "OnRightDown"};

            static const char *rgszUp[] =
                {
                    "OnAUp",
                    "OnBUp",
                    "OnXUp",
                    "OnYUp",
                    "OnBlackUp",
                    "OnWhiteUp",
                    "OnLeftUp",
                    "OnRightUp"};

            static const char rgszSecretKey[] =
                {
                    'A',
                    'B',
                    'X',
                    'Y',
                    'b', // Black
                    'w', // White
                    'l', // Left-trigger
                    'r'  // Right-trigger
                };

            {
                if (bIsDown)
                    k = CheckSecretKeySequence(rgszSecretKey[i]);

                CallFunction(bIsDown ? rgszDown[i] : rgszUp[i]);

                if (bIsDown)
                    ProcessSecretKeySequence(k);
            }
        }
    }

    // Digital button transitions (DPadU,D,L,R,Start,Back,LThumb,RThumb)
    for (int i = 0; i < 8; i++)
    {
        if (cur.digitalDown[i] != m_prevState.digitalDown[i])
        {
            static const char *rgszDown[] =
                {
                    "OnPressUp",
                    "OnPressDown",
                    "OnPressLeft",
                    "OnPressRight",
                    "OnADown",  // Start -> A (same as original)
                    "OnBDown",  // Back -> B (same as original)
                    "OnLeftThumbDown",
                    "OnRightThumbDown"};

            static const char *rgszUp[] =
                {
                    "OnReleaseUp",
                    "OnReleaseDown",
                    "OnReleaseLeft",
                    "OnReleaseRight",
                    "OnAUp",   // Start -> A
                    "OnBUp",   // Back -> B
                    "OnLeftThumbUp",
                    "OnRightThumbUp"};

            static const char rgszSecretKey[] =
                {
                    '\0',
                    '\0',
                    '\0',
                    '\0',
                    'S', // Start
                    'P', // Back
                    'Q', // Left-thumb
                    'W'  // Right-thumb
                };

            if (cur.digitalDown[i])
                k = CheckSecretKeySequence(rgszSecretKey[i]);
            CallFunction(cur.digitalDown[i] ? rgszDown[i] : rgszUp[i]);
            if (cur.digitalDown[i])
                ProcessSecretKeySequence(k);
        }
    }

    // Start+A+B combo -> OnReset
    if (cur.digitalDown[4] && cur.analogDown[0] && cur.analogDown[1])
        CallFunction("OnReset");

    // OnMoveCenter detection
    bool bIsCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;
    if (!bWasCentered && bIsCentered)
        CallFunction("OnMoveCenter");

    m_prevState = cur;
}

int CJoystick::CheckSecretKeySequence(char key)
{
    int nReturn = 0;

    if (!m_enableSecretKey || m_secretKeyLength == 0 || !m_secretKey)
    {
        ASSERT(m_secretKeyCurrentIndex == 0);
        return 0;
    }

    ASSERT(m_secretKeyCurrentIndex < m_secretKeyLength);

    if (key != m_secretKey[m_secretKeyCurrentIndex])
    {
        if (m_secretKeyCurrentIndex)
        {
            nReturn |= KS_END;
        }
        m_secretKeyCurrentIndex = 0;
        return nReturn;
    }

    if (m_secretKeyCurrentIndex == 0)
    {
        nReturn |= KS_BEGIN;
    }

    if (++m_secretKeyCurrentIndex == m_secretKeyLength)
    {
        nReturn |= KS_END;
        nReturn |= KS_VERIFIED;
        m_secretKeyCurrentIndex = 0;
    }

    return nReturn;
}

void CJoystick::ProcessSecretKeySequence(int flags)
{
    if (flags & KS_BEGIN)
        CallFunction("OnKeyVerificationEnter");

    if (flags & KS_END)
        CallFunction("OnKeyVerificationExit");

    if (flags & KS_VERIFIED)
        CallFunction("OnKeyVerified");
}

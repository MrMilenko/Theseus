// input.cpp: the Xbox Joystick node. Opens pads and IR remotes as they
// come and go and turns them into script events. The binding, properties,
// device handling, remote codes, event rules and secret key are rebuilt
// from the 5960 retail XBE (0x40620..0x41800); see docs/decomp/Input.md.
// Theseus handles each port on its own (5960 merges the pads first), adds
// LastActivePort and routes input to its overlay while that's capturing.

#include "std.h"
#include "theseus.h"
#include "node.h"

#define REPEAT_DELAY 0.3f
#define REPEAT_INTERVAL 0.12f

#define KS_BEGIN 0x0001
#define KS_VERIFIED 0x0002
#define KS_END 0x0004

// An analog button counts as down past this.
#define ANALOG_DOWN 0x20

float g_xaxis, g_yaxis;

bool g_bInputEnable;
#include "overlay.h"

class CJoystick : public CNode
{
    DECLARE_NODE(CJoystick, CNode)

public:
    CJoystick();
    ~CJoystick();

    bool m_needsInit;

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

    TCHAR *m_secretKey;
    size_t m_secretKeyLength;
    size_t m_secretKeyCurrentIndex;
    bool m_enableSecretKey;
    bool m_eatSecretKey;

    bool m_enableRemote;
    bool m_enableGamepad;

    int m_LastActivePort;

    void Advance(float nSeconds);
    bool OnSetProperty(const PRD *pprd, const void *pvValue);

    void Bind();

    bool CheckDevice();
    bool OpenDevice();

    void EnableGlobalInput(int bEnable);

    void CallFunction(const TCHAR *szFunc, bool bRemote = false);

    static HANDLE c_hDevices[4];
    static PXPP_DEVICE_TYPE c_devtype[4];

    static CJoystick *c_pPreviousBoundJoystick;
    static CJoystick *c_pBoundJoystick;

    XINPUT_STATE_INTERNAL m_joystate[4];
    XTIME m_nextRepeatTime[4];
    float m_repeatInterval[4];
    float m_remoteIdleTime;
    DWORD m_remoteRepeatCount;

    XTIME m_timeNextUpdate;
    XTIME m_timeRemoteRemoved;
    bool m_removeRemoteCalled;
    void OnMoveUp();
    void OnMoveDown();
    void OnADown();
    void OnBDown();

    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()

protected:
    int CheckSecretKeySequence(TCHAR key);
    void ProcessSecretKeySequence(int flags);

private:
    void ReadAllDevices();
    void OpenPorts(PXPP_DEVICE_TYPE type);
    bool PollRemote(int port, float nSeconds);
    void PollPad(int port);
    void FireMoves(int port);
    void KeyEvent(TCHAR key, const TCHAR *szEvent, bool bDown);
    void RemoteGone(int port);
};

HANDLE CJoystick::c_hDevices[4];
PXPP_DEVICE_TYPE CJoystick::c_devtype[4];

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
NODE_PROP(pt_integer, CJoystick, LastActivePort)
END_NODE_PROPS()

#define _FND_CLASS CJoystick
START_NODE_FUN(CJoystick, CNode)
NODE_FUN_VI(EnableGlobalInput)
NODE_FUN_VV(OnMoveUp)
NODE_FUN_VV(OnMoveDown)
NODE_FUN_VV(OnADown)
NODE_FUN_VV(OnBDown)
END_NODE_FUN()
#undef _FND_CLASS

// CJoystick::CJoystick()
// Reconstructed from 5960 retail XBE @ 0x00040620: 20 polls a second,
// sticks and buttons at rest, no secret key, remote and gamepad enabled,
// waiting for its first poll. The first Joystick made binds itself.
// Theseus starts hat at -1 and keeps per port repeat timers.
CJoystick::CJoystick()
{
    m_needsInit = true;
    m_isBound = false;
    m_frequency = 20.0f;
    m_hat = -1.0f;
    m_xaxis = m_yaxis = 0.0f;
    m_xaxis2 = m_yaxis2 = 0.0f;
    m_a = m_b = m_x = m_y = 0.0f;
    m_black = m_white = 0.0f;
    m_left = m_right = 0.0f;
    m_leftThumb = m_rightThumb = 0.0f;
    m_start = m_back = 0.0f;

    m_secretKey = NULL;
    m_secretKeyLength = 0;
    m_secretKeyCurrentIndex = 0;
    m_enableSecretKey = false;
    m_eatSecretKey = false;
    m_enableRemote = true;
    m_enableGamepad = true;
    m_LastActivePort = 0;

    m_remoteIdleTime = 0.0f;
    m_remoteRepeatCount = 0;
    m_timeRemoteRemoved = 0.0f;
    m_removeRemoteCalled = false;
    for (int port = 0; port < 4; port++)
    {
        m_nextRepeatTime[port] = 0.0f;
        m_repeatInterval[port] = 0.0f;
    }

    if (c_pBoundJoystick == NULL)
        Bind();
}

// CJoystick::~CJoystick()
// Reconstructed from 5960 retail XBE @ 0x00040750: forgets itself as the
// previous binding, hands the binding back if it held it, frees the key.
CJoystick::~CJoystick()
{
    if (c_pPreviousBoundJoystick == this)
        c_pPreviousBoundJoystick = NULL;
    if (c_pBoundJoystick == this)
        c_pBoundJoystick = c_pPreviousBoundJoystick;
    delete[] m_secretKey;
}

// Events go only to the bound joystick, and only while their kind of input
// (remote or gamepad) is enabled. 5960 tests the same at every call site.
void CJoystick::CallFunction(const TCHAR *szFunc, bool bRemote /* = false */)
{
    if (c_pBoundJoystick != this)
        return;
    if (bRemote ? !m_enableRemote : !m_enableGamepad)
        return;
    ::CallFunction(this, szFunc);
}

// BindJoystick(node)
// Reconstructed from 5960 retail XBE @ 0x00040790: NULL clears both
// bindings; a Joystick node gets bound; anything else is ignored.
void BindJoystick(CNode *pJoystickNode)
{
    if (pJoystickNode == NULL)
    {
        CJoystick::c_pBoundJoystick = NULL;
        CJoystick::c_pPreviousBoundJoystick = NULL;
        return;
    }
    if (pJoystickNode->IsKindOf(NODE_CLASS(CJoystick)))
        ((CJoystick *)pJoystickNode)->Bind();
}

// Theseus: script-callable names with nothing behind them.
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

// Take a fresh reading of every open device so held buttons don't fire.
void CJoystick::ReadAllDevices()
{
    for (int port = 0; port < 4; port++)
    {
        if (c_hDevices[port] != NULL)
            XInputGetState(c_hDevices[port], (XINPUT_STATE *)&m_joystate[port]);
    }
}

// CJoystick::Bind()
// Reconstructed from 5960 retail XBE @ 0x000407f0: the current binding
// becomes the previous one and this takes over; once it has polled before,
// it looks for devices and rereads them.
void CJoystick::Bind()
{
    c_pPreviousBoundJoystick = c_pBoundJoystick;
    c_pBoundJoystick = this;
    if (m_needsInit)
        return;
    CheckDevice();
    ReadAllDevices();
}

// CJoystick::OnSetProperty(prd, value)
// Reconstructed from 5960 retail XBE @ 0x00040820.
//     isBound: true binds; false, if bound, unbinds and rebinds the previous
//     secretKey: replaces the key (ending a sequence in progress), and a new
//         key turns enableSecretKey on
//     enableSecretKey: stores it; turning it off mid sequence ends it; the
//         position restarts either way
//     eatSecretKey, enableGamepad, enableRemote: stored here, not by the caller
bool CJoystick::OnSetProperty(const PRD *pprd, const void *pvValue)
{
    INT_PTR field = PTR2INT(pprd->pbOffset);

    if (field == offsetof(m_isBound))
    {
        if (*(const bool *)pvValue)
        {
            Bind();
        }
        else if (c_pBoundJoystick == this)
        {
            c_pBoundJoystick = NULL;
            if (c_pPreviousBoundJoystick != NULL)
                c_pPreviousBoundJoystick->Bind();
        }
        return true;
    }

    if (field == offsetof(m_secretKey))
    {
        delete[] m_secretKey;
        m_secretKey = NULL;
        m_secretKeyLength = 0;
        if (m_secretKeyCurrentIndex != 0)
            ProcessSecretKeySequence(KS_END);
        m_secretKeyCurrentIndex = 0;

        const TCHAR *szKey = *(TCHAR *const *)pvValue;
        if (szKey != NULL)
        {
            m_secretKeyLength = _tcslen(szKey);
            m_secretKey = new TCHAR[m_secretKeyLength + 1];
            _tcscpy(m_secretKey, szKey);
            m_enableSecretKey = true;
        }
        return true;
    }

    if (field == offsetof(m_enableSecretKey))
    {
        m_enableSecretKey = *(const bool *)pvValue;
        if (!m_enableSecretKey && m_secretKeyCurrentIndex != 0 && m_secretKeyCurrentIndex < m_secretKeyLength)
            ProcessSecretKeySequence(KS_END);
        m_secretKeyCurrentIndex = 0;
        return true;
    }

    if (field == offsetof(m_eatSecretKey))
    {
        m_eatSecretKey = *(const bool *)pvValue;
        return false;
    }
    if (field == offsetof(m_enableGamepad))
    {
        m_enableGamepad = *(const bool *)pvValue;
        return false;
    }
    if (field == offsetof(m_enableRemote))
    {
        m_enableRemote = *(const bool *)pvValue;
        return false;
    }

    return true;
}

// Open devices of one type that turned up on a free port, and close ours
// on ports where that type is gone.
void CJoystick::OpenPorts(PXPP_DEVICE_TYPE type)
{
    DWORD dwPresent = XGetDevices(type);
    bool bRemote = (type == XDEVICE_TYPE_IR_REMOTE);

    for (int port = 0; port < 4; port++)
    {
        bool bThere = (dwPresent & (1 << port)) != 0;

        if (c_hDevices[port] == NULL)
        {
            if (!bThere)
                continue;
            c_hDevices[port] = XInputOpen(type, port, XDEVICE_NO_SLOT, NULL);
            if (c_hDevices[port] == NULL)
                continue;

            c_devtype[port] = type;
            XInputGetState(c_hDevices[port], (XINPUT_STATE *)&m_joystate[port]);
            m_timeNextUpdate = TheseusGetNow();
            if (bRemote)
            {
                m_timeRemoteRemoved = 0.0f;
                if (m_removeRemoteCalled)
                    CallFunction(_T("OnInsertRemote"), true);
            }
        }
        else if (c_devtype[port] == type && !bThere)
        {
            XInputClose(c_hDevices[port]);
            c_devtype[port] = NULL;
            c_hDevices[port] = NULL;
            if (bRemote)
            {
                m_timeRemoteRemoved = TheseusGetNow() + 2.0f;
                m_removeRemoteCalled = false;
            }
        }
    }
}

// CJoystick::CheckDevice()
// Reconstructed from 5960 retail XBE @ 0x00040990: gamepads first, then IR
// remotes, each port opened when its device appears (and read once, so the
// state starts current) and closed when it goes. A remote that comes back
// after OnRemoveRemote fired raises OnInsertRemote; one that leaves starts
// a two second grace before OnRemoveRemote.
bool CJoystick::CheckDevice()
{
    OpenPorts(XDEVICE_TYPE_GAMEPAD);
    OpenPorts(XDEVICE_TYPE_IR_REMOTE);
    return true;
}

bool CJoystick::OpenDevice()
{
    return true;
}

// CJoystick::EnableGlobalInput(on)   [script fn 'EnableGlobalInput', FND 0x00181fb0, sig 14]
// Reconstructed from 5960 retail XBE @ 0x00040b70: g_bInputEnable = on != 0.
void CJoystick::EnableGlobalInput(int bEnable)
{
    g_bInputEnable = (bEnable != 0);
}

// A stick axis as -1..1, with everything inside +-0.9 treated as rest
// (5960 constants @ 0x0002bf08..0x0002bf10).
static float StickValue(SHORT sThumb)
{
    float value = (float)sThumb / 32767.0f;
    if (value > -0.9f && value < 0.9f)
        return 0.0f;
    return value;
}

// LastActivePort, 5960 PRD @ 0x001747e8 (int, +0x94), written by
// CJoystick::Advance @ 0x00041040 and its pad merge @ 0x00040e00: a remote
// key that maps to an event, or a pad with a button held, an analog button
// past 0x20 or a stick out of its rest zone.
static bool IsPadActive(const XINPUT_GAMEPAD *pad)
{
    if (pad->wButtons != 0)
        return true;
    for (int i = 0; i < 8; i++)
    {
        if (pad->bAnalogButtons[i] > ANALOG_DOWN)
            return true;
    }
    return StickValue(pad->sThumbLX) != 0.0f || StickValue(pad->sThumbLY) != 0.0f ||
           StickValue(pad->sThumbRX) != 0.0f || StickValue(pad->sThumbRY) != 0.0f;
}

// Same buttons pressed and sticks in the same place, as the dash sees them.
static bool SamePadInput(const XINPUT_GAMEPAD *a, const XINPUT_GAMEPAD *b)
{
    if (a->wButtons != b->wButtons)
        return false;
    for (int i = 0; i < 8; i++)
    {
        if ((a->bAnalogButtons[i] > ANALOG_DOWN) != (b->bAnalogButtons[i] > ANALOG_DOWN))
            return false;
    }
    return StickValue(a->sThumbLX) == StickValue(b->sThumbLX) &&
           StickValue(a->sThumbLY) == StickValue(b->sThumbLY) &&
           StickValue(a->sThumbRX) == StickValue(b->sThumbRX) &&
           StickValue(a->sThumbRY) == StickValue(b->sThumbRY);
}

// IR remote codes and the events they raise (5960 table @ 0x00020f60). A
// second event follows at once for the keys that also act as A and B.
struct REMOTE_KEY
{
    WORD wCode;
    const TCHAR *szEvent;
    const TCHAR *szThenEvent;
};

static const REMOTE_KEY s_remoteKeys[] =
{
    { 0xAD5, _T("OnPower"),       NULL },
    { 0xAE2, _T("OnReverse"),     NULL },
    { 0xAEA, _T("OnPlay"),        NULL },
    { 0xAE3, _T("OnForward"),     NULL },
    { 0xADD, _T("OnSkipBack"),    NULL },
    { 0xAE0, _T("OnStop"),        NULL },
    { 0xAE6, _T("OnPause"),       NULL },
    { 0xADF, _T("OnSkipForward"), NULL },
    { 0xAE5, _T("OnGuide"),       NULL },
    { 0xAC3, _T("OnInfo"),        NULL },
    { 0xAF7, _T("OnMenu"),        NULL },
    { 0xAD8, _T("OnBDown"),       _T("OnBUp") },
    { 0xAA6, _T("OnMoveUp"),      NULL },
    { 0xAA9, _T("OnMoveLeft"),    NULL },
    { 0xA0B, _T("OnADown"),       _T("OnAUp") },
    { 0xAA8, _T("OnMoveRight"),   NULL },
    { 0xAA7, _T("OnMoveDown"),    NULL },
    { 0xACF, _T("On0"),           NULL },
    { 0xACE, _T("On1"),           NULL },
    { 0xACD, _T("On2"),           NULL },
    { 0xACC, _T("On3"),           NULL },
    { 0xACB, _T("On4"),           NULL },
    { 0xACA, _T("On5"),           NULL },
    { 0xAC9, _T("On6"),           NULL },
    { 0xAC8, _T("On7"),           NULL },
    { 0xAC7, _T("On8"),           NULL },
    { 0xAC6, _T("On9"),           NULL },
};

// GetCodeFunction(code), 5960 @ 0x00040d50: the table entry for a code.
static const REMOTE_KEY *FindRemoteKey(WORD wCode)
{
    for (int i = 0; i < (int)countof(s_remoteKeys); i++)
    {
        if (s_remoteKeys[i].wCode == wCode)
            return &s_remoteKeys[i];
    }
    return NULL;
}

// The remote went quiet or failed: close it and start the grace period.
void CJoystick::RemoteGone(int port)
{
    XInputClose(c_hDevices[port]);
    c_hDevices[port] = NULL;
    c_devtype[port] = NULL;
    m_timeRemoteRemoved = TheseusGetNow() + 2.0f;
    m_removeRemoteCalled = false;
}

// One remote poll (5960 does this inside Advance). A held key repeats about
// every 65 ms, so: a new packet is a new press when the key differs or more
// than 120 ms passed since the last one, otherwise a repeat; 150 ms with no
// packet ends the hold. The first press fires, then every other repeat from
// the eighth on. Returns false when the remote failed to read.
bool CJoystick::PollRemote(int port, float nSeconds)
{
    XINPUT_STATE_INTERNAL state;
    if (XInputGetState(c_hDevices[port], (XINPUT_STATE *)&state) != ERROR_SUCCESS)
    {
        RemoteGone(port);
        return false;
    }

    if (state.dwPacketNumber == m_joystate[port].dwPacketNumber)
    {
        m_remoteIdleTime += nSeconds;
        if (m_remoteIdleTime > 0.150)
            m_remoteRepeatCount = 0;
        return true;
    }

    ResetScreenSaver();
    m_remoteIdleTime = 0.0f;
    m_joystate[port] = state;

    if (state.IrRemote.wKeyCode != m_joystate[port].IrRemote.wKeyCode || state.IrRemote.wTimeDelta > 120)
        m_remoteRepeatCount = 1;
    else
        m_remoteRepeatCount++;

    bool bFire = m_remoteRepeatCount == 1 || (m_remoteRepeatCount > 7 && (m_remoteRepeatCount % 2) != 0);
    if (!bFire)
        return true;

    const REMOTE_KEY *pKey = FindRemoteKey(state.IrRemote.wKeyCode);
    if (pKey == NULL)
        return true;

    m_LastActivePort = port;
    CallFunction(pKey->szEvent, true);
    if (pKey->szThenEvent != NULL)
        CallFunction(pKey->szThenEvent, true);
    return true;
}

// A key press with its secret key letter (0 for none): the letter steps the
// sequence before the event, its result fires after.
void CJoystick::KeyEvent(TCHAR key, const TCHAR *szEvent, bool bDown)
{
    int flags = 0;
    if (bDown)
        flags = CheckSecretKeySequence(key);
    CallFunction(szEvent);
    if (bDown)
        ProcessSecretKeySequence(flags);
}

// Held directions, 5960 @ 0x00041040: horizontal then vertical, at once,
// then after 0.3 s (0x0002c0b8), then every 0.12 s (0x0002c1f4). Coming to
// rest re-arms the first delay. Theseus's overlay takes them while it's
// capturing input.
void CJoystick::FireMoves(int port)
{
    if (m_xaxis == 0.0f && m_yaxis == 0.0f)
    {
        m_nextRepeatTime[port] = 0.0f;
        m_repeatInterval[port] = REPEAT_DELAY;
        return;
    }

    if (TheseusGetNow() < m_nextRepeatTime[port])
        return;

    if (m_xaxis != 0.0f)
    {
        bool bLeft = m_xaxis < 0.0f;
        if (g_bOverlayInputCapture)
        {
            if (bLeft)
                OverlayOnLeft();
            else
                OverlayOnRight();
        }
        else
        {
            KeyEvent(bLeft ? _T('L') : _T('R'), bLeft ? _T("OnMoveLeft") : _T("OnMoveRight"), true);
        }
    }

    if (m_yaxis != 0.0f)
    {
        bool bDown = m_yaxis < 0.0f;
        if (g_bOverlayInputCapture)
        {
            if (bDown)
                OverlayOnDown();
            else
                OverlayOnUp();
        }
        else
        {
            KeyEvent(bDown ? _T('D') : _T('U'), bDown ? _T("OnMoveDown") : _T("OnMoveUp"), true);
        }
    }

    m_nextRepeatTime[port] = TheseusGetNow() + m_repeatInterval[port];
    m_repeatInterval[port] = REPEAT_INTERVAL;
}

// Analog buttons A B X Y black white L R (5960 @ 0x00176640 / 0x00176620,
// letters "ABXYbwlr"), then digital: d-pad, Start and Back (raised as A and
// B), stick clicks (0x00176600 / 0x001765e0, letters @ 0x000259a0).
static const TCHAR *const s_analogDown[8] = { _T("OnADown"), _T("OnBDown"), _T("OnXDown"), _T("OnYDown"), _T("OnBlackDown"), _T("OnWhiteDown"), _T("OnLeftDown"), _T("OnRightDown") };
static const TCHAR *const s_analogUp[8]   = { _T("OnAUp"), _T("OnBUp"), _T("OnXUp"), _T("OnYUp"), _T("OnBlackUp"), _T("OnWhiteUp"), _T("OnLeftUp"), _T("OnRightUp") };
static const TCHAR s_analogKeys[8] = { 'A', 'B', 'X', 'Y', 'b', 'w', 'l', 'r' };
static const TCHAR *const s_digitalDown[8] = { _T("OnPressUp"), _T("OnPressDown"), _T("OnPressLeft"), _T("OnPressRight"), _T("OnADown"), _T("OnBDown"), _T("OnLeftThumbDown"), _T("OnRightThumbDown") };
static const TCHAR *const s_digitalUp[8]   = { _T("OnReleaseUp"), _T("OnReleaseDown"), _T("OnReleaseLeft"), _T("OnReleaseRight"), _T("OnAUp"), _T("OnBUp"), _T("OnLeftThumbUp"), _T("OnRightThumbUp") };
static const TCHAR s_digitalKeys[8] = { 0, 0, 0, 0, 'S', 'P', 'Q', 'W' };

// One gamepad poll, following 5960's event order: wake the screen saver on
// a change (and only wake it), sticks with the d-pad standing in for a
// resting left stick, moves, analog buttons, digital buttons, OnReset for
// Start with A and B, then OnMoveCenter once the stick comes back.
void CJoystick::PollPad(int port)
{
    XINPUT_STATE_INTERNAL state;
    if (XInputGetState(c_hDevices[port], (XINPUT_STATE *)&state) != ERROR_SUCCESS)
        return;

    const XINPUT_GAMEPAD &pad = state.Gamepad;
    const XINPUT_GAMEPAD &was = m_joystate[port].Gamepad;

    if (IsPadActive(&pad))
        m_LastActivePort = port;

    if (!SamePadInput(&pad, &was) && ResetScreenSaver())
    {
        m_joystate[port] = state;
        return;
    }

    bool bWasCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;

    m_xaxis = StickValue(pad.sThumbLX);
    m_yaxis = StickValue(pad.sThumbLY);
    m_xaxis2 = StickValue(pad.sThumbRX);
    m_yaxis2 = StickValue(pad.sThumbRY);

    if (m_xaxis == 0.0f && m_yaxis == 0.0f)
    {
        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP)
            m_yaxis = 1.0f;
        else if (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
            m_yaxis = -1.0f;
        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
            m_xaxis = -1.0f;
        else if (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
            m_xaxis = 1.0f;
    }

    g_xaxis += m_xaxis;
    g_yaxis += m_yaxis;

    FireMoves(port);

    for (int i = 0; i < 8; i++)
    {
        bool bDown = pad.bAnalogButtons[i] > ANALOG_DOWN;
        if (bDown == (was.bAnalogButtons[i] > ANALOG_DOWN))
            continue;

        // Theseus: B with the left trigger held shows or hides the overlay,
        // and the overlay takes A and B while it's capturing.
        if (i == XINPUT_GAMEPAD_B && bDown && pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] > ANALOG_DOWN)
        {
            g_bShowOverlay = !g_bShowOverlay;
            OutputDebugString(g_bShowOverlay ? L"[Overlay] Toggled ON\n" : L"[Overlay] Toggled OFF\n");
        }

        if (g_bOverlayInputCapture)
        {
            if (bDown && i == XINPUT_GAMEPAD_A)
                OverlayOnA();
            if (bDown && i == XINPUT_GAMEPAD_B)
                OverlayOnB();
            continue;
        }

        KeyEvent(s_analogKeys[i], bDown ? s_analogDown[i] : s_analogUp[i], bDown);
    }

    for (int i = 0; i < 8; i++)
    {
        WORD bit = (WORD)(1 << i);
        bool bDown = (pad.wButtons & bit) != 0;
        if (bDown == ((was.wButtons & bit) != 0))
            continue;
        KeyEvent(s_digitalKeys[i], bDown ? s_digitalDown[i] : s_digitalUp[i], bDown);
    }

    if ((pad.wButtons & XINPUT_GAMEPAD_START) &&
        pad.bAnalogButtons[XINPUT_GAMEPAD_A] > ANALOG_DOWN &&
        pad.bAnalogButtons[XINPUT_GAMEPAD_B] > ANALOG_DOWN)
        CallFunction(_T("OnReset"));

    if (!bWasCentered && m_xaxis == 0.0f && m_yaxis == 0.0f)
        CallFunction(_T("OnMoveCenter"));

    m_joystate[port] = state;
}

// CJoystick::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x00041040. Only with input enabled
// and only the bound joystick. A remote gone longer than its grace raises
// OnRemoveRemote once; devices are checked; the first poll just takes a
// reading; then at most `frequency` times a second every port is read. A
// remote that fails to read ends this poll. See docs/decomp/Input.md.
void CJoystick::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);

    if (!g_bInputEnable || this != c_pBoundJoystick)
        return;

    if (!m_removeRemoteCalled && m_timeRemoteRemoved != 0.0f && TheseusGetNow() > m_timeRemoteRemoved)
    {
        CallFunction(_T("OnRemoveRemote"), true);
        m_removeRemoteCalled = true;
    }

    if (!CheckDevice())
        return;

    if (m_needsInit)
    {
        ReadAllDevices();
        m_timeNextUpdate = TheseusGetNow();
        m_needsInit = false;
    }

    if (TheseusGetNow() < m_timeNextUpdate)
        return;
    m_timeNextUpdate = TheseusGetNow() + 1.0f / m_frequency;

    g_xaxis = g_yaxis = 0.0f;

    for (int port = 0; port < 4; port++)
    {
        if (c_hDevices[port] == NULL)
            continue;

        if (c_devtype[port] == XDEVICE_TYPE_IR_REMOTE)
        {
            if (!PollRemote(port, nSeconds))
                break;
            continue;
        }

        PollPad(port);
    }
}

// CJoystick::CheckSecretKeySequence(letter)
// Reconstructed from 5960 retail XBE @ 0x00041790. With a key set and
// enabled: the wrong letter ends a sequence in progress (KS_END) and starts
// over; the right one begins it (KS_BEGIN on the first letter) and the last
// one completes it (KS_VERIFIED | KS_END).
int CJoystick::CheckSecretKeySequence(TCHAR key)
{
    if (!m_enableSecretKey || m_secretKeyLength == 0 || m_secretKey == NULL)
        return 0;

    if (key != m_secretKey[m_secretKeyCurrentIndex])
    {
        int flags = m_secretKeyCurrentIndex != 0 ? KS_END : 0;
        m_secretKeyCurrentIndex = 0;
        return flags;
    }

    int flags = m_secretKeyCurrentIndex == 0 ? KS_BEGIN : 0;
    m_secretKeyCurrentIndex++;
    if (m_secretKeyCurrentIndex == m_secretKeyLength)
    {
        m_secretKeyCurrentIndex = 0;
        flags |= KS_VERIFIED | KS_END;
    }
    return flags;
}

// CJoystick::ProcessSecretKeySequence(flags)
// Reconstructed from 5960 retail XBE @ 0x00041800: OnKeyVerificationEnter,
// OnKeyVerificationExit, OnKeyVerified, in that order, for the flags set.
void CJoystick::ProcessSecretKeySequence(int flags)
{
    if (flags & KS_BEGIN)
        CallFunction(_T("OnKeyVerificationEnter"));
    if (flags & KS_END)
        CallFunction(_T("OnKeyVerificationExit"));
    if (flags & KS_VERIFIED)
        CallFunction(_T("OnKeyVerified"));
}

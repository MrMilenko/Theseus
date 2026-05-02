// input.cpp: CJoystick XAP node. Wraps XInput on Xbox: polls
// gamepads, exposes button state and analog axes to scripts, and
// dispatches OnUp / OnDown / OnA / etc. via the node-function table.
// Decompiled from the 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"

#define REPEAT_DELAY 0.3f
#define REPEAT_INTERVAL 0.12f

#define KS_BEGIN 0x0001
#define KS_VERIFIED 0x0002
#define KS_END 0x0004

float g_xaxis, g_yaxis;

#ifdef _DEBUG
extern bool g_bWireframe;
extern void CycleLanguage();
#endif

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
                         m_remoteIdleTime(0.0f),
                         m_timeRemoteRemoved(0.0f),
                         m_removeRemoteCalled(false),
                         m_remoteRepeatCount(0)
{
    m_needsInit = true;

    for (int i = 0; i < 4; i += 1)
    {
        m_nextRepeatTime[i] = 0.0f;
        m_repeatInterval[i] = 0.0f;
    }

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

void CJoystick::CallFunction(const TCHAR *szFunc, bool bRemote /* = false */)
{
    if (c_pBoundJoystick == this)
    {
        if (bRemote && !m_enableRemote)
        {
            TRACE(_T("Ignoring input from remote\n"));
            return;
        }
        if (!bRemote && !m_enableGamepad)
        {
            TRACE(_T("Ignoring input from gamepad\n"));
            return;
        }
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

    if (!m_needsInit)
    {
        CheckDevice();
        for (int i = 0; i < 4; i += 1)
        {
            if (c_hDevices[i] != NULL)
                XInputGetState(c_hDevices[i], (XINPUT_STATE *)&m_joystate[i]);
        }
    }
}

bool CJoystick::OnSetProperty(const PRD *pprd, const void *pvValue)
{
    if (PTR2INT(pprd->pbOffset) == offsetof(m_isBound))
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
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_secretKey))
    {
        // Delete the previous one if exists
        delete[] m_secretKey;
        m_secretKey = NULL;
        m_secretKeyLength = 0;

        // Cancel if we are in the middle of sequence checking
        if (m_secretKeyCurrentIndex)
            ProcessSecretKeySequence(KS_END);

        m_secretKeyCurrentIndex = 0;

        TCHAR *pszNewKey = *(TCHAR **)pvValue;
        if (pszNewKey)
        {
            m_secretKeyLength = _tcslen(pszNewKey);
            m_secretKey = new TCHAR[m_secretKeyLength + 1];
            _tcscpy(m_secretKey, pszNewKey);
            m_enableSecretKey = true;
        }
    }
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_enableSecretKey))
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
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_eatSecretKey))
    {
        m_eatSecretKey = *(bool *)pvValue;
        return false;
    }
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_enableGamepad))
    {
#if defined(_DEBUG) || defined(COOL_XDASH)
        TRACE(_T("\001Gamepad can be disabled only in retail build! ask Tyler (a-tylerc) why\n"));
#else
        m_enableGamepad = *(bool *)pvValue;
#endif
        return false;
    }
    else if (PTR2INT(pprd->pbOffset) == offsetof(m_enableRemote))
    {
        m_enableRemote = *(bool *)pvValue;
        return false;
    }

    return true;
}

// Poll the XInput device manager for hot-plug changes on every port,
// opening newly-attached pads/remotes and closing handles for any that
// have gone away. Called every Advance pass.
bool CJoystick::CheckDevice()
{
    DWORD gamepadDevices = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    for (int i = 0; i < 4; i += 1)
    {
        if (c_hDevices[i] == NULL)
        {
            if ((gamepadDevices & (1 << i)) != 0)
            {
                TRACE(_T("Opening device %d\n"), i);
                c_hDevices[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, NULL);
                if (c_hDevices[i] != NULL)
                {
                    TRACE(_T("We have a gamepad on port %d (0x%08x)\n"), i, c_hDevices[i]);
                    c_devtype[i] = XDEVICE_TYPE_GAMEPAD;
                    XInputGetState(c_hDevices[i], (XINPUT_STATE *)&m_joystate[i]);
                    m_timeNextUpdate = TheseusGetNow();
                }
            }
        }
        else if (c_devtype[i] == XDEVICE_TYPE_GAMEPAD)
        {
            if ((gamepadDevices & (1 << i)) == 0)
            {
                TRACE(_T("Gamepad %d was removed! (0x%08x)\n"), i, c_hDevices[i]);
                XInputClose(c_hDevices[i]);
                c_devtype[i] = NULL;
                c_hDevices[i] = NULL;
            }
        }
    }

    DWORD remoteDevices = XGetDevices(XDEVICE_TYPE_IR_REMOTE);
    for (int i = 0; i < 4; i += 1)
    {
        if (c_hDevices[i] == NULL)
        {
            if ((remoteDevices & (1 << i)) != 0)
            {
                TRACE(_T("Opening device %d\n"), i);
                c_hDevices[i] = XInputOpen(XDEVICE_TYPE_IR_REMOTE, i, XDEVICE_NO_SLOT, NULL);
                if (c_hDevices[i] != NULL)
                {
                    TRACE(_T("We have a remote control on port %d (0x%08x)\n"), i, c_hDevices[i]);
                    c_devtype[i] = XDEVICE_TYPE_IR_REMOTE;
                    XInputGetState(c_hDevices[i], (XINPUT_STATE *)&m_joystate[i]);
                    m_timeNextUpdate = TheseusGetNow();
                    m_timeRemoteRemoved = 0.0f;
                    if (m_removeRemoteCalled)
                    {
                        CallFunction(_T("OnInsertRemote"), true);
                    }
                }
            }
        }
        else if (c_devtype[i] == XDEVICE_TYPE_IR_REMOTE)
        {
            if ((remoteDevices & (1 << i)) == 0)
            {
                XInputClose(c_hDevices[i]);
                c_devtype[i] = NULL;
                c_hDevices[i] = NULL;
                m_timeRemoteRemoved = TheseusGetNow() + 2.0f;
                m_removeRemoteCalled = false;
            }
        }
    }

    return true;
}

bool CJoystick::OpenDevice()
{
    return true;
}

void CJoystick::EnableGlobalInput(int bEnable)
{
    g_bInputEnable = (bEnable != 0);
}

// Threshold for analog buttons
#define XINPUT_ANALOGBUTTONTHRESHOLD 32

static float ConvertThumb(SHORT sThumb)
{
    float axis = (float)sThumb / 32767.0f;

    // Large dead zone for prototype dukes...
    if (axis < 0.0f && axis > -0.9f)
        axis = 0.0f;
    else if (axis > 0.0f && axis < 0.9f)
        axis = 0.0f;

    return axis;
}

static bool EqualGamepads(const XINPUT_GAMEPAD *pad1, const XINPUT_GAMEPAD *pad2)
{
    if (pad1->wButtons != pad2->wButtons)
        return false;

    for (int i = 0; i < 8; i += 1)
    {
        if ((pad1->bAnalogButtons[i] > XINPUT_ANALOGBUTTONTHRESHOLD) !=
            (pad2->bAnalogButtons[i] > XINPUT_ANALOGBUTTONTHRESHOLD))
            return false;
    }

    if (ConvertThumb(pad1->sThumbLX) != ConvertThumb(pad2->sThumbLX))
        return false;

    if (ConvertThumb(pad1->sThumbLY) != ConvertThumb(pad2->sThumbLY))
        return false;

    if (ConvertThumb(pad1->sThumbRX) != ConvertThumb(pad2->sThumbRX))
        return false;

    if (ConvertThumb(pad1->sThumbRY) != ConvertThumb(pad2->sThumbRY))
        return false;

    return true;
}

struct REMOTE_CODE_TABLE
{
    TCHAR *szFunction1;
    TCHAR *szFunction2;
    WORD wCode;
};

static const REMOTE_CODE_TABLE RemoteCodeTable[] =
    {
        {_T("OnPower"), NULL, 0xAD5},
        {_T("OnReverse"), NULL, 0xAE2},
        {_T("OnPlay"), NULL, 0xAEA},
        {_T("OnForward"), NULL, 0xAE3},
        {_T("OnSkipBack"), NULL, 0xADD},
        {_T("OnStop"), NULL, 0xAE0},
        {_T("OnPause"), NULL, 0xAE6},
        {_T("OnSkipForward"), NULL, 0xADF},
        {_T("OnGuide"), NULL, 0xAE5},
        {_T("OnInfo"), NULL, 0xAC3},
        {_T("OnMenu"), NULL, 0xAF7},
        {_T("OnBDown"), _T("OnBUp"), 0xAD8},
        {_T("OnMoveUp"), NULL, 0xAA6},
        {_T("OnMoveLeft"), NULL, 0xAA9},
        {_T("OnADown"), _T("OnAUp"), 0xA0B},
        {_T("OnMoveRight"), NULL, 0xAA8},
        {_T("OnMoveDown"), NULL, 0xAA7},
        {_T("On0"), NULL, 0xACF},
        {_T("On1"), NULL, 0xACE},
        {_T("On2"), NULL, 0xACD},
        {_T("On3"), NULL, 0xACC},
        {_T("On4"), NULL, 0xACB},
        {_T("On5"), NULL, 0xACA},
        {_T("On6"), NULL, 0xAC9},
        {_T("On7"), NULL, 0xAC8},
        {_T("On8"), NULL, 0xAC7},
        {_T("On9"), NULL, 0xAC6},
        {NULL, NULL, 0}};

static const REMOTE_CODE_TABLE *GetCodeFunction(WORD wCode)
{
    const REMOTE_CODE_TABLE *pRemoteEntry = RemoteCodeTable;
    while (pRemoteEntry->wCode != 0)
    {
        if (pRemoteEntry->wCode == wCode)
            return pRemoteEntry;

        pRemoteEntry += 1;
    }

    return NULL;
}

void CJoystick::Advance(float nSeconds)
{
    int k;

    CNode::Advance(nSeconds);

    if (!g_bInputEnable)
        return;

    if (this != c_pBoundJoystick)
        return;

    if (!m_removeRemoteCalled && m_timeRemoteRemoved != 0.0f && TheseusGetNow() > m_timeRemoteRemoved)
    {
        TRACE(_T("Remote control was removed!\n"));
        CallFunction(_T("OnRemoveRemote"), true);
        m_removeRemoteCalled = true;
    }

    if (!CheckDevice())
        return;

    if (m_needsInit)
    {
        for (int i = 0; i < 4; i += 1)
        {
            if (c_hDevices[i] != NULL)
                XInputGetState(c_hDevices[i], (XINPUT_STATE *)&m_joystate[i]);
        }

        m_timeNextUpdate = TheseusGetNow();
        m_needsInit = false;
    }

    if (m_timeNextUpdate > TheseusGetNow())
        return;

    m_timeNextUpdate = TheseusGetNow() + 1.0f / m_frequency;

    g_xaxis = g_yaxis = 0.0f;

    XINPUT_STATE_INTERNAL joystate;
    for (int nJoypad = 0; nJoypad < 4; nJoypad += 1)
    {
        if (c_hDevices[nJoypad] == NULL)
            continue;

        if (c_devtype[nJoypad] == XDEVICE_TYPE_IR_REMOTE)
        {
            XINPUT_STATE_INTERNAL state;
            if (XInputGetState(c_hDevices[nJoypad], (XINPUT_STATE *)&state) != ERROR_SUCCESS)
            {
                TRACE(_T("XInputGetState for remote failed; close the device...\n"));
                XInputClose(c_hDevices[nJoypad]);
                c_hDevices[nJoypad] = NULL;
                c_devtype[nJoypad] = NULL;
                m_timeRemoteRemoved = TheseusGetNow() + 2.0f;
                m_removeRemoteCalled = false;
                break;
            }

            //
            //  Now some logic to implement typomatic on the IR remote.
            //-----------------------------------------------------------------
            //  It is important to understand how codes are sent:
            //  1) When a button is depressed its code is sent in
            //      state.IrRemote.wKeyCode.
            //  2) The time since the last code (as measured by the FW)
            //     is sent in c_state.IrRemote.wTimeDelta.
            //  3) A button held depressed on the remote will repeat about
            //     every 65 ms.
            //  4) Codes are not cached, so if we poll too slowly (less than
            //     once every 65 ms) we are likely to miss a code or see only
            //     its repeat.
            //  5) If state.dwPacketNumber doesn't change we have not received
            //     a packet since the last poll.
            //-----------------------------------------------------------------
            //  The desired behavior is:
            //  1) If a key is pressed quickly and repeatedly, it should fire once
            //     per press.
            //  2) If a key is held, there should be a short delay (~325 ms), followed
            //     by a more rapid repeat (~130 ms interval).  This interval is partly
            //     chosen as it is easy.
            //  3) When a key is released, it should stop repeating immediately.
            //-----------------------------------------------------------------
            //  To achieve this we follow the following rules:
            //  1) If we do not got a new packet in over 150 ms assume that the user
            //     has released the key.
            //  2) Use the state.IrRemote.wTimeDelta to distinguish rapid press
            //     from repeats.  In particular, if the state.IrRemote.wTimeDelta
            //     is less than 120 ms (and the key is the same as the last one)
            //     assume that it is repeat, otherwise, assume that it is a new
            //     button press.
            //  3) Pass on the first press, then pause for a number of presses
            //     and pass every n'th press.  The initiali pause and n are
            //     adjusted for a good feel.

            if (state.dwPacketNumber == m_joystate[nJoypad].dwPacketNumber)
            {
                m_remoteIdleTime += nSeconds;
                if (m_remoteIdleTime > 0.150)
                {
                    m_remoteRepeatCount = 0;
                }
            }
            else
            {
                //
                //  Reset Screen Saver
                //

                ResetScreenSaver();

                //
                //  Reset repeat delay.
                //

                m_remoteIdleTime = 0.0;

                //
                //  Copy the state.
                //

                m_joystate[nJoypad] = state;

                //
                //  Check for new key press
                //
                if (state.IrRemote.wKeyCode != m_joystate[nJoypad].IrRemote.wKeyCode || state.IrRemote.wTimeDelta > 120)
                {
                    m_remoteRepeatCount = 1;
                }
                else
                //
                //  Otherwise, it is a repeat
                //
                {
                    m_remoteRepeatCount++;
                }
                //
                //  Let through repeated buttons based on their repeat count.
                //  Adjust for an immediate key (on 1) and then a pause with
                //  a reasonable repeat rate.
                //
                if ((1 == m_remoteRepeatCount) ||
                    ((m_remoteRepeatCount > 7) && m_remoteRepeatCount % 2))
                {
                    const REMOTE_CODE_TABLE *pFunction = GetCodeFunction(state.IrRemote.wKeyCode);
                    if (pFunction != NULL)
                    {
                        TRACE(_T("IR Code: 0x%04x %s\n"), state.IrRemote.wKeyCode, pFunction->szFunction1);

                        CallFunction(pFunction->szFunction1, true);

                        if (pFunction->szFunction2 != NULL)
                            CallFunction(pFunction->szFunction2, true);
                    }
                }
            }
            continue; // done with IR Remote, continue with next port
        }

        DWORD dwError = XInputGetState(c_hDevices[nJoypad], (XINPUT_STATE *)&joystate);

        if (dwError != ERROR_SUCCESS)
        {
            // NOTE: We catch device removal above...
            TRACE(_T("\001XInputGetState returned %d (device %d)\n"), dwError, nJoypad);
            ZeroMemory(&joystate, sizeof(joystate));
            continue;
            //			return;
        }

        if (!EqualGamepads(&joystate.Gamepad, &m_joystate[nJoypad].Gamepad) /*memcmp(&joystate.Gamepad, &m_joystate[nJoypad].Gamepad, sizeof (XINPUT_GAMEPAD)) != 0*/)
        {
            if (ResetScreenSaver())
            {
                // Screen saver was active, so ignore this change...
                m_joystate[nJoypad] = joystate;
                continue;
            }
        }

        bool bWasCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;

        m_xaxis = ConvertThumb(joystate.Gamepad.sThumbLX);
        m_yaxis = ConvertThumb(joystate.Gamepad.sThumbLY);

        m_xaxis2 = ConvertThumb(joystate.Gamepad.sThumbRX);
        m_yaxis2 = ConvertThumb(joystate.Gamepad.sThumbRY);

        // Make the DPad act like the left joystick; maybe I'll be more clever later...
        if (m_xaxis == 0 && m_yaxis == 0)
        {
            if (joystate.Gamepad.wButtons & 1)
                m_yaxis = 1.0f;
            else if (joystate.Gamepad.wButtons & 2)
                m_yaxis = -1.0f;
            if (joystate.Gamepad.wButtons & 4)
                m_xaxis = -1.0f;
            else if (joystate.Gamepad.wButtons & 8)
                m_xaxis = 1.0f;
        }

        g_xaxis += m_xaxis;
        g_yaxis += m_yaxis;

        {
            if (m_xaxis == 0 && m_yaxis == 0)
            {
                m_nextRepeatTime[nJoypad] = 0;
                m_repeatInterval[nJoypad] = REPEAT_DELAY; // delay to first repeat
            }
            else if (TheseusGetNow() >= m_nextRepeatTime[nJoypad])
            {
                if (m_xaxis != 0)
                {
                    if (g_bOverlayInputCapture)
                    {
                        if (m_xaxis < 0.0f)
                            OverlayOnLeft();
                        else
                            OverlayOnRight();
                    }
                    else
                    {
                        k = CheckSecretKeySequence(m_xaxis < 0.0f ? _T('L') : _T('R'));
                        CallFunction(m_xaxis < 0.0f ? _T("OnMoveLeft") : _T("OnMoveRight"));
                        ProcessSecretKeySequence(k);
                    }
                }

                if (m_yaxis != 0)
                {
                    if (g_bOverlayInputCapture)
                    {
                        if (m_yaxis < 0.0f)
                            OverlayOnDown();
                        else
                            OverlayOnUp();
                    }
                    else
                    {
                        k = CheckSecretKeySequence(m_yaxis < 0.0f ? _T('D') : _T('U'));
                        CallFunction(m_yaxis < 0.0f ? _T("OnMoveDown") : _T("OnMoveUp"));
                        ProcessSecretKeySequence(k);
                    }
                }

                m_nextRepeatTime[nJoypad] = TheseusGetNow() + m_repeatInterval[nJoypad];
                m_repeatInterval[nJoypad] = REPEAT_INTERVAL; // delay for all but first repeat
            }
        }

        int i;
        for (i = 0; i < 8; i += 1)
        {
            bool bIsDown = joystate.Gamepad.bAnalogButtons[i] > XINPUT_ANALOGBUTTONTHRESHOLD;
            bool bWasDown = m_joystate[nJoypad].Gamepad.bAnalogButtons[i] > XINPUT_ANALOGBUTTONTHRESHOLD;

            if (bIsDown != bWasDown)
            {
                // Toggle overlay with L trigger + B
                if (i == 1 && bIsDown) // B button pressed
                {
                    if (joystate.Gamepad.bAnalogButtons[6] > XINPUT_ANALOGBUTTONTHRESHOLD) // L Trigger held
                    {
                        g_bShowOverlay = !g_bShowOverlay;
                        OutputDebugString(g_bShowOverlay ? L"[Overlay] Toggled ON\n" : L"[Overlay] Toggled OFF\n");
                    }
                }

                static const TCHAR *rgszDown[] =
                    {
                        _T("OnADown"),
                        _T("OnBDown"),
                        _T("OnXDown"),
                        _T("OnYDown"),
                        _T("OnBlackDown"),
                        _T("OnWhiteDown"),
                        _T("OnLeftDown"),
                        _T("OnRightDown")};

                static const TCHAR *rgszUp[] =
                    {
                        _T("OnAUp"),
                        _T("OnBUp"),
                        _T("OnXUp"),
                        _T("OnYUp"),
                        _T("OnBlackUp"),
                        _T("OnWhiteUp"),
                        _T("OnLeftUp"),
                        _T("OnRightUp")};

                static const TCHAR rgszSecretKey[] =
                    {
                        _T('A'),
                        _T('B'),
                        _T('X'),
                        _T('Y'),
                        _T('b'), // Black
                        _T('w'), // White
                        _T('l'), // Left-trigger
                        _T('r')  // Right-trigger
                    };

                // Overlay input routing
                if (g_bOverlayInputCapture)
                {
                    if (bIsDown)
                    {
                        if (i == 0) OverlayOnA();
                        if (i == 1) OverlayOnB();
                    }
                }
                else
                {
                    if (bIsDown)
                        k = CheckSecretKeySequence(rgszSecretKey[i]);

                    CallFunction(bIsDown ? rgszDown[i] : rgszUp[i]);

                    if (bIsDown)
                        ProcessSecretKeySequence(k);
                }

#if defined(_DEBUG) && defined(_CYCLE_LANGUAGE)
                if (i == 7 && bIsDown && m_joystate[nJoypad].Gamepad.bAnalogButtons[6] > XINPUT_ANALOGBUTTONTHRESHOLD)
                    CycleLanguage();
#endif

#ifdef COOL_XDASH
                if (m_joystate[nJoypad].Gamepad.bAnalogButtons[6] > XINPUT_ANALOGBUTTONTHRESHOLD &&
                    m_joystate[nJoypad].Gamepad.bAnalogButtons[7] > XINPUT_ANALOGBUTTONTHRESHOLD)
                {
                    if (m_joystate[nJoypad].Gamepad.bAnalogButtons[4] > XINPUT_ANALOGBUTTONTHRESHOLD)
                    {
                        XWriteTitleInfoAndReboot("xshell.xbe", "\\Device\\Harddisk0\\Partition2", ~0, 0, NULL);
                    }
                    else if (m_joystate[nJoypad].Gamepad.bAnalogButtons[5] > XINPUT_ANALOGBUTTONTHRESHOLD)
                    {
                        XWriteTitleInfoAndReboot("default.xbe", "\\Device\\Harddisk0\\Partition1\\Devkit\\Gripper", ~0, 0, NULL);
                    }
                }
#endif
            }
        }

#ifdef _DEBUG
        g_bWireframe = joystate.Gamepad.bAnalogButtons[4] > XINPUT_ANALOGBUTTONTHRESHOLD;
#endif

        WORD bit = 1;
        for (i = 0; i < 8; i += 1, bit <<= 1)
        {
            if ((joystate.Gamepad.wButtons & bit) != (m_joystate[nJoypad].Gamepad.wButtons & bit))
            {
                static const TCHAR *rgszDown[] =
                    {
                        _T("OnPressUp"),
                        _T("OnPressDown"),
                        _T("OnPressLeft"),
                        _T("OnPressRight"),
#if 1 // Make Start/Back the same as A/B; maybe I'll be more clever later...
                        _T("OnADown"),
                        _T("OnBDown"),
#else
                        _T("OnStartDown"),
                        _T("OnBackDown"),
#endif
                        _T("OnLeftThumbDown"),
                        _T("OnRightThumbDown")};

                static const TCHAR *rgszUp[] =
                    {
                        _T("OnReleaseUp"),
                        _T("OnReleaseDown"),
                        _T("OnReleaseLeft"),
                        _T("OnReleaseRight"),
#if 1 // Make Start/Back the same as A/B; maybe I'll be more clever later...
                        _T("OnAUp"),
                        _T("OnBUp"),
#else
                        _T("OnStartUp"),
                        _T("OnBackUp"),
#endif
                        _T("OnLeftThumbUp"),
                        _T("OnRightThumbUp")};

                static const TCHAR rgszSecretKey[] =
                    {
                        _T('\0'),
                        _T('\0'),
                        _T('\0'),
                        _T('\0'),
                        _T('S'), // Start
                        _T('P'), // Back
                        _T('Q'), // Left-thumb
                        _T('W')  // Right-thumb
                    };

                if (joystate.Gamepad.wButtons & bit)
                    k = CheckSecretKeySequence(rgszSecretKey[i]);
                CallFunction((joystate.Gamepad.wButtons & bit) ? rgszDown[i] : rgszUp[i]);
                if (joystate.Gamepad.wButtons & bit)
                    ProcessSecretKeySequence(k);
            }
        }

        if ((joystate.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0 &&
            joystate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A] > XINPUT_ANALOGBUTTONTHRESHOLD &&
            joystate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B] > XINPUT_ANALOGBUTTONTHRESHOLD)
            CallFunction(_T("OnReset"));

        bool bIsCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;

        if (!bWasCentered && bIsCentered)
            CallFunction(_T("OnMoveCenter"));

        m_joystate[nJoypad] = joystate;
    }
}

int CJoystick::CheckSecretKeySequence(TCHAR key)
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
        CallFunction(_T("OnKeyVerificationEnter"));

    if (flags & KS_END)
        CallFunction(_T("OnKeyVerificationExit"));

    if (flags & KS_VERIFIED)
        CallFunction(_T("OnKeyVerified"));
}
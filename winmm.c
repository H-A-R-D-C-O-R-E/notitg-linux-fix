/*
 * High-Resolution winmm proxy for NotITG on Wine/Linux
 * Uses a continuous Phase-Locked Loop (PLL) / servo clock driven by QueryPerformanceCounter
 * to ensure 100% judder-free 360Hz+ scrolling synchronized with Wine audio.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

static HMODULE g_hRealWinMM = NULL;
static CRITICAL_SECTION g_cs;
static LARGE_INTEGER g_qpc_freq;
static UINT g_sample_rate = 44100;
static UINT g_bytes_per_frame = 4;
static LONGLONG g_total_frames_written = 0;

/* Smooth PLL State */
static double g_smooth_pos = 0.0;
static LARGE_INTEGER g_last_qpc = {0};
static DWORD g_last_reported = 0;
static DWORD g_last_raw = 0;
static BOOL g_is_playing = FALSE;
static BOOL g_is_paused = FALSE;
static FILE* g_log = NULL;
static int g_log_count = 0;

static void LogMsg(const char* fmt, ...) {
    if (!g_log) {
        g_log = fopen("winmm_smooth.log", "w");
    }
    if (g_log) {
        va_list va;
        va_start(va, fmt);
        vfprintf(g_log, fmt, va);
        va_end(va);
        fflush(g_log);
    }
}

static void* pReal_CloseDriver = NULL;
static void* pReal_DefDriverProc = NULL;
static void* pReal_DriverCallback = NULL;
static void* pReal_DrvClose = NULL;
static void* pReal_DrvDefDriverProc = NULL;
static void* pReal_DrvGetModuleHandle = NULL;
static void* pReal_DrvOpen = NULL;
static void* pReal_DrvOpenA = NULL;
static void* pReal_DrvSendMessage = NULL;
static void* pReal_GetDriverFlags = NULL;
static void* pReal_GetDriverModuleHandle = NULL;
static void* pReal_OpenDriver = NULL;
static void* pReal_OpenDriverA = NULL;
static void* pReal_PlaySound = NULL;
static void* pReal_PlaySoundA = NULL;
static void* pReal_PlaySoundW = NULL;
static void* pReal_SendDriverMessage = NULL;
static void* pReal_auxGetDevCapsA = NULL;
static void* pReal_auxGetDevCapsW = NULL;
static void* pReal_auxGetNumDevs = NULL;
static void* pReal_auxGetVolume = NULL;
static void* pReal_auxOutMessage = NULL;
static void* pReal_auxSetVolume = NULL;
static void* pReal_joyConfigChanged = NULL;
static void* pReal_joyGetDevCapsA = NULL;
static void* pReal_joyGetDevCapsW = NULL;
static void* pReal_joyGetNumDevs = NULL;
static void* pReal_joyGetPos = NULL;
static void* pReal_joyGetPosEx = NULL;
static void* pReal_joyGetThreshold = NULL;
static void* pReal_joyReleaseCapture = NULL;
static void* pReal_joySetCapture = NULL;
static void* pReal_joySetThreshold = NULL;
static void* pReal_mciDriverNotify = NULL;
static void* pReal_mciDriverYield = NULL;
static void* pReal_mciExecute = NULL;
static void* pReal_mciFreeCommandResource = NULL;
static void* pReal_mciGetCreatorTask = NULL;
static void* pReal_mciGetDeviceIDA = NULL;
static void* pReal_mciGetDeviceIDFromElementIDA = NULL;
static void* pReal_mciGetDeviceIDFromElementIDW = NULL;
static void* pReal_mciGetDeviceIDW = NULL;
static void* pReal_mciGetDriverData = NULL;
static void* pReal_mciGetErrorStringA = NULL;
static void* pReal_mciGetErrorStringW = NULL;
static void* pReal_mciGetYieldProc = NULL;
static void* pReal_mciLoadCommandResource = NULL;
static void* pReal_mciSendCommandA = NULL;
static void* pReal_mciSendCommandW = NULL;
static void* pReal_mciSendStringA = NULL;
static void* pReal_mciSendStringW = NULL;
static void* pReal_mciSetDriverData = NULL;
static void* pReal_mciSetYieldProc = NULL;
static void* pReal_midiConnect = NULL;
static void* pReal_midiDisconnect = NULL;
static void* pReal_midiInAddBuffer = NULL;
static void* pReal_midiInClose = NULL;
static void* pReal_midiInGetDevCapsA = NULL;
static void* pReal_midiInGetDevCapsW = NULL;
static void* pReal_midiInGetErrorTextA = NULL;
static void* pReal_midiInGetErrorTextW = NULL;
static void* pReal_midiInGetID = NULL;
static void* pReal_midiInGetNumDevs = NULL;
static void* pReal_midiInMessage = NULL;
static void* pReal_midiInOpen = NULL;
static void* pReal_midiInPrepareHeader = NULL;
static void* pReal_midiInReset = NULL;
static void* pReal_midiInStart = NULL;
static void* pReal_midiInStop = NULL;
static void* pReal_midiInUnprepareHeader = NULL;
static void* pReal_midiOutCacheDrumPatches = NULL;
static void* pReal_midiOutCachePatches = NULL;
static void* pReal_midiOutClose = NULL;
static void* pReal_midiOutGetDevCapsA = NULL;
static void* pReal_midiOutGetDevCapsW = NULL;
static void* pReal_midiOutGetErrorTextA = NULL;
static void* pReal_midiOutGetErrorTextW = NULL;
static void* pReal_midiOutGetID = NULL;
static void* pReal_midiOutGetNumDevs = NULL;
static void* pReal_midiOutGetVolume = NULL;
static void* pReal_midiOutLongMsg = NULL;
static void* pReal_midiOutMessage = NULL;
static void* pReal_midiOutOpen = NULL;
static void* pReal_midiOutPrepareHeader = NULL;
static void* pReal_midiOutReset = NULL;
static void* pReal_midiOutSetVolume = NULL;
static void* pReal_midiOutShortMsg = NULL;
static void* pReal_midiOutUnprepareHeader = NULL;
static void* pReal_midiStreamClose = NULL;
static void* pReal_midiStreamOpen = NULL;
static void* pReal_midiStreamOut = NULL;
static void* pReal_midiStreamPause = NULL;
static void* pReal_midiStreamPosition = NULL;
static void* pReal_midiStreamProperty = NULL;
static void* pReal_midiStreamRestart = NULL;
static void* pReal_midiStreamStop = NULL;
static void* pReal_mixerClose = NULL;
static void* pReal_mixerGetControlDetailsA = NULL;
static void* pReal_mixerGetControlDetailsW = NULL;
static void* pReal_mixerGetDevCapsA = NULL;
static void* pReal_mixerGetDevCapsW = NULL;
static void* pReal_mixerGetID = NULL;
static void* pReal_mixerGetLineControlsA = NULL;
static void* pReal_mixerGetLineControlsW = NULL;
static void* pReal_mixerGetLineInfoA = NULL;
static void* pReal_mixerGetLineInfoW = NULL;
static void* pReal_mixerGetNumDevs = NULL;
static void* pReal_mixerMessage = NULL;
static void* pReal_mixerOpen = NULL;
static void* pReal_mixerSetControlDetails = NULL;
static void* pReal_mmGetCurrentTask = NULL;
static void* pReal_mmTaskBlock = NULL;
static void* pReal_mmTaskCreate = NULL;
static void* pReal_mmTaskSignal = NULL;
static void* pReal_mmTaskYield = NULL;
static void* pReal_mmioAdvance = NULL;
static void* pReal_mmioAscend = NULL;
static void* pReal_mmioClose = NULL;
static void* pReal_mmioCreateChunk = NULL;
static void* pReal_mmioDescend = NULL;
static void* pReal_mmioFlush = NULL;
static void* pReal_mmioGetInfo = NULL;
static void* pReal_mmioInstallIOProc16 = NULL;
static void* pReal_mmioInstallIOProcA = NULL;
static void* pReal_mmioInstallIOProcW = NULL;
static void* pReal_mmioOpenA = NULL;
static void* pReal_mmioOpenW = NULL;
static void* pReal_mmioRead = NULL;
static void* pReal_mmioRenameA = NULL;
static void* pReal_mmioRenameW = NULL;
static void* pReal_mmioSeek = NULL;
static void* pReal_mmioSendMessage = NULL;
static void* pReal_mmioSetBuffer = NULL;
static void* pReal_mmioSetInfo = NULL;
static void* pReal_mmioStringToFOURCCA = NULL;
static void* pReal_mmioStringToFOURCCW = NULL;
static void* pReal_mmioWrite = NULL;
static void* pReal_mmsystemGetVersion = NULL;
static void* pReal_sndPlaySoundA = NULL;
static void* pReal_sndPlaySoundW = NULL;
static void* pReal_timeBeginPeriod = NULL;
static void* pReal_timeEndPeriod = NULL;
static void* pReal_timeGetDevCaps = NULL;
static void* pReal_timeGetSystemTime = NULL;
static void* pReal_timeGetTime = NULL;
static void* pReal_timeKillEvent = NULL;
static void* pReal_timeSetEvent = NULL;
static void* pReal_waveInAddBuffer = NULL;
static void* pReal_waveInClose = NULL;
static void* pReal_waveInGetDevCapsA = NULL;
static void* pReal_waveInGetDevCapsW = NULL;
static void* pReal_waveInGetErrorTextA = NULL;
static void* pReal_waveInGetErrorTextW = NULL;
static void* pReal_waveInGetID = NULL;
static void* pReal_waveInGetNumDevs = NULL;
static void* pReal_waveInGetPosition = NULL;
static void* pReal_waveInMessage = NULL;
static void* pReal_waveInOpen = NULL;
static void* pReal_waveInPrepareHeader = NULL;
static void* pReal_waveInReset = NULL;
static void* pReal_waveInStart = NULL;
static void* pReal_waveInStop = NULL;
static void* pReal_waveInUnprepareHeader = NULL;
static void* pReal_waveOutBreakLoop = NULL;
static void* pReal_waveOutClose = NULL;
static void* pReal_waveOutGetDevCapsA = NULL;
static void* pReal_waveOutGetDevCapsW = NULL;
static void* pReal_waveOutGetErrorTextA = NULL;
static void* pReal_waveOutGetErrorTextW = NULL;
static void* pReal_waveOutGetID = NULL;
static void* pReal_waveOutGetNumDevs = NULL;
static void* pReal_waveOutGetPitch = NULL;
static void* pReal_waveOutGetPlaybackRate = NULL;
static void* pReal_waveOutGetPosition = NULL;
static void* pReal_waveOutGetVolume = NULL;
static void* pReal_waveOutMessage = NULL;
static void* pReal_waveOutOpen = NULL;
static void* pReal_waveOutPause = NULL;
static void* pReal_waveOutPrepareHeader = NULL;
static void* pReal_waveOutReset = NULL;
static void* pReal_waveOutRestart = NULL;
static void* pReal_waveOutSetPitch = NULL;
static void* pReal_waveOutSetPlaybackRate = NULL;
static void* pReal_waveOutSetVolume = NULL;
static void* pReal_waveOutUnprepareHeader = NULL;
static void* pReal_waveOutWrite = NULL;

static void InitRealWinMM(void) {
    if (g_hRealWinMM) return;
    char sysDir[MAX_PATH];
    UINT len = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (len > 0 && len < MAX_PATH - 15) {
        strcat(sysDir, "\\winmm.dll");
        g_hRealWinMM = LoadLibraryA(sysDir);
    }
    if (!g_hRealWinMM) {
        g_hRealWinMM = LoadLibraryA("C:\\windows\\system32\\winmm.dll");
    }
    if (!g_hRealWinMM) {
        LogMsg("[winmm-proxy] ERROR: Failed to load real winmm.dll!\n");
        return;
    }
    LogMsg("[winmm-proxy] Successfully hooked real winmm.dll\n");

    pReal_CloseDriver = (void*)GetProcAddress(g_hRealWinMM, "CloseDriver");
    pReal_DefDriverProc = (void*)GetProcAddress(g_hRealWinMM, "DefDriverProc");
    pReal_DriverCallback = (void*)GetProcAddress(g_hRealWinMM, "DriverCallback");
    pReal_DrvClose = (void*)GetProcAddress(g_hRealWinMM, "DrvClose");
    pReal_DrvDefDriverProc = (void*)GetProcAddress(g_hRealWinMM, "DrvDefDriverProc");
    pReal_DrvGetModuleHandle = (void*)GetProcAddress(g_hRealWinMM, "DrvGetModuleHandle");
    pReal_DrvOpen = (void*)GetProcAddress(g_hRealWinMM, "DrvOpen");
    pReal_DrvOpenA = (void*)GetProcAddress(g_hRealWinMM, "DrvOpenA");
    pReal_DrvSendMessage = (void*)GetProcAddress(g_hRealWinMM, "DrvSendMessage");
    pReal_GetDriverFlags = (void*)GetProcAddress(g_hRealWinMM, "GetDriverFlags");
    pReal_GetDriverModuleHandle = (void*)GetProcAddress(g_hRealWinMM, "GetDriverModuleHandle");
    pReal_OpenDriver = (void*)GetProcAddress(g_hRealWinMM, "OpenDriver");
    pReal_OpenDriverA = (void*)GetProcAddress(g_hRealWinMM, "OpenDriverA");
    pReal_PlaySound = (void*)GetProcAddress(g_hRealWinMM, "PlaySound");
    pReal_PlaySoundA = (void*)GetProcAddress(g_hRealWinMM, "PlaySoundA");
    pReal_PlaySoundW = (void*)GetProcAddress(g_hRealWinMM, "PlaySoundW");
    pReal_SendDriverMessage = (void*)GetProcAddress(g_hRealWinMM, "SendDriverMessage");
    pReal_auxGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "auxGetDevCapsA");
    pReal_auxGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "auxGetDevCapsW");
    pReal_auxGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "auxGetNumDevs");
    pReal_auxGetVolume = (void*)GetProcAddress(g_hRealWinMM, "auxGetVolume");
    pReal_auxOutMessage = (void*)GetProcAddress(g_hRealWinMM, "auxOutMessage");
    pReal_auxSetVolume = (void*)GetProcAddress(g_hRealWinMM, "auxSetVolume");
    pReal_joyConfigChanged = (void*)GetProcAddress(g_hRealWinMM, "joyConfigChanged");
    pReal_joyGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "joyGetDevCapsA");
    pReal_joyGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "joyGetDevCapsW");
    pReal_joyGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "joyGetNumDevs");
    pReal_joyGetPos = (void*)GetProcAddress(g_hRealWinMM, "joyGetPos");
    pReal_joyGetPosEx = (void*)GetProcAddress(g_hRealWinMM, "joyGetPosEx");
    pReal_joyGetThreshold = (void*)GetProcAddress(g_hRealWinMM, "joyGetThreshold");
    pReal_joyReleaseCapture = (void*)GetProcAddress(g_hRealWinMM, "joyReleaseCapture");
    pReal_joySetCapture = (void*)GetProcAddress(g_hRealWinMM, "joySetCapture");
    pReal_joySetThreshold = (void*)GetProcAddress(g_hRealWinMM, "joySetThreshold");
    pReal_mciDriverNotify = (void*)GetProcAddress(g_hRealWinMM, "mciDriverNotify");
    pReal_mciDriverYield = (void*)GetProcAddress(g_hRealWinMM, "mciDriverYield");
    pReal_mciExecute = (void*)GetProcAddress(g_hRealWinMM, "mciExecute");
    pReal_mciFreeCommandResource = (void*)GetProcAddress(g_hRealWinMM, "mciFreeCommandResource");
    pReal_mciGetCreatorTask = (void*)GetProcAddress(g_hRealWinMM, "mciGetCreatorTask");
    pReal_mciGetDeviceIDA = (void*)GetProcAddress(g_hRealWinMM, "mciGetDeviceIDA");
    pReal_mciGetDeviceIDFromElementIDA = (void*)GetProcAddress(g_hRealWinMM, "mciGetDeviceIDFromElementIDA");
    pReal_mciGetDeviceIDFromElementIDW = (void*)GetProcAddress(g_hRealWinMM, "mciGetDeviceIDFromElementIDW");
    pReal_mciGetDeviceIDW = (void*)GetProcAddress(g_hRealWinMM, "mciGetDeviceIDW");
    pReal_mciGetDriverData = (void*)GetProcAddress(g_hRealWinMM, "mciGetDriverData");
    pReal_mciGetErrorStringA = (void*)GetProcAddress(g_hRealWinMM, "mciGetErrorStringA");
    pReal_mciGetErrorStringW = (void*)GetProcAddress(g_hRealWinMM, "mciGetErrorStringW");
    pReal_mciGetYieldProc = (void*)GetProcAddress(g_hRealWinMM, "mciGetYieldProc");
    pReal_mciLoadCommandResource = (void*)GetProcAddress(g_hRealWinMM, "mciLoadCommandResource");
    pReal_mciSendCommandA = (void*)GetProcAddress(g_hRealWinMM, "mciSendCommandA");
    pReal_mciSendCommandW = (void*)GetProcAddress(g_hRealWinMM, "mciSendCommandW");
    pReal_mciSendStringA = (void*)GetProcAddress(g_hRealWinMM, "mciSendStringA");
    pReal_mciSendStringW = (void*)GetProcAddress(g_hRealWinMM, "mciSendStringW");
    pReal_mciSetDriverData = (void*)GetProcAddress(g_hRealWinMM, "mciSetDriverData");
    pReal_mciSetYieldProc = (void*)GetProcAddress(g_hRealWinMM, "mciSetYieldProc");
    pReal_midiConnect = (void*)GetProcAddress(g_hRealWinMM, "midiConnect");
    pReal_midiDisconnect = (void*)GetProcAddress(g_hRealWinMM, "midiDisconnect");
    pReal_midiInAddBuffer = (void*)GetProcAddress(g_hRealWinMM, "midiInAddBuffer");
    pReal_midiInClose = (void*)GetProcAddress(g_hRealWinMM, "midiInClose");
    pReal_midiInGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "midiInGetDevCapsA");
    pReal_midiInGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "midiInGetDevCapsW");
    pReal_midiInGetErrorTextA = (void*)GetProcAddress(g_hRealWinMM, "midiInGetErrorTextA");
    pReal_midiInGetErrorTextW = (void*)GetProcAddress(g_hRealWinMM, "midiInGetErrorTextW");
    pReal_midiInGetID = (void*)GetProcAddress(g_hRealWinMM, "midiInGetID");
    pReal_midiInGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "midiInGetNumDevs");
    pReal_midiInMessage = (void*)GetProcAddress(g_hRealWinMM, "midiInMessage");
    pReal_midiInOpen = (void*)GetProcAddress(g_hRealWinMM, "midiInOpen");
    pReal_midiInPrepareHeader = (void*)GetProcAddress(g_hRealWinMM, "midiInPrepareHeader");
    pReal_midiInReset = (void*)GetProcAddress(g_hRealWinMM, "midiInReset");
    pReal_midiInStart = (void*)GetProcAddress(g_hRealWinMM, "midiInStart");
    pReal_midiInStop = (void*)GetProcAddress(g_hRealWinMM, "midiInStop");
    pReal_midiInUnprepareHeader = (void*)GetProcAddress(g_hRealWinMM, "midiInUnprepareHeader");
    pReal_midiOutCacheDrumPatches = (void*)GetProcAddress(g_hRealWinMM, "midiOutCacheDrumPatches");
    pReal_midiOutCachePatches = (void*)GetProcAddress(g_hRealWinMM, "midiOutCachePatches");
    pReal_midiOutClose = (void*)GetProcAddress(g_hRealWinMM, "midiOutClose");
    pReal_midiOutGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetDevCapsA");
    pReal_midiOutGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetDevCapsW");
    pReal_midiOutGetErrorTextA = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetErrorTextA");
    pReal_midiOutGetErrorTextW = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetErrorTextW");
    pReal_midiOutGetID = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetID");
    pReal_midiOutGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetNumDevs");
    pReal_midiOutGetVolume = (void*)GetProcAddress(g_hRealWinMM, "midiOutGetVolume");
    pReal_midiOutLongMsg = (void*)GetProcAddress(g_hRealWinMM, "midiOutLongMsg");
    pReal_midiOutMessage = (void*)GetProcAddress(g_hRealWinMM, "midiOutMessage");
    pReal_midiOutOpen = (void*)GetProcAddress(g_hRealWinMM, "midiOutOpen");
    pReal_midiOutPrepareHeader = (void*)GetProcAddress(g_hRealWinMM, "midiOutPrepareHeader");
    pReal_midiOutReset = (void*)GetProcAddress(g_hRealWinMM, "midiOutReset");
    pReal_midiOutSetVolume = (void*)GetProcAddress(g_hRealWinMM, "midiOutSetVolume");
    pReal_midiOutShortMsg = (void*)GetProcAddress(g_hRealWinMM, "midiOutShortMsg");
    pReal_midiOutUnprepareHeader = (void*)GetProcAddress(g_hRealWinMM, "midiOutUnprepareHeader");
    pReal_midiStreamClose = (void*)GetProcAddress(g_hRealWinMM, "midiStreamClose");
    pReal_midiStreamOpen = (void*)GetProcAddress(g_hRealWinMM, "midiStreamOpen");
    pReal_midiStreamOut = (void*)GetProcAddress(g_hRealWinMM, "midiStreamOut");
    pReal_midiStreamPause = (void*)GetProcAddress(g_hRealWinMM, "midiStreamPause");
    pReal_midiStreamPosition = (void*)GetProcAddress(g_hRealWinMM, "midiStreamPosition");
    pReal_midiStreamProperty = (void*)GetProcAddress(g_hRealWinMM, "midiStreamProperty");
    pReal_midiStreamRestart = (void*)GetProcAddress(g_hRealWinMM, "midiStreamRestart");
    pReal_midiStreamStop = (void*)GetProcAddress(g_hRealWinMM, "midiStreamStop");
    pReal_mixerClose = (void*)GetProcAddress(g_hRealWinMM, "mixerClose");
    pReal_mixerGetControlDetailsA = (void*)GetProcAddress(g_hRealWinMM, "mixerGetControlDetailsA");
    pReal_mixerGetControlDetailsW = (void*)GetProcAddress(g_hRealWinMM, "mixerGetControlDetailsW");
    pReal_mixerGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "mixerGetDevCapsA");
    pReal_mixerGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "mixerGetDevCapsW");
    pReal_mixerGetID = (void*)GetProcAddress(g_hRealWinMM, "mixerGetID");
    pReal_mixerGetLineControlsA = (void*)GetProcAddress(g_hRealWinMM, "mixerGetLineControlsA");
    pReal_mixerGetLineControlsW = (void*)GetProcAddress(g_hRealWinMM, "mixerGetLineControlsW");
    pReal_mixerGetLineInfoA = (void*)GetProcAddress(g_hRealWinMM, "mixerGetLineInfoA");
    pReal_mixerGetLineInfoW = (void*)GetProcAddress(g_hRealWinMM, "mixerGetLineInfoW");
    pReal_mixerGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "mixerGetNumDevs");
    pReal_mixerMessage = (void*)GetProcAddress(g_hRealWinMM, "mixerMessage");
    pReal_mixerOpen = (void*)GetProcAddress(g_hRealWinMM, "mixerOpen");
    pReal_mixerSetControlDetails = (void*)GetProcAddress(g_hRealWinMM, "mixerSetControlDetails");
    pReal_mmGetCurrentTask = (void*)GetProcAddress(g_hRealWinMM, "mmGetCurrentTask");
    pReal_mmTaskBlock = (void*)GetProcAddress(g_hRealWinMM, "mmTaskBlock");
    pReal_mmTaskCreate = (void*)GetProcAddress(g_hRealWinMM, "mmTaskCreate");
    pReal_mmTaskSignal = (void*)GetProcAddress(g_hRealWinMM, "mmTaskSignal");
    pReal_mmTaskYield = (void*)GetProcAddress(g_hRealWinMM, "mmTaskYield");
    pReal_mmioAdvance = (void*)GetProcAddress(g_hRealWinMM, "mmioAdvance");
    pReal_mmioAscend = (void*)GetProcAddress(g_hRealWinMM, "mmioAscend");
    pReal_mmioClose = (void*)GetProcAddress(g_hRealWinMM, "mmioClose");
    pReal_mmioCreateChunk = (void*)GetProcAddress(g_hRealWinMM, "mmioCreateChunk");
    pReal_mmioDescend = (void*)GetProcAddress(g_hRealWinMM, "mmioDescend");
    pReal_mmioFlush = (void*)GetProcAddress(g_hRealWinMM, "mmioFlush");
    pReal_mmioGetInfo = (void*)GetProcAddress(g_hRealWinMM, "mmioGetInfo");
    pReal_mmioInstallIOProc16 = (void*)GetProcAddress(g_hRealWinMM, "mmioInstallIOProc16");
    pReal_mmioInstallIOProcA = (void*)GetProcAddress(g_hRealWinMM, "mmioInstallIOProcA");
    pReal_mmioInstallIOProcW = (void*)GetProcAddress(g_hRealWinMM, "mmioInstallIOProcW");
    pReal_mmioOpenA = (void*)GetProcAddress(g_hRealWinMM, "mmioOpenA");
    pReal_mmioOpenW = (void*)GetProcAddress(g_hRealWinMM, "mmioOpenW");
    pReal_mmioRead = (void*)GetProcAddress(g_hRealWinMM, "mmioRead");
    pReal_mmioRenameA = (void*)GetProcAddress(g_hRealWinMM, "mmioRenameA");
    pReal_mmioRenameW = (void*)GetProcAddress(g_hRealWinMM, "mmioRenameW");
    pReal_mmioSeek = (void*)GetProcAddress(g_hRealWinMM, "mmioSeek");
    pReal_mmioSendMessage = (void*)GetProcAddress(g_hRealWinMM, "mmioSendMessage");
    pReal_mmioSetBuffer = (void*)GetProcAddress(g_hRealWinMM, "mmioSetBuffer");
    pReal_mmioSetInfo = (void*)GetProcAddress(g_hRealWinMM, "mmioSetInfo");
    pReal_mmioStringToFOURCCA = (void*)GetProcAddress(g_hRealWinMM, "mmioStringToFOURCCA");
    pReal_mmioStringToFOURCCW = (void*)GetProcAddress(g_hRealWinMM, "mmioStringToFOURCCW");
    pReal_mmioWrite = (void*)GetProcAddress(g_hRealWinMM, "mmioWrite");
    pReal_mmsystemGetVersion = (void*)GetProcAddress(g_hRealWinMM, "mmsystemGetVersion");
    pReal_sndPlaySoundA = (void*)GetProcAddress(g_hRealWinMM, "sndPlaySoundA");
    pReal_sndPlaySoundW = (void*)GetProcAddress(g_hRealWinMM, "sndPlaySoundW");
    pReal_timeBeginPeriod = (void*)GetProcAddress(g_hRealWinMM, "timeBeginPeriod");
    pReal_timeEndPeriod = (void*)GetProcAddress(g_hRealWinMM, "timeEndPeriod");
    pReal_timeGetDevCaps = (void*)GetProcAddress(g_hRealWinMM, "timeGetDevCaps");
    pReal_timeGetSystemTime = (void*)GetProcAddress(g_hRealWinMM, "timeGetSystemTime");
    pReal_timeGetTime = (void*)GetProcAddress(g_hRealWinMM, "timeGetTime");
    pReal_timeKillEvent = (void*)GetProcAddress(g_hRealWinMM, "timeKillEvent");
    pReal_timeSetEvent = (void*)GetProcAddress(g_hRealWinMM, "timeSetEvent");
    pReal_waveInAddBuffer = (void*)GetProcAddress(g_hRealWinMM, "waveInAddBuffer");
    pReal_waveInClose = (void*)GetProcAddress(g_hRealWinMM, "waveInClose");
    pReal_waveInGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "waveInGetDevCapsA");
    pReal_waveInGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "waveInGetDevCapsW");
    pReal_waveInGetErrorTextA = (void*)GetProcAddress(g_hRealWinMM, "waveInGetErrorTextA");
    pReal_waveInGetErrorTextW = (void*)GetProcAddress(g_hRealWinMM, "waveInGetErrorTextW");
    pReal_waveInGetID = (void*)GetProcAddress(g_hRealWinMM, "waveInGetID");
    pReal_waveInGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "waveInGetNumDevs");
    pReal_waveInGetPosition = (void*)GetProcAddress(g_hRealWinMM, "waveInGetPosition");
    pReal_waveInMessage = (void*)GetProcAddress(g_hRealWinMM, "waveInMessage");
    pReal_waveInOpen = (void*)GetProcAddress(g_hRealWinMM, "waveInOpen");
    pReal_waveInPrepareHeader = (void*)GetProcAddress(g_hRealWinMM, "waveInPrepareHeader");
    pReal_waveInReset = (void*)GetProcAddress(g_hRealWinMM, "waveInReset");
    pReal_waveInStart = (void*)GetProcAddress(g_hRealWinMM, "waveInStart");
    pReal_waveInStop = (void*)GetProcAddress(g_hRealWinMM, "waveInStop");
    pReal_waveInUnprepareHeader = (void*)GetProcAddress(g_hRealWinMM, "waveInUnprepareHeader");
    pReal_waveOutBreakLoop = (void*)GetProcAddress(g_hRealWinMM, "waveOutBreakLoop");
    pReal_waveOutClose = (void*)GetProcAddress(g_hRealWinMM, "waveOutClose");
    pReal_waveOutGetDevCapsA = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetDevCapsA");
    pReal_waveOutGetDevCapsW = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetDevCapsW");
    pReal_waveOutGetErrorTextA = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetErrorTextA");
    pReal_waveOutGetErrorTextW = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetErrorTextW");
    pReal_waveOutGetID = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetID");
    pReal_waveOutGetNumDevs = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetNumDevs");
    pReal_waveOutGetPitch = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetPitch");
    pReal_waveOutGetPlaybackRate = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetPlaybackRate");
    pReal_waveOutGetPosition = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetPosition");
    pReal_waveOutGetVolume = (void*)GetProcAddress(g_hRealWinMM, "waveOutGetVolume");
    pReal_waveOutMessage = (void*)GetProcAddress(g_hRealWinMM, "waveOutMessage");
    pReal_waveOutOpen = (void*)GetProcAddress(g_hRealWinMM, "waveOutOpen");
    pReal_waveOutPause = (void*)GetProcAddress(g_hRealWinMM, "waveOutPause");
    pReal_waveOutPrepareHeader = (void*)GetProcAddress(g_hRealWinMM, "waveOutPrepareHeader");
    pReal_waveOutReset = (void*)GetProcAddress(g_hRealWinMM, "waveOutReset");
    pReal_waveOutRestart = (void*)GetProcAddress(g_hRealWinMM, "waveOutRestart");
    pReal_waveOutSetPitch = (void*)GetProcAddress(g_hRealWinMM, "waveOutSetPitch");
    pReal_waveOutSetPlaybackRate = (void*)GetProcAddress(g_hRealWinMM, "waveOutSetPlaybackRate");
    pReal_waveOutSetVolume = (void*)GetProcAddress(g_hRealWinMM, "waveOutSetVolume");
    pReal_waveOutUnprepareHeader = (void*)GetProcAddress(g_hRealWinMM, "waveOutUnprepareHeader");
    pReal_waveOutWrite = (void*)GetProcAddress(g_hRealWinMM, "waveOutWrite");
}

__attribute__((naked)) void proxy_CloseDriver() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_CloseDriver));
}
__attribute__((naked)) void proxy_DefDriverProc() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DefDriverProc));
}
__attribute__((naked)) void proxy_DriverCallback() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DriverCallback));
}
__attribute__((naked)) void proxy_DrvClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvClose));
}
__attribute__((naked)) void proxy_DrvDefDriverProc() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvDefDriverProc));
}
__attribute__((naked)) void proxy_DrvGetModuleHandle() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvGetModuleHandle));
}
__attribute__((naked)) void proxy_DrvOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvOpen));
}
__attribute__((naked)) void proxy_DrvOpenA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvOpenA));
}
__attribute__((naked)) void proxy_DrvSendMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_DrvSendMessage));
}
__attribute__((naked)) void proxy_GetDriverFlags() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_GetDriverFlags));
}
__attribute__((naked)) void proxy_GetDriverModuleHandle() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_GetDriverModuleHandle));
}
__attribute__((naked)) void proxy_OpenDriver() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_OpenDriver));
}
__attribute__((naked)) void proxy_OpenDriverA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_OpenDriverA));
}
__attribute__((naked)) void proxy_PlaySound() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_PlaySound));
}
__attribute__((naked)) void proxy_PlaySoundA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_PlaySoundA));
}
__attribute__((naked)) void proxy_PlaySoundW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_PlaySoundW));
}
__attribute__((naked)) void proxy_SendDriverMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_SendDriverMessage));
}
__attribute__((naked)) void proxy_auxGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxGetDevCapsA));
}
__attribute__((naked)) void proxy_auxGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxGetDevCapsW));
}
__attribute__((naked)) void proxy_auxGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxGetNumDevs));
}
__attribute__((naked)) void proxy_auxGetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxGetVolume));
}
__attribute__((naked)) void proxy_auxOutMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxOutMessage));
}
__attribute__((naked)) void proxy_auxSetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_auxSetVolume));
}
__attribute__((naked)) void proxy_joyConfigChanged() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyConfigChanged));
}
__attribute__((naked)) void proxy_joyGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetDevCapsA));
}
__attribute__((naked)) void proxy_joyGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetDevCapsW));
}
__attribute__((naked)) void proxy_joyGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetNumDevs));
}
__attribute__((naked)) void proxy_joyGetPos() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetPos));
}
__attribute__((naked)) void proxy_joyGetPosEx() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetPosEx));
}
__attribute__((naked)) void proxy_joyGetThreshold() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyGetThreshold));
}
__attribute__((naked)) void proxy_joyReleaseCapture() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joyReleaseCapture));
}
__attribute__((naked)) void proxy_joySetCapture() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joySetCapture));
}
__attribute__((naked)) void proxy_joySetThreshold() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_joySetThreshold));
}
__attribute__((naked)) void proxy_mciDriverNotify() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciDriverNotify));
}
__attribute__((naked)) void proxy_mciDriverYield() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciDriverYield));
}
__attribute__((naked)) void proxy_mciExecute() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciExecute));
}
__attribute__((naked)) void proxy_mciFreeCommandResource() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciFreeCommandResource));
}
__attribute__((naked)) void proxy_mciGetCreatorTask() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetCreatorTask));
}
__attribute__((naked)) void proxy_mciGetDeviceIDA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetDeviceIDA));
}
__attribute__((naked)) void proxy_mciGetDeviceIDFromElementIDA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetDeviceIDFromElementIDA));
}
__attribute__((naked)) void proxy_mciGetDeviceIDFromElementIDW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetDeviceIDFromElementIDW));
}
__attribute__((naked)) void proxy_mciGetDeviceIDW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetDeviceIDW));
}
__attribute__((naked)) void proxy_mciGetDriverData() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetDriverData));
}
__attribute__((naked)) void proxy_mciGetErrorStringA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetErrorStringA));
}
__attribute__((naked)) void proxy_mciGetErrorStringW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetErrorStringW));
}
__attribute__((naked)) void proxy_mciGetYieldProc() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciGetYieldProc));
}
__attribute__((naked)) void proxy_mciLoadCommandResource() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciLoadCommandResource));
}
__attribute__((naked)) void proxy_mciSendCommandA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSendCommandA));
}
__attribute__((naked)) void proxy_mciSendCommandW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSendCommandW));
}
__attribute__((naked)) void proxy_mciSendStringA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSendStringA));
}
__attribute__((naked)) void proxy_mciSendStringW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSendStringW));
}
__attribute__((naked)) void proxy_mciSetDriverData() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSetDriverData));
}
__attribute__((naked)) void proxy_mciSetYieldProc() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mciSetYieldProc));
}
__attribute__((naked)) void proxy_midiConnect() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiConnect));
}
__attribute__((naked)) void proxy_midiDisconnect() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiDisconnect));
}
__attribute__((naked)) void proxy_midiInAddBuffer() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInAddBuffer));
}
__attribute__((naked)) void proxy_midiInClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInClose));
}
__attribute__((naked)) void proxy_midiInGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetDevCapsA));
}
__attribute__((naked)) void proxy_midiInGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetDevCapsW));
}
__attribute__((naked)) void proxy_midiInGetErrorTextA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetErrorTextA));
}
__attribute__((naked)) void proxy_midiInGetErrorTextW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetErrorTextW));
}
__attribute__((naked)) void proxy_midiInGetID() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetID));
}
__attribute__((naked)) void proxy_midiInGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInGetNumDevs));
}
__attribute__((naked)) void proxy_midiInMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInMessage));
}
__attribute__((naked)) void proxy_midiInOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInOpen));
}
__attribute__((naked)) void proxy_midiInPrepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInPrepareHeader));
}
__attribute__((naked)) void proxy_midiInReset() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInReset));
}
__attribute__((naked)) void proxy_midiInStart() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInStart));
}
__attribute__((naked)) void proxy_midiInStop() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInStop));
}
__attribute__((naked)) void proxy_midiInUnprepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiInUnprepareHeader));
}
__attribute__((naked)) void proxy_midiOutCacheDrumPatches() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutCacheDrumPatches));
}
__attribute__((naked)) void proxy_midiOutCachePatches() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutCachePatches));
}
__attribute__((naked)) void proxy_midiOutClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutClose));
}
__attribute__((naked)) void proxy_midiOutGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetDevCapsA));
}
__attribute__((naked)) void proxy_midiOutGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetDevCapsW));
}
__attribute__((naked)) void proxy_midiOutGetErrorTextA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetErrorTextA));
}
__attribute__((naked)) void proxy_midiOutGetErrorTextW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetErrorTextW));
}
__attribute__((naked)) void proxy_midiOutGetID() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetID));
}
__attribute__((naked)) void proxy_midiOutGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetNumDevs));
}
__attribute__((naked)) void proxy_midiOutGetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutGetVolume));
}
__attribute__((naked)) void proxy_midiOutLongMsg() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutLongMsg));
}
__attribute__((naked)) void proxy_midiOutMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutMessage));
}
__attribute__((naked)) void proxy_midiOutOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutOpen));
}
__attribute__((naked)) void proxy_midiOutPrepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutPrepareHeader));
}
__attribute__((naked)) void proxy_midiOutReset() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutReset));
}
__attribute__((naked)) void proxy_midiOutSetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutSetVolume));
}
__attribute__((naked)) void proxy_midiOutShortMsg() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutShortMsg));
}
__attribute__((naked)) void proxy_midiOutUnprepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiOutUnprepareHeader));
}
__attribute__((naked)) void proxy_midiStreamClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamClose));
}
__attribute__((naked)) void proxy_midiStreamOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamOpen));
}
__attribute__((naked)) void proxy_midiStreamOut() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamOut));
}
__attribute__((naked)) void proxy_midiStreamPause() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamPause));
}
__attribute__((naked)) void proxy_midiStreamPosition() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamPosition));
}
__attribute__((naked)) void proxy_midiStreamProperty() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamProperty));
}
__attribute__((naked)) void proxy_midiStreamRestart() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamRestart));
}
__attribute__((naked)) void proxy_midiStreamStop() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_midiStreamStop));
}
__attribute__((naked)) void proxy_mixerClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerClose));
}
__attribute__((naked)) void proxy_mixerGetControlDetailsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetControlDetailsA));
}
__attribute__((naked)) void proxy_mixerGetControlDetailsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetControlDetailsW));
}
__attribute__((naked)) void proxy_mixerGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetDevCapsA));
}
__attribute__((naked)) void proxy_mixerGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetDevCapsW));
}
__attribute__((naked)) void proxy_mixerGetID() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetID));
}
__attribute__((naked)) void proxy_mixerGetLineControlsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetLineControlsA));
}
__attribute__((naked)) void proxy_mixerGetLineControlsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetLineControlsW));
}
__attribute__((naked)) void proxy_mixerGetLineInfoA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetLineInfoA));
}
__attribute__((naked)) void proxy_mixerGetLineInfoW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetLineInfoW));
}
__attribute__((naked)) void proxy_mixerGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerGetNumDevs));
}
__attribute__((naked)) void proxy_mixerMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerMessage));
}
__attribute__((naked)) void proxy_mixerOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerOpen));
}
__attribute__((naked)) void proxy_mixerSetControlDetails() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mixerSetControlDetails));
}
__attribute__((naked)) void proxy_mmGetCurrentTask() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmGetCurrentTask));
}
__attribute__((naked)) void proxy_mmTaskBlock() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmTaskBlock));
}
__attribute__((naked)) void proxy_mmTaskCreate() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmTaskCreate));
}
__attribute__((naked)) void proxy_mmTaskSignal() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmTaskSignal));
}
__attribute__((naked)) void proxy_mmTaskYield() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmTaskYield));
}
__attribute__((naked)) void proxy_mmioAdvance() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioAdvance));
}
__attribute__((naked)) void proxy_mmioAscend() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioAscend));
}
__attribute__((naked)) void proxy_mmioClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioClose));
}
__attribute__((naked)) void proxy_mmioCreateChunk() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioCreateChunk));
}
__attribute__((naked)) void proxy_mmioDescend() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioDescend));
}
__attribute__((naked)) void proxy_mmioFlush() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioFlush));
}
__attribute__((naked)) void proxy_mmioGetInfo() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioGetInfo));
}
__attribute__((naked)) void proxy_mmioInstallIOProc16() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioInstallIOProc16));
}
__attribute__((naked)) void proxy_mmioInstallIOProcA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioInstallIOProcA));
}
__attribute__((naked)) void proxy_mmioInstallIOProcW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioInstallIOProcW));
}
__attribute__((naked)) void proxy_mmioOpenA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioOpenA));
}
__attribute__((naked)) void proxy_mmioOpenW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioOpenW));
}
__attribute__((naked)) void proxy_mmioRead() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioRead));
}
__attribute__((naked)) void proxy_mmioRenameA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioRenameA));
}
__attribute__((naked)) void proxy_mmioRenameW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioRenameW));
}
__attribute__((naked)) void proxy_mmioSeek() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioSeek));
}
__attribute__((naked)) void proxy_mmioSendMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioSendMessage));
}
__attribute__((naked)) void proxy_mmioSetBuffer() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioSetBuffer));
}
__attribute__((naked)) void proxy_mmioSetInfo() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioSetInfo));
}
__attribute__((naked)) void proxy_mmioStringToFOURCCA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioStringToFOURCCA));
}
__attribute__((naked)) void proxy_mmioStringToFOURCCW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioStringToFOURCCW));
}
__attribute__((naked)) void proxy_mmioWrite() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmioWrite));
}
__attribute__((naked)) void proxy_mmsystemGetVersion() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_mmsystemGetVersion));
}
__attribute__((naked)) void proxy_sndPlaySoundA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_sndPlaySoundA));
}
__attribute__((naked)) void proxy_sndPlaySoundW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_sndPlaySoundW));
}
__attribute__((naked)) void proxy_timeBeginPeriod() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeBeginPeriod));
}
__attribute__((naked)) void proxy_timeEndPeriod() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeEndPeriod));
}
__attribute__((naked)) void proxy_timeGetDevCaps() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeGetDevCaps));
}
__attribute__((naked)) void proxy_timeGetSystemTime() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeGetSystemTime));
}
__attribute__((naked)) void proxy_timeGetTime() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeGetTime));
}
__attribute__((naked)) void proxy_timeKillEvent() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeKillEvent));
}
__attribute__((naked)) void proxy_timeSetEvent() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_timeSetEvent));
}
__attribute__((naked)) void proxy_waveInAddBuffer() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInAddBuffer));
}
__attribute__((naked)) void proxy_waveInClose() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInClose));
}
__attribute__((naked)) void proxy_waveInGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetDevCapsA));
}
__attribute__((naked)) void proxy_waveInGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetDevCapsW));
}
__attribute__((naked)) void proxy_waveInGetErrorTextA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetErrorTextA));
}
__attribute__((naked)) void proxy_waveInGetErrorTextW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetErrorTextW));
}
__attribute__((naked)) void proxy_waveInGetID() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetID));
}
__attribute__((naked)) void proxy_waveInGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetNumDevs));
}
__attribute__((naked)) void proxy_waveInGetPosition() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInGetPosition));
}
__attribute__((naked)) void proxy_waveInMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInMessage));
}
__attribute__((naked)) void proxy_waveInOpen() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInOpen));
}
__attribute__((naked)) void proxy_waveInPrepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInPrepareHeader));
}
__attribute__((naked)) void proxy_waveInReset() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInReset));
}
__attribute__((naked)) void proxy_waveInStart() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInStart));
}
__attribute__((naked)) void proxy_waveInStop() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInStop));
}
__attribute__((naked)) void proxy_waveInUnprepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveInUnprepareHeader));
}
__attribute__((naked)) void proxy_waveOutBreakLoop() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutBreakLoop));
}
__attribute__((naked)) void proxy_waveOutGetDevCapsA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetDevCapsA));
}
__attribute__((naked)) void proxy_waveOutGetDevCapsW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetDevCapsW));
}
__attribute__((naked)) void proxy_waveOutGetErrorTextA() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetErrorTextA));
}
__attribute__((naked)) void proxy_waveOutGetErrorTextW() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetErrorTextW));
}
__attribute__((naked)) void proxy_waveOutGetID() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetID));
}
__attribute__((naked)) void proxy_waveOutGetNumDevs() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetNumDevs));
}
__attribute__((naked)) void proxy_waveOutGetPitch() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetPitch));
}
__attribute__((naked)) void proxy_waveOutGetPlaybackRate() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetPlaybackRate));
}
__attribute__((naked)) void proxy_waveOutGetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutGetVolume));
}
__attribute__((naked)) void proxy_waveOutMessage() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutMessage));
}
__attribute__((naked)) void proxy_waveOutPrepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutPrepareHeader));
}
__attribute__((naked)) void proxy_waveOutSetPitch() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutSetPitch));
}
__attribute__((naked)) void proxy_waveOutSetPlaybackRate() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutSetPlaybackRate));
}
__attribute__((naked)) void proxy_waveOutSetVolume() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutSetVolume));
}
__attribute__((naked)) void proxy_waveOutUnprepareHeader() {
    __asm__ volatile ("jmp *%0" : : "m"(pReal_waveOutUnprepareHeader));
}

MMRESULT WINAPI proxy_waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    typedef MMRESULT (WINAPI *pfn)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    pfn real_func = (pfn)pReal_waveOutOpen;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
    if (res == MMSYSERR_NOERROR && pwfx) {
        EnterCriticalSection(&g_cs);
        g_sample_rate = pwfx->nSamplesPerSec ? pwfx->nSamplesPerSec : 44100;
        g_bytes_per_frame = pwfx->nBlockAlign ? pwfx->nBlockAlign : (pwfx->nChannels * 2);
        g_total_frames_written = 0;
        g_smooth_pos = 0.0;
        g_last_reported = 0;
        g_last_raw = 0;
        g_last_qpc.QuadPart = 0;
        g_is_playing = FALSE;
        g_is_paused = FALSE;
        LeaveCriticalSection(&g_cs);
        LogMsg("[winmm-proxy] waveOutOpen: rate=%u, channels=%u, blockAlign=%u\n", 
               g_sample_rate, pwfx->nChannels, g_bytes_per_frame);
    }
    return res;
}

MMRESULT WINAPI proxy_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT, LPWAVEHDR, UINT);
    pfn real_func = (pfn)pReal_waveOutWrite;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo, pwh, cbwh);
    if (res == MMSYSERR_NOERROR && pwh && g_bytes_per_frame > 0) {
        EnterCriticalSection(&g_cs);
        g_total_frames_written += (pwh->dwBufferLength / g_bytes_per_frame);
        LeaveCriticalSection(&g_cs);
    }
    return res;
}

MMRESULT WINAPI proxy_waveOutPause(HWAVEOUT hwo) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT);
    pfn real_func = (pfn)pReal_waveOutPause;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo);
    if (res == MMSYSERR_NOERROR) {
        EnterCriticalSection(&g_cs);
        g_is_paused = TRUE;
        LeaveCriticalSection(&g_cs);
    }
    return res;
}

MMRESULT WINAPI proxy_waveOutRestart(HWAVEOUT hwo) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT);
    pfn real_func = (pfn)pReal_waveOutRestart;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo);
    if (res == MMSYSERR_NOERROR) {
        EnterCriticalSection(&g_cs);
        g_is_paused = FALSE;
        QueryPerformanceCounter(&g_last_qpc);
        LeaveCriticalSection(&g_cs);
    }
    return res;
}

MMRESULT WINAPI proxy_waveOutReset(HWAVEOUT hwo) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT);
    pfn real_func = (pfn)pReal_waveOutReset;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo);
    EnterCriticalSection(&g_cs);
    g_total_frames_written = 0;
    g_smooth_pos = 0.0;
    g_last_reported = 0;
    g_last_raw = 0;
    g_last_qpc.QuadPart = 0;
    g_is_playing = FALSE;
    g_is_paused = FALSE;
    LeaveCriticalSection(&g_cs);
    return res;
}

MMRESULT WINAPI proxy_waveOutClose(HWAVEOUT hwo) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT);
    pfn real_func = (pfn)pReal_waveOutClose;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo);
    EnterCriticalSection(&g_cs);
    g_total_frames_written = 0;
    g_smooth_pos = 0.0;
    g_last_reported = 0;
    g_last_raw = 0;
    g_last_qpc.QuadPart = 0;
    g_is_playing = FALSE;
    g_is_paused = FALSE;
    LeaveCriticalSection(&g_cs);
    LogMsg("[winmm-proxy] waveOutClose\n");
    return res;
}

MMRESULT WINAPI proxy_waveOutGetPosition(HWAVEOUT hwo, LPMMTIME pmmt, UINT cbmmt) {
    typedef MMRESULT (WINAPI *pfn)(HWAVEOUT, LPMMTIME, UINT);
    pfn real_func = (pfn)pReal_waveOutGetPosition;
    if (!real_func) return MMSYSERR_ERROR;
    
    MMRESULT res = real_func(hwo, pmmt, cbmmt);
    if (res != MMSYSERR_NOERROR || !pmmt || cbmmt < sizeof(MMTIME)) {
        return res;
    }

    DWORD raw_sample = 0;
    if (pmmt->wType == TIME_SAMPLES) {
        raw_sample = pmmt->u.sample;
    } else if (pmmt->wType == TIME_BYTES && g_bytes_per_frame > 0) {
        raw_sample = pmmt->u.cb / g_bytes_per_frame;
    } else if (pmmt->wType == TIME_MS && g_sample_rate > 0) {
        raw_sample = (DWORD)(((ULONGLONG)pmmt->u.ms * g_sample_rate) / 1000);
    } else {
        return res;
    }

    EnterCriticalSection(&g_cs);
    
    LARGE_INTEGER now_qpc;
    QueryPerformanceCounter(&now_qpc);

    double raw_d = (double)raw_sample;

    // Detect startup, song restarts/loops, or massive jumps (> 250ms discrepancy)
    if (!g_is_playing || raw_sample < g_last_raw || fabs(raw_d - g_smooth_pos) > (g_sample_rate * 0.25)) {
        g_smooth_pos = raw_d;
        g_last_qpc = now_qpc;
        g_last_reported = raw_sample;
        g_last_raw = raw_sample;
        g_is_playing = TRUE;
    } else {
        g_last_raw = raw_sample;
        
        double dt = 0.0;
        if (g_qpc_freq.QuadPart > 0 && g_last_qpc.QuadPart > 0) {
            LONGLONG dt_ticks = now_qpc.QuadPart - g_last_qpc.QuadPart;
            if (dt_ticks > 0) {
                dt = (double)dt_ticks / (double)g_qpc_freq.QuadPart;
            }
        }
        g_last_qpc = now_qpc;

        // Cap dt to 50ms in case of scheduler pauses/hitches
        if (dt > 0.050) dt = 0.050;

        if (!g_is_paused && dt > 0.0) {
            /*
             * Phase-Locked Loop (PLL) Servo:
             * error = raw_d - g_smooth_pos
             * If smooth is lagging behind Wine audio (error > 0), speed up by up to +2%.
             * If smooth is ahead of Wine audio (error < 0), slow down by down to -2%.
             * NEVER freeze or jump. Rate smoothly adjusts so movement is 100% continuous.
             */
            double error = raw_d - g_smooth_pos;
            double speed = 1.0 + (error / (double)g_sample_rate) * 0.75;
            if (speed > 1.025) speed = 1.025;
            if (speed < 0.975) speed = 0.975;

            g_smooth_pos += dt * (double)g_sample_rate * speed;
        }
    }

    // Never exceed audio actually written
    if (g_total_frames_written > 0 && g_smooth_pos > (double)g_total_frames_written) {
        g_smooth_pos = (double)g_total_frames_written;
    }

    // Strictly enforce monotonicity (never go backward)
    DWORD reported_sample = (DWORD)g_smooth_pos;
    if (reported_sample < g_last_reported) {
        reported_sample = g_last_reported;
    } else {
        g_last_reported = reported_sample;
    }
    if (g_smooth_pos < (double)reported_sample) {
        g_smooth_pos = (double)reported_sample;
    }

    if (g_log_count < 250 && g_is_playing) {
        g_log_count++;
        LogMsg("[pll] #%03d: raw=%u, reported=%u, diff=%.1f\n",
               g_log_count, raw_sample, reported_sample, (double)reported_sample - raw_d);
    }

    // Write back into the requested format
    if (pmmt->wType == TIME_SAMPLES) {
        pmmt->u.sample = reported_sample;
    } else if (pmmt->wType == TIME_BYTES && g_bytes_per_frame > 0) {
        pmmt->u.cb = reported_sample * g_bytes_per_frame;
    } else if (pmmt->wType == TIME_MS && g_sample_rate > 0) {
        pmmt->u.ms = (DWORD)(((ULONGLONG)reported_sample * 1000) / g_sample_rate);
    }

    LeaveCriticalSection(&g_cs);
    return MMSYSERR_NOERROR;
}

/* =========================================================================
 * Transparent AFT Fix: Hook GDI ChoosePixelFormat to ensure Alpha Buffer
 * ========================================================================= */
typedef int (WINAPI *pfn_ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR*);
static pfn_ChoosePixelFormat g_pReal_ChoosePixelFormat = NULL;

typedef BOOL (WINAPI *pfn_SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR*);
static pfn_SetPixelFormat g_pReal_SetPixelFormat = NULL;

typedef int (WINAPI *pfn_DescribePixelFormat)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
static pfn_DescribePixelFormat g_pReal_DescribePixelFormat = NULL;

static int WINAPI Hook_ChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd) {
    LogMsg("[winmm-pfd] ChoosePixelFormat intercepted!\n");
    if (ppfd) {
        LogMsg("[winmm-pfd] Requested: flags=0x%lx, type=%u, colorBits=%u, alphaBits=%u, depthBits=%u, stencilBits=%u\n",
               ppfd->dwFlags, ppfd->iPixelType, ppfd->cColorBits, ppfd->cAlphaBits, ppfd->cDepthBits, ppfd->cStencilBits);
    }

    if (!g_pReal_DescribePixelFormat) {
        HMODULE hGdi = GetModuleHandleA("gdi32.dll");
        if (!hGdi) hGdi = LoadLibraryA("gdi32.dll");
        if (hGdi) {
            g_pReal_DescribePixelFormat = (pfn_DescribePixelFormat)GetProcAddress(hGdi, "DescribePixelFormat");
        }
    }

    int wine_choice = 0;
    if (g_pReal_ChoosePixelFormat) {
        wine_choice = g_pReal_ChoosePixelFormat(hdc, ppfd);
        LogMsg("[winmm-pfd] Wine original choice: %d\n", wine_choice);
    }

    if (g_pReal_DescribePixelFormat && wine_choice > 0) {
        PIXELFORMATDESCRIPTOR origPfd;
        ZeroMemory(&origPfd, sizeof(origPfd));
        if (g_pReal_DescribePixelFormat(hdc, wine_choice, sizeof(origPfd), &origPfd)) {
            LogMsg("[winmm-pfd] Wine format %d details: flags=0x%lx, type=%u, color=%u (R%u G%u B%u A%u), depth=%u, stencil=%u\n",
                   wine_choice, origPfd.dwFlags, origPfd.iPixelType, origPfd.cColorBits,
                   origPfd.cRedBits, origPfd.cGreenBits, origPfd.cBlueBits, origPfd.cAlphaBits,
                   origPfd.cDepthBits, origPfd.cStencilBits);
            if (origPfd.cAlphaBits >= 8) {
                LogMsg("[winmm-pfd] Wine format already has %u alpha bits! Keeping it.\n", origPfd.cAlphaBits);
                return wine_choice;
            }
        }
    }

    // Wine format has 0 alpha bits. Enumerate all available formats to find one with alpha >= 8!
    if (g_pReal_DescribePixelFormat) {
        int max_formats = g_pReal_DescribePixelFormat(hdc, 1, sizeof(PIXELFORMATDESCRIPTOR), NULL);
        LogMsg("[winmm-pfd] Enumerating %d pixel formats for 32-bit RGBA (alpha >= 8)...\n", max_formats);

        int best_format = 0;
        int best_score = -1;

        for (int i = 1; i <= max_formats; i++) {
            PIXELFORMATDESCRIPTOR pfd;
            ZeroMemory(&pfd, sizeof(pfd));
            if (!g_pReal_DescribePixelFormat(hdc, i, sizeof(pfd), &pfd))
                continue;

            // Must support OpenGL, window, and double buffering
            if (!(pfd.dwFlags & PFD_SUPPORT_OPENGL)) continue;
            if (!(pfd.dwFlags & PFD_DRAW_TO_WINDOW)) continue;
            if (!(pfd.dwFlags & PFD_DOUBLEBUFFER)) continue;
            if (pfd.iPixelType != PFD_TYPE_RGBA) continue;

            // Must have alpha bits!
            if (pfd.cAlphaBits < 8) continue;

            int score = 0;

            // Prefer hardware accelerated (ICD)
            if (!(pfd.dwFlags & PFD_GENERIC_FORMAT)) {
                score += 10000;
            }

            // Prefer 32-bit color
            if (pfd.cColorBits == 32) score += 500;
            else if (pfd.cColorBits >= 24) score += 400;

            // Prefer exactly 8 alpha bits
            if (pfd.cAlphaBits == 8) score += 200;

            // Prefer 24 or 32 depth bits
            if (pfd.cDepthBits == 24) score += 100;
            else if (pfd.cDepthBits == 32) score += 90;
            else if (pfd.cDepthBits == 16) score += 80;

            // Prefer 8 stencil bits
            if (pfd.cStencilBits == 8) score += 50;

            LogMsg("[winmm-pfd] Candidate %d: flags=0x%lx color=%u alpha=%u depth=%u stencil=%u -> score %d\n",
                   i, pfd.dwFlags, pfd.cColorBits, pfd.cAlphaBits, pfd.cDepthBits, pfd.cStencilBits, score);

            if (score > best_score) {
                best_score = score;
                best_format = i;
            }
        }

        if (best_format > 0) {
            PIXELFORMATDESCRIPTOR bestPfd;
            ZeroMemory(&bestPfd, sizeof(bestPfd));
            g_pReal_DescribePixelFormat(hdc, best_format, sizeof(bestPfd), &bestPfd);
            LogMsg("[winmm-pfd] -> SELECTED alpha format %d (score %d): flags=0x%lx, color=%u (A%u), depth=%u, stencil=%u\n",
                   best_format, best_score, bestPfd.dwFlags, bestPfd.cColorBits,
                   bestPfd.cAlphaBits, bestPfd.cDepthBits, bestPfd.cStencilBits);
            return best_format;
        } else {
            LogMsg("[winmm-pfd] WARNING: No suitable format with alpha >= 8 found!\n");
        }
    }

    return wine_choice;
}

static BOOL WINAPI Hook_SetPixelFormat(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd) {
    LogMsg("[winmm-pfd] SetPixelFormat called for format %d\n", iPixelFormat);
    BOOL res = FALSE;
    if (g_pReal_SetPixelFormat) {
        res = g_pReal_SetPixelFormat(hdc, iPixelFormat, ppfd);
    }
    LogMsg("[winmm-pfd] SetPixelFormat result: %d (LastError=0x%lx)\n", res, GetLastError());
    return res;
}

static void InstallGDIHooks(void) {
    HMODULE hGdi = GetModuleHandleA("gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryA("gdi32.dll");
    if (hGdi) {
        g_pReal_ChoosePixelFormat = (pfn_ChoosePixelFormat)GetProcAddress(hGdi, "ChoosePixelFormat");
        g_pReal_SetPixelFormat = (pfn_SetPixelFormat)GetProcAddress(hGdi, "SetPixelFormat");
        g_pReal_DescribePixelFormat = (pfn_DescribePixelFormat)GetProcAddress(hGdi, "DescribePixelFormat");
    }

    HMODULE hExe = GetModuleHandleA(NULL);
    if (!hExe) return;

    BYTE* base = (BYTE*)hExe;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    IMAGE_DATA_DIRECTORY importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!importDir.VirtualAddress) return;

    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)(base + importDir.VirtualAddress);
    BOOL chooseHooked = FALSE;
    BOOL setHooked = FALSE;

    for (; importDesc->Name != 0; importDesc++) {
        const char* name = (const char*)(base + importDesc->Name);
        if (_stricmp(name, "gdi32.dll") == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + importDesc->FirstThunk);
            PIMAGE_THUNK_DATA origThunk = (PIMAGE_THUNK_DATA)(base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));

            for (; thunk->u1.Function != 0; thunk++, origThunk++) {
                if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)(base + origThunk->u1.AddressOfData);
                    if (strcmp((const char*)importByName->Name, "ChoosePixelFormat") == 0) {
                        DWORD oldProtect;
                        if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                            thunk->u1.Function = (DWORD)Hook_ChoosePixelFormat;
                            VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                            LogMsg("[winmm-pfd] Hooked ChoosePixelFormat via IAT at %p\n", &thunk->u1.Function);
                            chooseHooked = TRUE;
                        }
                    } else if (strcmp((const char*)importByName->Name, "SetPixelFormat") == 0) {
                        DWORD oldProtect;
                        if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                            thunk->u1.Function = (DWORD)Hook_SetPixelFormat;
                            VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                            LogMsg("[winmm-pfd] Hooked SetPixelFormat via IAT at %p\n", &thunk->u1.Function);
                            setHooked = TRUE;
                        }
                    }
                }
            }
            break;
        }
    }

    // Direct address fallback if needed
    if (!chooseHooked) {
        void** pDirect = (void**)0x00998064;
        DWORD oldProtect;
        if (VirtualProtect(pDirect, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            *pDirect = (void*)Hook_ChoosePixelFormat;
            VirtualProtect(pDirect, sizeof(void*), oldProtect, &oldProtect);
            LogMsg("[winmm-pfd] Hooked ChoosePixelFormat via direct address 0x00998064\n");
        }
    }
    if (!setHooked) {
        void** pDirect = (void**)0x0099804c;
        DWORD oldProtect;
        if (VirtualProtect(pDirect, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            *pDirect = (void*)Hook_SetPixelFormat;
            VirtualProtect(pDirect, sizeof(void*), oldProtect, &oldProtect);
            LogMsg("[winmm-pfd] Hooked SetPixelFormat via direct address 0x0099804c\n");
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_cs);
        QueryPerformanceFrequency(&g_qpc_freq);
        InitRealWinMM();
        InstallGDIHooks();
        LogMsg("[winmm-proxy] DllMain DLL_PROCESS_ATTACH, QPC Freq = %lld\n", g_qpc_freq.QuadPart);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&g_cs);
        if (g_log) {
            fclose(g_log);
            g_log = NULL;
        }
    }
    return TRUE;
}

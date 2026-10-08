#!/usr/bin/env python3
import os

with open("winmm_proxy/winmm_exports.txt") as f:
    exports = [line.strip() for line in f if line.strip()]

hooked = {
    "waveOutOpen",
    "waveOutWrite",
    "waveOutReset",
    "waveOutClose",
    "waveOutPause",
    "waveOutRestart",
    "waveOutGetPosition"
}

c_code = []
c_code.append("""/*
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
""")

# Pointers
for e in exports:
    c_code.append(f"static void* pReal_{e} = NULL;")

c_code.append("""
static void InitRealWinMM(void) {
    if (g_hRealWinMM) return;
    char sysDir[MAX_PATH];
    UINT len = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (len > 0 && len < MAX_PATH - 15) {
        strcat(sysDir, "\\\\winmm.dll");
        g_hRealWinMM = LoadLibraryA(sysDir);
    }
    if (!g_hRealWinMM) {
        g_hRealWinMM = LoadLibraryA("C:\\\\windows\\\\system32\\\\winmm.dll");
    }
    if (!g_hRealWinMM) {
        LogMsg("[winmm-proxy] ERROR: Failed to load real winmm.dll!\\n");
        return;
    }
    LogMsg("[winmm-proxy] Successfully hooked real winmm.dll\\n");
""")

for e in exports:
    c_code.append(f'    pReal_{e} = (void*)GetProcAddress(g_hRealWinMM, "{e}");')

c_code.append("}\n")

# Pass-through stubs
for e in exports:
    if e not in hooked:
        c_code.append(f"""__attribute__((naked)) void proxy_{e}() {{
    __asm__ volatile ("jmp *%0" : : "m"(pReal_{e}));
}}""")

# Hooked functions
c_code.append("""
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
        LogMsg("[winmm-proxy] waveOutOpen: rate=%u, channels=%u, blockAlign=%u\\n", 
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
    LogMsg("[winmm-proxy] waveOutClose\\n");
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
        LogMsg("[pll] #%03d: raw=%u, reported=%u, diff=%.1f\\n",
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

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_cs);
        QueryPerformanceFrequency(&g_qpc_freq);
        InitRealWinMM();
        LogMsg("[winmm-proxy] DllMain DLL_PROCESS_ATTACH, QPC Freq = %lld\\n", g_qpc_freq.QuadPart);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&g_cs);
        if (g_log) {
            fclose(g_log);
            g_log = NULL;
        }
    }
    return TRUE;
}
""")

with open("winmm_proxy/winmm.c", "w") as f:
    f.write("\n".join(c_code))

def_lines = ["EXPORTS"]
for e in exports:
    def_lines.append(f"    {e} = proxy_{e}")

with open("winmm_proxy/winmm.def", "w") as f:
    f.write("\n".join(def_lines) + "\n")

print("Generated winmm.c and winmm.def successfully with PLL servo clock!")

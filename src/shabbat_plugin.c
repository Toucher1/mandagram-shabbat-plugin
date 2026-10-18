#define _DEFAULT_SOURCE
#include "mandagram_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#ifndef MANDAGRAM_EXPORT
#define MANDAGRAM_EXPORT PLUGIN_EXPORT
#endif

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <process.h>
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
#endif

/*
 * Конфигурация времени Шаббата:
 * Пятница (wday = 5) начиная с 18:00
 */
#define SHABBAT_START_WDAY 5
#define SHABBAT_START_HOUR 18
#define SHABBAT_START_MIN  0

/*
 * Суббота (wday = 6) до 21:30
 */
#define SHABBAT_END_WDAY   6
#define SHABBAT_END_HOUR   21
#define SHABBAT_END_MIN    30

/*
 * Для быстрого тестирования: если раскомментировать строку ниже,
 * плагин будет считать, что Шаббат идет ВСЕГДА!
 */
/* #define FORCE_SHABBAT_DEBUG 1 */ 

static volatile bool s_running = true;

/* ========================================================================= */
/*                              ПРОВЕРКА ВРЕМЕНИ                             */
/* ========================================================================= */

static bool is_shabbat_now(void) {
#ifdef FORCE_SHABBAT_DEBUG
    return true; /* Тестовый режим — всегда включен Шаббат */
#endif

    time_t raw = time(NULL);
    struct tm* t = localtime(&raw);
    if (!t) return false;

    /* Пятница после 18:00 */
    if (t->tm_wday == SHABBAT_START_WDAY) {
        if (t->tm_hour > SHABBAT_START_HOUR || 
           (t->tm_hour == SHABBAT_START_HOUR && t->tm_min >= SHABBAT_START_MIN)) {
            return true;
        }
    }

    /* Суббота до 21:30 */
    if (t->tm_wday == SHABBAT_END_WDAY) {
        if (t->tm_hour < SHABBAT_END_HOUR || 
           (t->tm_hour == SHABBAT_END_HOUR && t->tm_min < SHABBAT_END_MIN)) {
            return true;
        }
    }

    return false;
}

/* ========================================================================= */
/*                          WINDOWS РЕАЛИЗАЦИЯ (Win32)                       */
/* ========================================================================= */
#ifdef _WIN32

static HWND s_main_hwnd = NULL;
static HWND s_overlay_hwnd = NULL;
static HANDLE s_thread = NULL;

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam && IsWindowVisible(hwnd)) {
        RECT r;
        GetClientRect(hwnd, &r);
        /* Находим главное окно Telegram/MandaGram по размеру */
        if (r.right > 400 && r.bottom > 300) {
            s_main_hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT r;
            GetClientRect(hwnd, &r);

            /* 1. Темный фон в стиле Material 3 Dark (#141419) */
            HBRUSH bg = CreateSolidBrush(RGB(20, 20, 25));
            FillRect(hdc, &r, bg);
            DeleteObject(bg);

            /* 2. Текст */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(240, 240, 245));

            HFONT font_title = CreateFontA(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            HFONT font_body = CreateFontA(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

            SelectObject(hdc, font_title);
            RECT rt = r;
            rt.bottom = r.bottom / 2 - 20;
            DrawTextA(hdc, "🕯️  Шаббат Шалом!  🕯️", -1, &rt, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);

            SelectObject(hdc, font_body);
            RECT rb = r;
            rb.top = r.bottom / 2 + 10;
            DrawTextA(hdc, "Интерфейс заблокирован до 21:30 субботы.\nОтложите суету, работу и мессенджеры. Время отдыхать!",
                      -1, &rb, DT_CENTER | DT_TOP);

            DeleteObject(font_title);
            DeleteObject(font_body);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            /* Блокируем любые попытки закрытия или хоткеев */
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void RegisterOverlayClass(void) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "MandaGramShabbatOverlay";
    wc.hCursor = LoadCursor(NULL, IDC_NO); /* Курсор "запрещено" */
    RegisterClassExA(&wc);
    registered = true;
}

static void ShowShabbatLock(void) {
    if (s_overlay_hwnd && IsWindow(s_overlay_hwnd)) return;

    if (!s_main_hwnd || !IsWindow(s_main_hwnd)) {
        EnumWindows(EnumWindowsCallback, (LPARAM)GetCurrentProcessId());
    }
    if (!s_main_hwnd) return;

    RegisterOverlayClass();

    RECT r;
    GetWindowRect(s_main_hwnd, &r);

    s_overlay_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "MandaGramShabbatOverlay",
        "Shabbat",
        WS_POPUP | WS_VISIBLE,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        s_main_hwnd, NULL, GetModuleHandle(NULL), NULL
    );

    /* Запрещаем любые действия в основном окне Telegram */
    EnableWindow(s_main_hwnd, FALSE);
}

static void HideShabbatLock(void) {
    if (s_overlay_hwnd && IsWindow(s_overlay_hwnd)) {
        DestroyWindow(s_overlay_hwnd);
        s_overlay_hwnd = NULL;
    }
    if (s_main_hwnd && IsWindow(s_main_hwnd)) {
        EnableWindow(s_main_hwnd, TRUE);
    }
}

static unsigned __stdcall EnforcerThread(void* arg) {
    (void)arg;
    while (s_running) {
        if (is_shabbat_now()) {
            ShowShabbatLock();
            if (s_overlay_hwnd && s_main_hwnd && IsWindow(s_main_hwnd)) {
                RECT r;
                GetWindowRect(s_main_hwnd, &r);
                SetWindowPos(s_overlay_hwnd, HWND_TOPMOST,
                             r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        } else {
            HideShabbatLock();
        }
        Sleep(500);
    }
    return 0;
}

#else
/* ========================================================================= */
/*                           LINUX РЕАЛИЗАЦИЯ (X11)                          */
/* ========================================================================= */

static pthread_t s_thread;
static Display* s_display = NULL;
static Window s_overlay_win = 0;
static bool s_is_locked = false;

static void ShowShabbatLockLinux(void) {
    if (s_is_locked) return;

    s_display = XOpenDisplay(NULL);
    if (!s_display) {
        s_is_locked = true;
        return;
    }

    int screen = DefaultScreen(s_display);
    int width = DisplayWidth(s_display, screen);
    int height = DisplayHeight(s_display, screen);
    Window root = RootWindow(s_display, screen);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True; /* Без рамок оконного менеджера и без кнопок */
    attrs.background_pixel = BlackPixel(s_display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    s_overlay_win = XCreateWindow(
        s_display, root, 0, 0, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs
    );

    XMapRaised(s_display, s_overlay_win);
    XGrabPointer(s_display, s_overlay_win, True, ButtonPressMask,
                 GrabModeAsync, GrabModeAsync, s_overlay_win, None, CurrentTime);
    XGrabKeyboard(s_display, s_overlay_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XFlush(s_display);

    s_is_locked = true;
}

static void HideShabbatLockLinux(void) {
    if (!s_is_locked) return;

    if (s_display) {
        XUngrabKeyboard(s_display, CurrentTime);
        XUngrabPointer(s_display, CurrentTime);
        if (s_overlay_win) {
            XDestroyWindow(s_display, s_overlay_win);
            s_overlay_win = 0;
        }
        XCloseDisplay(s_display);
        s_display = NULL;
    }
    s_is_locked = false;
}

static void* EnforcerThread(void* arg) {
    (void)arg;
    while (s_running) {
        if (is_shabbat_now()) {
            ShowShabbatLockLinux();
            if (s_display && s_overlay_win) {
                /* Отрисовка текста через X11 GC */
                XEvent ev;
                while (XPending(s_display)) {
                    XNextEvent(s_display, &ev);
                    if (ev.type == Expose) {
                        int screen = DefaultScreen(s_display);
                        GC gc = XCreateGC(s_display, s_overlay_win, 0, NULL);
                        XSetForeground(s_display, gc, WhitePixel(s_display, screen));
                        const char* text1 = "[SHABBAT SHALOM]";
                        const char* text2 = "Telegram is locked until Saturday 21:30.";
                        XDrawString(s_display, s_overlay_win, gc, 100, 100, text1, strlen(text1));
                        XDrawString(s_display, s_overlay_win, gc, 100, 130, text2, strlen(text2));
                        XFreeGC(s_display, gc);
                    }
                }
            }
        } else {
            HideShabbatLockLinux();
        }
        usleep(500000); /* 500 мс */
    }
    return NULL;
}

#endif

/* ========================================================================= */
/*                     TCL/TDLib ХУК ОТПРАВКИ СООБЩЕНИЙ                      */
/* ========================================================================= */

static const char* ShabbatMessageHook(int64_t chat_id, const char* text, uintptr_t user_data) {
    (void)chat_id;
    (void)text;
    (void)user_data;
    if (is_shabbat_now()) {
        /* В MandaGram MessageBeforeSendFn возвращает текст сообщения или NULL */
        return NULL; /* отменяет отправку */
    }
    return text;
}

/* ========================================================================= */
/*                          ЭКСПОРТИРУЕМЫЙ C ABI                             */
/* ========================================================================= */

static PluginMetadata s_meta = {
    .name = "Shabbat Enforcer",
    .version = "2.0.0",
    .author = "Toucher1",
    .description = "Блокирует интерфейс MandaGram и отправку сообщений в Шаббат без крашей"
};

MANDAGRAM_EXPORT const PluginMetadata* mandagram_plugin_metadata(void) {
    return &s_meta;
}

MANDAGRAM_EXPORT bool mandagram_plugin_init(const PluginApi* api) {
    if (api && api->log_info) {
        api->log_info("Shabbat", "Shabbat Enforcer v2.0 (Overlay Mode) успешно активирован.");
    }

    /* Регистрируем перехватчик отправки сообщений */
    if (api && api->register_message_send_hook) {
        api->register_message_send_hook(ShabbatMessageHook, NULL, 0);
    }

    s_running = true;

#ifdef _WIN32
    s_thread = (HANDLE)_beginthreadex(NULL, 0, EnforcerThread, NULL, 0, NULL);
#else
    pthread_create(&s_thread, NULL, EnforcerThread, NULL);
#endif

    return true;
}

MANDAGRAM_EXPORT void mandagram_plugin_deinit(void) {
    s_running = false;

#ifdef _WIN32
    HideShabbatLock();
    if (s_thread) {
        WaitForSingleObject(s_thread, 1000);
        CloseHandle(s_thread);
        s_thread = NULL;
    }
#else
    HideShabbatLockLinux();
    pthread_join(s_thread, NULL);
#endif
}

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 500
#include "mandagram_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdatomic.h>

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
  #include <locale.h>
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
  #include <X11/Xatom.h>
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

static atomic_bool s_running = ATOMIC_VAR_INIT(true);

/* ========================================================================= */
/*                              ПРОВЕРКА ВРЕМЕНИ                             */
/* ========================================================================= */

static bool is_shabbat_now(void) {
#ifdef FORCE_SHABBAT_DEBUG
    return true; /* Тестовый режим — всегда включен Шаббат */
#endif

    time_t raw = time(NULL);
    if (raw == (time_t)-1) return false;

    struct tm t;
#ifdef _WIN32
    if (localtime_s(&t, &raw) != 0) return false;
#else
    if (!localtime_r(&raw, &t)) return false;
#endif

    /* Пятница после 18:00 */
    if (t.tm_wday == SHABBAT_START_WDAY) {
        if (t.tm_hour > SHABBAT_START_HOUR || 
           (t.tm_hour == SHABBAT_START_HOUR && t.tm_min >= SHABBAT_START_MIN)) {
            return true;
        }
    }

    /* Суббота до 21:30 */
    if (t.tm_wday == SHABBAT_END_WDAY) {
        if (t.tm_hour < SHABBAT_END_HOUR || 
           (t.tm_hour == SHABBAT_END_HOUR && t.tm_min < SHABBAT_END_MIN)) {
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
static unsigned s_thread_id = 0;
static const wchar_t* const OVERLAY_CLASS_NAME = L"MandaGramShabbatOverlay";
static bool s_class_registered = false;
static HINSTANCE s_module_instance = NULL;

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam && IsWindowVisible(hwnd) && hwnd != s_overlay_hwnd) {
        /* Фильтруем дочерние окна и вспомогательные тулбары */
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((style & WS_CHILD) == 0 && GetWindow(hwnd, GW_OWNER) == NULL) {
            RECT r;
            GetClientRect(hwnd, &r);
            if (r.right > 400 && r.bottom > 300) {
                s_main_hwnd = hwnd;
                return FALSE;
            }
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

            /* 2. Текст с полной поддержкой Unicode (шрифты Segoe UI) */
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(240, 240, 245));

            HFONT font_title = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT font_body = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            HFONT old_font = (HFONT)SelectObject(hdc, font_title);
            RECT rt = r;
            rt.bottom = r.bottom / 2 - 20;
            DrawTextW(hdc, L"🕯️  Шаббат Шалом!  🕯️", -1, &rt, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);

            SelectObject(hdc, font_body);
            RECT rb = r;
            rb.top = r.bottom / 2 + 10;
            DrawTextW(hdc, L"Интерфейс заблокирован до 21:30 субботы.\nОтложите суету, работу и мессенджеры. Время отдыхать!",
                      -1, &rb, DT_CENTER | DT_TOP | DT_WORDBREAK);

            /* Восстанавливаем оригинальный объект GDI во избежание утечек дескрипторов */
            SelectObject(hdc, old_font);
            DeleteObject(font_title);
            DeleteObject(font_body);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; /* Предотвращаем мерцание */
        case WM_CLOSE:
            /* Закрытие окна оверлея из потока-владельца */
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            s_overlay_hwnd = NULL;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            /* Блокируем любые попытки закрытия или хоткеев */
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterOverlayClass(void) {
    if (s_class_registered) return;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = s_module_instance ? s_module_instance : GetModuleHandleW(NULL);
    wc.lpszClassName = OVERLAY_CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_NO); /* Курсор "запрещено" */
    RegisterClassExW(&wc);
    s_class_registered = true;
}

static void UnregisterOverlayClass(void) {
    if (s_class_registered) {
        UnregisterClassW(OVERLAY_CLASS_NAME, s_module_instance ? s_module_instance : GetModuleHandleW(NULL));
        s_class_registered = false;
    }
}

static void ShowShabbatLock(void) {
    if (s_overlay_hwnd && IsWindow(s_overlay_hwnd)) return;

    if (!s_main_hwnd || !IsWindow(s_main_hwnd)) {
        EnumWindows(EnumWindowsCallback, (LPARAM)GetCurrentProcessId());
    }
    if (!s_main_hwnd || !IsWindow(s_main_hwnd)) return;

    RegisterOverlayClass();

    RECT r;
    GetWindowRect(s_main_hwnd, &r);

    s_overlay_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS_NAME,
        L"Shabbat",
        WS_POPUP | WS_VISIBLE,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        s_main_hwnd, NULL, s_module_instance ? s_module_instance : GetModuleHandleW(NULL), NULL
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
    RegisterOverlayClass();

    while (atomic_load(&s_running)) {
        if (is_shabbat_now()) {
            ShowShabbatLock();
            if (s_overlay_hwnd && s_main_hwnd && IsWindow(s_main_hwnd) && !IsIconic(s_main_hwnd)) {
                RECT r;
                GetWindowRect(s_main_hwnd, &r);
                SetWindowPos(s_overlay_hwnd, HWND_TOPMOST,
                             r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        } else {
            HideShabbatLock();
        }

        /* Выборка и обработка сообщений потока во избежание зависания оверлея */
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        /* Ожидаем события либо тика таймера (500 мс во время блокировки, 1000 мс в покое) */
        DWORD wait_ms = (s_overlay_hwnd != NULL) ? 500 : 1000;
        MsgWaitForMultipleObjects(0, NULL, FALSE, wait_ms, QS_ALLINPUT);
    }

    HideShabbatLock();
    UnregisterOverlayClass();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        s_module_instance = hinstDLL;
    }
    return TRUE;
}

#else
/* ========================================================================= */
/*                           LINUX РЕАЛИЗАЦИЯ (X11)                          */
/* ========================================================================= */

static pthread_t s_thread;
static bool s_thread_started = false;
static Display* s_display = NULL;
static Window s_overlay_win = 0;
static bool s_is_locked = false;

/* Поиск окна MandaGram по атрибуту _NET_WM_PID */
static Window find_app_window(Display* dpy, Window root, pid_t pid) {
    Atom atom_pid = XInternAtom(dpy, "_NET_WM_PID", True);
    if (atom_pid == None) return 0;

    Window root_ret, parent_ret;
    Window* children = NULL;
    unsigned int nchildren = 0;
    Window target = 0;

    if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren) && children) {
        for (int i = (int)nchildren - 1; i >= 0; --i) {
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char* prop = NULL;
            if (XGetWindowProperty(dpy, children[i], atom_pid, 0, 1, False,
                                   XA_CARDINAL, &actual_type, &actual_format,
                                   &nitems, &bytes_after, &prop) == Success && prop) {
                pid_t win_pid = *(pid_t*)prop;
                XFree(prop);
                if (win_pid == pid) {
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(dpy, children[i], &attrs) &&
                        attrs.map_state == IsViewable &&
                        attrs.width > 300 && attrs.height > 200) {
                        target = children[i];
                        break;
                    }
                }
            }
        }
        XFree(children);
    }
    return target;
}

static void draw_overlay_text(Display* dpy, Window win, int width, int height) {
    int screen = DefaultScreen(dpy);
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));

    char** missing_charset_list = NULL;
    int missing_charset_count = 0;
    char* def_string = NULL;
    XFontSet fontset = XCreateFontSet(dpy,
        "-*-*-bold-r-*-*-24-*-*-*-*-*-*-*,-*-*-medium-r-*-*-16-*-*-*-*-*-*-*,*",
        &missing_charset_list, &missing_charset_count, &def_string);

    if (missing_charset_list) {
        XFreeStringList(missing_charset_list);
    }

    const char* text_title = "🕯️  Шаббат Шалом!  🕯️";
    const char* text_line1 = "Интерфейс заблокирован до 21:30 субботы.";
    const char* text_line2 = "Отложите суету, работу и мессенджеры. Время отдыхать!";

    if (fontset) {
        XRectangle ink, log;
        Xutf8TextExtents(fontset, text_title, (int)strlen(text_title), &ink, &log);
        int x = (width - log.width) / 2;
        if (x < 20) x = 20;
        int y = height / 2 - 20;
        Xutf8DrawString(dpy, win, fontset, gc, x, y, text_title, (int)strlen(text_title));

        Xutf8TextExtents(fontset, text_line1, (int)strlen(text_line1), &ink, &log);
        x = (width - log.width) / 2;
        if (x < 20) x = 20;
        y = height / 2 + 15;
        Xutf8DrawString(dpy, win, fontset, gc, x, y, text_line1, (int)strlen(text_line1));

        Xutf8TextExtents(fontset, text_line2, (int)strlen(text_line2), &ink, &log);
        x = (width - log.width) / 2;
        if (x < 20) x = 20;
        y = height / 2 + 45;
        Xutf8DrawString(dpy, win, fontset, gc, x, y, text_line2, (int)strlen(text_line2));

        XFreeFontSet(dpy, fontset);
    } else {
        const char* fb_title = "[SHABBAT SHALOM]";
        const char* fb_sub = "Telegram is locked until Saturday 21:30.";
        XDrawString(dpy, win, gc, width / 2 - 70, height / 2 - 10, fb_title, (int)strlen(fb_title));
        XDrawString(dpy, win, gc, width / 2 - 140, height / 2 + 20, fb_sub, (int)strlen(fb_sub));
    }

    XFreeGC(dpy, gc);
    XFlush(dpy);
}

static void ShowShabbatLockLinux(void) {
    if (s_is_locked || !s_display) return;

    int screen = DefaultScreen(s_display);
    Window root = RootWindow(s_display, screen);
    Window target = find_app_window(s_display, root, getpid());

    int x = 0, y = 0;
    int width = DisplayWidth(s_display, screen);
    int height = DisplayHeight(s_display, screen);

    if (target != 0) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(s_display, target, &wa)) {
            Window child;
            XTranslateCoordinates(s_display, target, root, 0, 0, &x, &y, &child);
            width = wa.width;
            height = wa.height;
        }
    }

    XSetWindowAttributes attrs;
    attrs.override_redirect = True; /* Без рамок оконного менеджера */
    attrs.background_pixel = BlackPixel(s_display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;

    s_overlay_win = XCreateWindow(
        s_display, root, x, y, (unsigned int)width, (unsigned int)height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs
    );

    XMapRaised(s_display, s_overlay_win);
    XFlush(s_display);

    s_is_locked = true;
}

static void HideShabbatLockLinux(void) {
    if (!s_is_locked) return;

    if (s_display && s_overlay_win) {
        XDestroyWindow(s_display, s_overlay_win);
        XFlush(s_display);
        s_overlay_win = 0;
    }
    s_is_locked = false;
}

static void* EnforcerThread(void* arg) {
    (void)arg;

    s_display = XOpenDisplay(NULL);
    if (!s_display) {
        /* X11 недоступен (Wayland / headless) — хук сообщений TDLib продолжает работать */
        while (atomic_load(&s_running)) {
            usleep(1000000);
        }
        return NULL;
    }

    while (atomic_load(&s_running)) {
        if (is_shabbat_now()) {
            ShowShabbatLockLinux();
            if (s_display && s_overlay_win) {
                XEvent ev;
                while (XPending(s_display)) {
                    XNextEvent(s_display, &ev);
                    if (ev.type == Expose) {
                        XWindowAttributes wa;
                        XGetWindowAttributes(s_display, s_overlay_win, &wa);
                        draw_overlay_text(s_display, s_overlay_win, wa.width, wa.height);
                    }
                }
            }
        } else {
            HideShabbatLockLinux();
        }
        usleep(500000); /* 500 мс */
    }

    HideShabbatLockLinux();
    if (s_display) {
        XCloseDisplay(s_display);
        s_display = NULL;
    }
    return NULL;
}

#endif

/* ========================================================================= */
/*                     TDLib / FFI ХУК ОТПРАВКИ СООБЩЕНИЙ                   */
/* ========================================================================= */

static const char* ShabbatMessageHook(int64_t chat_id, const char* text, uintptr_t user_data) {
    (void)chat_id;
    (void)user_data;
    if (is_shabbat_now()) {
        /* В MandaGram MessageBeforeSendFn возвращает текст сообщения или NULL для отмены отправки */
        return NULL;
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

    atomic_store(&s_running, true);

#ifdef _WIN32
    s_thread = (HANDLE)_beginthreadex(NULL, 0, EnforcerThread, NULL, 0, &s_thread_id);
#else
    setlocale(LC_ALL, "");
    XInitThreads();
    if (pthread_create(&s_thread, NULL, EnforcerThread, NULL) == 0) {
        s_thread_started = true;
    }
#endif

    return true;
}

MANDAGRAM_EXPORT void mandagram_plugin_deinit(void) {
    atomic_store(&s_running, false);

#ifdef _WIN32
    if (s_overlay_hwnd && IsWindow(s_overlay_hwnd)) {
        PostMessageW(s_overlay_hwnd, WM_CLOSE, 0, 0);
    }
    if (s_thread) {
        WaitForSingleObject(s_thread, 2000);
        CloseHandle(s_thread);
        s_thread = NULL;
    }
#else
    if (s_thread_started) {
        pthread_join(s_thread, NULL);
        s_thread_started = false;
    }
#endif
}

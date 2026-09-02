/*
 * console.c - Modern cross-platform console implementation for x64 systems
 *
 * This file contains the implementation of the console module providing
 * cross-platform console interface with ANSI escape code support.
 *
 * Features implemented:
 *   - Windows Virtual Terminal Processing initialization
 *   - Cross-platform ANSI escape sequence handling
 *   - RGB color support for modern terminals
 *   - Terminal detection capabilities
 *
 * Dependencies:
 *   - console/console.h
 *   - Platform-specific headers (windows.h, unistd.h)
 *
 * See console.h for API documentation.
 */

#include <console/console.h>

#ifdef OS_WINDOWS
#include <platform/windows/windows.h>
#include <io.h>
static HANDLE   _console                    = nullptr;
static HANDLE   _console_input              = nullptr;
static DWORD    _mode_original              = 0;
static DWORD    _input_mode_original        = 0;
static UINT     _output_code_page_original  = 0;
static UINT     _input_code_page_original   = 0;
static bool     _virtual_terminal_enabled   = false;
static bool     _raw_mode_enabled           = false;
static bool     _restore_done               = false;
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
static struct termios   _termios_original   = DEFAULT_INITIALIZATION;
static bool             _raw_mode_enabled   = false;
static bool             _restore_done       = false;
#endif

bool console_init(void) {
#ifdef OS_WINDOWS
    _console = GetStdHandle(STD_OUTPUT_HANDLE);

    if (_console == INVALID_HANDLE_VALUE || _console == nullptr) {
        return false;
    }

    // Get current console mode
    if (!GetConsoleMode(_console, &_mode_original)) {
        return false;
    }

    // Enable Virtual Terminal Processing for ANSI support
    DWORD const new_mode = _mode_original | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (!SetConsoleMode(_console, new_mode)) {
        return false; // VT processing not supported (pre-Win10)
    }

    _virtual_terminal_enabled = true;

    // Windows consoles default to an OEM code page, so UTF-8 output (box-drawing glyphs, wide
    // characters) renders as mojibake; switch the console to UTF-8 for the session and restore
    // the original code pages on uninit.
    _output_code_page_original = GetConsoleOutputCP();
    _input_code_page_original = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    return true;
#else
    // On Unix-like systems, ANSI is supported by default
    // Just check if stdout is a terminal
    return isatty(STDOUT_FILENO);
#endif
}

void console_uninit(void) {
    if (_raw_mode_enabled) {
        console_raw_mode_leave();
    }

#ifdef OS_WINDOWS
    if (_virtual_terminal_enabled && _console != INVALID_HANDLE_VALUE) {
        SetConsoleMode(_console, _mode_original); // Restore original mode
    }

    // Restore the code pages saved in console_init (0 means init never set them).
    if (_output_code_page_original != 0) {
        SetConsoleOutputCP(_output_code_page_original);
        SetConsoleCP(_input_code_page_original);
    }
#endif

    fflush(stdout);
}

void console_restore_all(void) {
    // Idempotent: an abort partway through a frame and a normal exit both land here, and atexit
    // may run it after the program already tore itself down cleanly.
    if (_restore_done) {
        return;
    }

    _restore_done = true;

    console_paste_disable();
    console_mouse_disable();
    console_focus_disable();
    console_keyboard_enhance_disable();
    console_raw_mode_leave();
    console_cursor_show();
    console_alt_screen_leave();
    console_flush();
    console_uninit();
}

void console_foreground_rgb(U8 const r, U8 const g, U8 const b) {
    printf("\x1b[38;2;%d;%d;%dm", r, g, b);
}

void console_background_rgb(U8 const r, U8 const g, U8 const b) {
    printf("\x1b[48;2;%d;%d;%dm", r, g, b);
}

void console_foreground_256(U8 const color_index) {
    printf("\x1b[38;5;%dm", color_index);
}

void console_background_256(U8 const color_index) {
    printf("\x1b[48;5;%dm", color_index);
}

void console_format_bold(void) {
    printf("\x1b[1m");
}

void console_format_dim(void) {
    printf("\x1b[2m");
}

void console_format_italic(void) {
    printf("\x1b[3m");
}

void console_format_underline(void) {
    printf("\x1b[4m");
}

void console_format_clear(void) {
    printf("\x1b[0m");
}

void console_cursor_position(U16 const row, U16 const col) {
    printf("\x1b[%d;%dH", row, col);
}

void console_cursor_hide(void) {
    printf("\x1b[?25l");
}

void console_cursor_show(void) {
    printf("\x1b[?25h");
}

void console_cursor_up(U16 const value) {
    printf("\x1b[%dA", value);
}

void console_cursor_down(U16 const value) {
    printf("\x1b[%dB", value);
}

void console_cursor_forward(U16 const value) {
    printf("\x1b[%dC", value);
}

void console_cursor_backward(U16 const value) {
    printf("\x1b[%dD", value);
}

void console_clear_screen(void) {
    printf("\x1b[2J\x1b[H");
}

void console_clear_line(void) {
    printf("\x1b[2K");
}

void console_save_cursor_pos(void) {
    printf("\x1b[s");
}

void console_restore_cursor_pos(void) {
    printf("\x1b[u");
}

void console_alt_screen_enter(void) {
    fputs(CONSOLE_ALT_SCREEN_ENTER, stdout);
}

void console_alt_screen_leave(void) {
    fputs(CONSOLE_ALT_SCREEN_LEAVE, stdout);
}

bool console_raw_mode_enter(void) {
    // Guard re-entry: capture the original terminal state only once per enter/leave
    // cycle, or a second enter would save the already-raw mode as the "original".
    if (_raw_mode_enabled) {
        return true;
    }

#ifdef OS_WINDOWS
    _console_input = GetStdHandle(STD_INPUT_HANDLE);

    if (_console_input == INVALID_HANDLE_VALUE || _console_input == nullptr) {
        return false;
    }

    if (!GetConsoleMode(_console_input, &_input_mode_original)) {
        return false;
    }

    // Disable line buffering, echo, Ctrl-C/Ctrl-Z signal cooking, and QuickEdit selection (which
    // would otherwise swallow mouse events); enable VT input plus mouse reporting so keys and SGR
    // mouse arrive as escape sequences a TUI can parse. ENABLE_EXTENDED_FLAGS is required for the
    // console to honour the cleared QuickEdit bit.
    DWORD const new_mode = (_input_mode_original
        & ~(DWORD) (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE))
        | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;

    if (!SetConsoleMode(_console_input, new_mode)) {
        return false;
    }

    _raw_mode_enabled = true;

    return true;
#else
    if (tcgetattr(STDIN_FILENO, &_termios_original) != 0) {
        return false;
    }

    struct termios raw = _termios_original;

    // Standard cfmakeraw-equivalent clears: no echo, no canonical mode, no signals,
    // no CR/NL translation, 1-byte reads with no timeout.
    raw.c_iflag &= ~(tcflag_t) (BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t) (OPOST);
    raw.c_lflag &= ~(tcflag_t) (ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return false;
    }

    _raw_mode_enabled = true;

    return true;
#endif
}

void console_raw_mode_leave(void) {
#ifdef OS_WINDOWS
    if (_raw_mode_enabled && _console_input != INVALID_HANDLE_VALUE) {
        SetConsoleMode(_console_input, _input_mode_original);
    }
#else
    if (_raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &_termios_original);
    }
#endif

    _raw_mode_enabled = false;
}

bool console_size(U16 *const cols, U16 *const rows) {
    if (cols == nullptr || rows == nullptr) {
        return false;
    }

#ifdef OS_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO info = DEFAULT_INITIALIZATION;
    HANDLE const out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (out == INVALID_HANDLE_VALUE || out == nullptr || !GetConsoleScreenBufferInfo(out, &info)) {
        return false;
    }

    // srWindow edges are signed SHORT; compute the span in int and reject a
    // non-positive result (a transient/resizing buffer) before the U16 cast.
    I32 const width  = info.srWindow.Right  - info.srWindow.Left + 1;
    I32 const height = info.srWindow.Bottom - info.srWindow.Top  + 1;

    if (width < 1 || height < 1) {
        return false;
    }

    *cols = (U16) width;
    *rows = (U16) height;

    return true;
#else
    struct winsize ws = DEFAULT_INITIALIZATION;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        return false;
    }

    *cols = (U16) ws.ws_col;
    *rows = (U16) ws.ws_row;

    return true;
#endif
}

void console_write(char const *const data, USize const size) {
    if (data == nullptr || size == 0) {
        return;
    }

    fwrite(data, 1, size, stdout);
}

bool console_input_available(U32 const timeout_ms) {
#ifdef OS_WINDOWS
    HANDLE const input = _console_input != nullptr ? _console_input : GetStdHandle(STD_INPUT_HANDLE);

    if (input == INVALID_HANDLE_VALUE || input == nullptr) {
        return false;
    }

    return WaitForSingleObject(input, (DWORD) timeout_ms) == WAIT_OBJECT_0;
#else
    struct pollfd descriptor = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };

    // Clamp to INT_MAX: poll() takes a signed int and treats a negative timeout as "block
    // forever", so a huge U32 must not wrap into an unintended infinite wait.
    int const wait = timeout_ms > (U32) 2147483647 ? 2147483647 : (int) timeout_ms;

    return poll(&descriptor, 1, wait) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
}

USize console_read(char *const buffer, USize const size) {
    if (buffer == nullptr || size == 0) {
        return 0;
    }

#ifdef OS_WINDOWS
    HANDLE const input = _console_input != nullptr ? _console_input : GetStdHandle(STD_INPUT_HANDLE);

    if (input == INVALID_HANDLE_VALUE || input == nullptr) {
        return 0;
    }

    DWORD count = 0;

    if (!ReadFile(input, buffer, (DWORD) size, &count, nullptr)) {
        return 0;
    }

    return (USize) count;
#else
    ISize const count = read(STDIN_FILENO, buffer, size);

    return count > 0 ? (USize) count : 0;
#endif
}

void console_focus_enable(void) {
    fputs("\x1b[?1004h", stdout);
}

void console_focus_disable(void) {
    fputs("\x1b[?1004l", stdout);
}

void console_keyboard_enhance_enable(void) {
    fputs("\x1b[>1u", stdout);
}

void console_keyboard_enhance_disable(void) {
    fputs("\x1b[<u", stdout);
}

void console_mouse_enable(void) {
    fputs("\x1b[?1000;1002;1006h", stdout);
}

void console_mouse_disable(void) {
    fputs("\x1b[?1000;1002;1006l", stdout);
}

void console_paste_enable(void) {
    fputs("\x1b[?2004h", stdout);
}

void console_paste_disable(void) {
    fputs("\x1b[?2004l", stdout);
}

void console_flush(void) {
    fflush(stdout);
}

bool console_is_terminal(FILE *const stream) {
#ifdef OS_WINDOWS
    return _isatty(_fileno(stream));
#else
    return isatty(fileno(stream));
#endif
}
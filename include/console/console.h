/*
 * console.h - Cross-platform terminal library for x64 systems
 *
 * Features:
 *   - ANSI escape sequences for color (16 / 256 / 24-bit RGB), text attributes, cursor
 *     movement, and screen/line erase — exposed both as string constants and as helpers.
 *   - Automatic Windows Virtual Terminal Processing initialization; ANSI works on Win10+
 *     and modern Linux/macOS terminals.
 *   - Full-screen TUI backend surface: raw input mode (console_raw_mode_enter/leave),
 *     terminal size query (console_size), alternate screen buffer
 *     (console_alt_screen_enter/leave), and one bulk write (console_write).
 *   - Terminal input: a raw event byte stream (console_input_available / console_read) with
 *     mode toggles for SGR mouse, bracketed paste, focus reporting, and the keyboard
 *     enhancement (kitty) protocol.
 *   - Cross-platform: the Windows console API and POSIX termios/ioctl behind one interface.
 *
 * Usage Examples:
 *   @code
 *   // Inline styling on the primary screen.
 *   console_init();
 *   console_foreground_rgb(255, 0, 0);
 *   printf("Hello World\n");
 *   console_format_clear();
 *   console_uninit();
 *   @endcode
 *
 *   @code
 *   // Full-screen TUI backend: raw mode + alternate screen, restored on exit.
 *   console_init();
 *   console_raw_mode_enter();
 *   console_alt_screen_enter();
 *   console_cursor_hide();
 *   U16 cols = 0, rows = 0;
 *   console_size(&cols, &rows);
 *   console_write(frame_bytes, frame_size);
 *   console_flush();
 *   console_cursor_show();
 *   console_alt_screen_leave();
 *   console_uninit();
 *   @endcode
 *
 * Error Handling:
 *   - console_init(), console_raw_mode_enter(), and console_size() return false on
 *     failure; the remaining functions emit escape sequences and assume a valid state.
 *   - console_raw_mode_enter() is re-entrant-safe: a second call while already in raw
 *     mode is a no-op, so the original terminal state is captured only once.
 *
 * Thread Safety:
 *   - Not thread-safe: every function writes the shared stdout / process console; wrap
 *     calls with a mutex if used from multiple threads.
 *
 * Memory Management:
 *   - No allocation is performed; functions emit escape sequences or copy caller bytes
 *     straight to stdout.
 *
 * Performance Characteristics:
 *   - Each call is a single buffered write to stdout; console_write forwards one bulk
 *     buffer, the intended path for writing a whole frame at once.
 *
 * Dependencies:
 *   - <stdio.h> for stdout and FILE.
 *   - <types.h> for the framework integer types.
 *   - Platform terminal control is implemented in console.c (the Windows console API on
 *     Windows; <termios.h>/<sys/ioctl.h> on POSIX); no platform header leaks through here.
 *
 * Note:
 *   - Call console_init() before any console function; call console_uninit() when done
 *     to restore the original console mode (it also leaves raw mode if still enabled).
 *   - Pair console_alt_screen_enter() / console_raw_mode_enter() with their leave calls
 *     (or console_uninit) so the user's terminal is restored on exit.
 *
 * See console.c for implementation details.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <stdio.h>

#include <types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

// --- Color Sequences (Foreground, Alphabetical) ---
/**
 * @def CONSOLE_COLOR_BLACK
 * @brief ANSI escape sequence for black foreground color.
 */
#define CONSOLE_COLOR_BLACK "\x1b[30m"

/**
 * @def CONSOLE_COLOR_BLUE
 * @brief ANSI escape sequence for blue foreground color.
 */
#define CONSOLE_COLOR_BLUE "\x1b[34m"

/**
 * @def CONSOLE_COLOR_CYAN
 * @brief ANSI escape sequence for cyan foreground color.
 */
#define CONSOLE_COLOR_CYAN "\x1b[36m"

/**
 * @def CONSOLE_COLOR_GREEN
 * @brief ANSI escape sequence for green foreground color.
 */
#define CONSOLE_COLOR_GREEN "\x1b[32m"

/**
 * @def CONSOLE_COLOR_MAGENTA
 * @brief ANSI escape sequence for magenta foreground color.
 */
#define CONSOLE_COLOR_MAGENTA "\x1b[35m"

/**
 * @def CONSOLE_COLOR_RED
 * @brief ANSI escape sequence for red foreground color.
 */
#define CONSOLE_COLOR_RED "\x1b[31m"

/**
 * @def CONSOLE_COLOR_WHITE
 * @brief ANSI escape sequence for white foreground color.
 */
#define CONSOLE_COLOR_WHITE "\x1b[37m"

/**
 * @def CONSOLE_COLOR_YELLOW
 * @brief ANSI escape sequence for yellow foreground color.
 */
#define CONSOLE_COLOR_YELLOW "\x1b[33m"

// --- Bright Color Sequences (Foreground, Alphabetical) ---
/**
 * @def CONSOLE_COLOR_BRIGHT_BLACK
 * @brief ANSI escape sequence for bright black foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_BLACK "\x1b[90m"

/**
 * @def CONSOLE_COLOR_BRIGHT_BLUE
 * @brief ANSI escape sequence for bright blue foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_BLUE "\x1b[94m"

/**
 * @def CONSOLE_COLOR_BRIGHT_CYAN
 * @brief ANSI escape sequence for bright cyan foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_CYAN "\x1b[96m"

/**
 * @def CONSOLE_COLOR_BRIGHT_GREEN
 * @brief ANSI escape sequence for bright green foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_GREEN "\x1b[92m"

/**
 * @def CONSOLE_COLOR_BRIGHT_MAGENTA
 * @brief ANSI escape sequence for bright magenta foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_MAGENTA "\x1b[95m"

/**
 * @def CONSOLE_COLOR_BRIGHT_RED
 * @brief ANSI escape sequence for bright red foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_RED "\x1b[91m"

/**
 * @def CONSOLE_COLOR_BRIGHT_WHITE
 * @brief ANSI escape sequence for bright white foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_WHITE "\x1b[97m"

/**
 * @def CONSOLE_COLOR_BRIGHT_YELLOW
 * @brief ANSI escape sequence for bright yellow foreground color.
 */
#define CONSOLE_COLOR_BRIGHT_YELLOW "\x1b[93m"

// --- Background Color Sequences (Alphabetical) ---
/**
 * @def CONSOLE_BACKGROUND_BLACK
 * @brief ANSI escape sequence for black background color.
 */
#define CONSOLE_BACKGROUND_BLACK "\x1b[40m"

/**
 * @def CONSOLE_BACKGROUND_BLUE
 * @brief ANSI escape sequence for blue background color.
 */
#define CONSOLE_BACKGROUND_BLUE "\x1b[44m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_BLACK
 * @brief ANSI escape sequence for bright black background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_BLACK "\x1b[100m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_BLUE
 * @brief ANSI escape sequence for bright blue background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_BLUE "\x1b[104m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_CYAN
 * @brief ANSI escape sequence for bright cyan background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_CYAN "\x1b[106m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_GREEN
 * @brief ANSI escape sequence for bright green background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_GREEN "\x1b[102m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_MAGENTA
 * @brief ANSI escape sequence for bright magenta background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_MAGENTA "\x1b[105m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_RED
 * @brief ANSI escape sequence for bright red background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_RED "\x1b[101m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_WHITE
 * @brief ANSI escape sequence for bright white background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_WHITE "\x1b[107m"

/**
 * @def CONSOLE_BACKGROUND_BRIGHT_YELLOW
 * @brief ANSI escape sequence for bright yellow background color.
 */
#define CONSOLE_BACKGROUND_BRIGHT_YELLOW "\x1b[103m"

/**
 * @def CONSOLE_BACKGROUND_CYAN
 * @brief ANSI escape sequence for cyan background color.
 */
#define CONSOLE_BACKGROUND_CYAN "\x1b[46m"

/**
 * @def CONSOLE_BACKGROUND_DEFAULT
 * @brief ANSI escape sequence for default background color.
 */
#define CONSOLE_BACKGROUND_DEFAULT "\x1b[49m"

/**
 * @def CONSOLE_BACKGROUND_GREEN
 * @brief ANSI escape sequence for green background color.
 */
#define CONSOLE_BACKGROUND_GREEN "\x1b[42m"

/**
 * @def CONSOLE_BACKGROUND_MAGENTA
 * @brief ANSI escape sequence for magenta background color.
 */
#define CONSOLE_BACKGROUND_MAGENTA "\x1b[45m"

/**
 * @def CONSOLE_BACKGROUND_RED
 * @brief ANSI escape sequence for red background color.
 */
#define CONSOLE_BACKGROUND_RED "\x1b[41m"

/**
 * @def CONSOLE_BACKGROUND_WHITE
 * @brief ANSI escape sequence for white background color.
 */
#define CONSOLE_BACKGROUND_WHITE "\x1b[47m"

/**
 * @def CONSOLE_BACKGROUND_YELLOW
 * @brief ANSI escape sequence for yellow background color.
 */
#define CONSOLE_BACKGROUND_YELLOW "\x1b[43m"

// --- Formatting Sequences (Alphabetical) ---
/**
 * @def CONSOLE_BLINK
 * @brief ANSI escape sequence for blinking text.
 */
#define CONSOLE_BLINK "\x1b[5m"

/**
 * @def CONSOLE_BOLD
 * @brief ANSI escape sequence for bold text.
 */
#define CONSOLE_BOLD "\x1b[1m"

/**
 * @def CONSOLE_DIM
 * @brief ANSI escape sequence for dim text.
 */
#define CONSOLE_DIM "\x1b[2m"

/**
 * @def CONSOLE_FORMAT_RESET
 * @brief ANSI escape sequence to reset all formatting.
 */
#define CONSOLE_FORMAT_RESET "\x1b[0m"

/**
 * @def CONSOLE_HIDDEN
 * @brief ANSI escape sequence for hidden text.
 */
#define CONSOLE_HIDDEN "\x1b[8m"

/**
 * @def CONSOLE_ITALIC
 * @brief ANSI escape sequence for italic text.
 */
#define CONSOLE_ITALIC "\x1b[3m"

/**
 * @def CONSOLE_REVERSED
 * @brief ANSI escape sequence for reversed colors.
 */
#define CONSOLE_REVERSED "\x1b[7m"

/**
 * @def CONSOLE_STRIKETHROUGH
 * @brief ANSI escape sequence for strikethrough text.
 */
#define CONSOLE_STRIKETHROUGH "\x1b[9m"

/**
 * @def CONSOLE_UNDERLINE
 * @brief ANSI escape sequence for underlined text.
 */
#define CONSOLE_UNDERLINE "\x1b[4m"

// --- Cursor Control Sequences (Alphabetical) ---
/**
 * @def CONSOLE_CURSOR_BACK
 * @brief ANSI escape sequence moving the cursor n columns left.
 * @param n Number of columns to move.
 */
#define CONSOLE_CURSOR_BACK(n)              "\x1b[" #n "D"

/**
 * @def CONSOLE_CURSOR_COLUMN
 * @brief ANSI escape sequence moving the cursor to absolute column n (1-based).
 * @param n Target column.
 */
#define CONSOLE_CURSOR_COLUMN(n)            "\x1b[" #n "G"

/**
 * @def CONSOLE_CURSOR_DOWN
 * @brief ANSI escape sequence moving the cursor n rows down.
 * @param n Number of rows to move.
 */
#define CONSOLE_CURSOR_DOWN(n)              "\x1b[" #n "B"

/**
 * @def CONSOLE_CURSOR_FORWARD
 * @brief ANSI escape sequence moving the cursor n columns right.
 * @param n Number of columns to move.
 */
#define CONSOLE_CURSOR_FORWARD(n)           "\x1b[" #n "C"

/**
 * @def CONSOLE_CURSOR_NEXT_LINE
 * @brief ANSI escape sequence moving the cursor to the start of the line n rows down.
 * @param n Number of rows to move.
 */
#define CONSOLE_CURSOR_NEXT_LINE(n)         "\x1b[" #n "E"

/**
 * @def CONSOLE_CURSOR_POSITION
 * @brief ANSI escape sequence moving the cursor to an absolute (row, col), 1-based.
 * @param row Target row.
 * @param col Target column.
 */
#define CONSOLE_CURSOR_POSITION(row, col)   "\x1b[" #row ";" #col "H"

/**
 * @def CONSOLE_CURSOR_PREV_LINE
 * @brief ANSI escape sequence moving the cursor to the start of the line n rows up.
 * @param n Number of rows to move.
 */
#define CONSOLE_CURSOR_PREV_LINE(n)         "\x1b[" #n "F"

/**
 * @def CONSOLE_CURSOR_UP
 * @brief ANSI escape sequence moving the cursor n rows up.
 * @param n Number of rows to move.
 */
#define CONSOLE_CURSOR_UP(n)                "\x1b[" #n "A"

/**
 * @def CONSOLE_RESTORE_CURSOR
 * @brief ANSI escape sequence restoring the cursor to the saved position.
 */
#define CONSOLE_RESTORE_CURSOR              "\x1b[u"

/**
 * @def CONSOLE_SAVE_CURSOR
 * @brief ANSI escape sequence saving the current cursor position.
 */
#define CONSOLE_SAVE_CURSOR                 "\x1b[s"

// --- Erase/Screen Control Sequences (Alphabetical) ---
/**
 * @def CONSOLE_ERASE_DISPLAY_0
 * @brief ANSI escape sequence to erase from cursor to end of screen.
 */
#define CONSOLE_ERASE_DISPLAY_0 "\x1b[0J"

/**
 * @def CONSOLE_ERASE_DISPLAY_1
 * @brief ANSI escape sequence to erase from cursor to beginning of screen.
 */
#define CONSOLE_ERASE_DISPLAY_1 "\x1b[1J"

/**
 * @def CONSOLE_ERASE_DISPLAY_2
 * @brief ANSI escape sequence to erase entire screen.
 */
#define CONSOLE_ERASE_DISPLAY_2 "\x1b[2J"

/**
 * @def CONSOLE_ERASE_LINE_0
 * @brief ANSI escape sequence to erase from cursor to end of line.
 */
#define CONSOLE_ERASE_LINE_0 "\x1b[0K"

/**
 * @def CONSOLE_ERASE_LINE_1
 * @brief ANSI escape sequence to erase from cursor to beginning of line.
 */
#define CONSOLE_ERASE_LINE_1 "\x1b[1K"

/**
 * @def CONSOLE_ERASE_LINE_2
 * @brief ANSI escape sequence to erase entire line.
 */
#define CONSOLE_ERASE_LINE_2 "\x1b[2K"

// --- Alternate Screen Buffer (Alphabetical) ---
/**
 * @def CONSOLE_ALT_SCREEN_ENTER
 * @brief ANSI escape sequence to switch to the alternate screen buffer.
 */
#define CONSOLE_ALT_SCREEN_ENTER "\x1b[?1049h"

/**
 * @def CONSOLE_ALT_SCREEN_LEAVE
 * @brief ANSI escape sequence to restore the primary screen buffer.
 */
#define CONSOLE_ALT_SCREEN_LEAVE "\x1b[?1049l"

// --- Bell ---
/**
 * @def CONSOLE_BELL
 * @brief Terminal bell/alert character.
 */
#define CONSOLE_BELL "\a"

/*==============================================================================
 * MARK: - API
 *============================================================================*/

// --- Initialization and cleanup ---
/**
 * @brief Initialize the console, enabling Windows Virtual Terminal Processing.
 * @return true on success; false when console setup fails (e.g. pre-Win10 or no console).
 */
bool console_init(void);

/**
 * @brief Restore the original console mode (and leave raw mode if still enabled) and flush.
 */
void console_uninit(void);

/**
 * @brief Restore every console setting this module changes, once, in the right order.
 *
 * Leaves raw mode, mouse reporting, bracketed paste and the alternate screen, shows the cursor,
 * and restores the original mode and code pages. Safe to call more than once (later calls do
 * nothing) and safe to register with atexit, so a terminal is never left unusable when a program
 * aborts partway through a frame.
 */
void console_restore_all(void);

// --- Terminal control (full-screen TUI backend surface) ---
/**
 * @brief Switch to the alternate screen buffer (preserving the primary screen).
 */
void console_alt_screen_enter(void);

/**
 * @brief Restore the primary screen buffer.
 */
void console_alt_screen_leave(void);

/**
 * @brief Enter raw input mode: no line buffering, echo, or signal cooking; VT input on.
 *
 * Re-entrant-safe: a second call while already in raw mode is a no-op, so the original
 * terminal state is captured only once. On Windows it also turns off QuickEdit selection and
 * turns on mouse input, so mouse reporting works in the classic console as well as in terminals
 * that track the mouse themselves.
 *
 * @return true on success; false when the terminal state cannot be read or set.
 */
bool console_raw_mode_enter(void);

/**
 * @brief Restore the terminal mode saved by console_raw_mode_enter (no-op if not raw).
 */
void console_raw_mode_leave(void);

/**
 * @brief Query the terminal size in character cells.
 * @param cols Destination for the column count (must not be NULL).
 * @param rows Destination for the row count (must not be NULL).
 * @return true and writes both outputs on success; false leaves the outputs unchanged.
 */
bool console_size(U16 *const cols, U16 *const rows);

/**
 * @brief Write a byte buffer straight to stdout (the bulk path for a whole frame).
 * @param data Bytes to write (must not be NULL).
 * @param size Number of bytes; a size of 0 is a no-op.
 */
void console_write(char const *const data, USize const size);

// --- Terminal input (raw event byte stream) ---
/**
 * @brief Test whether the terminal input has bytes ready within a timeout.
 * @param timeout_ms Milliseconds to wait (0 polls without blocking).
 * @return true when at least one byte can be read before the timeout elapses.
 */
bool console_input_available(U32 const timeout_ms);

/**
 * @brief Read available input bytes from the terminal (call after console_input_available).
 * @param buffer Destination buffer (must not be NULL).
 * @param size Capacity of the buffer in bytes.
 * @return Number of bytes read; 0 on end-of-input or error.
 */
USize console_read(char *const buffer, USize const size);

/**
 * @brief Enable terminal focus-change reporting (emits \x1b[I on focus, \x1b[O on blur).
 */
void console_focus_enable(void);

/**
 * @brief Disable terminal focus-change reporting.
 */
void console_focus_disable(void);

/**
 * @brief Enable the progressive keyboard-enhancement (kitty) protocol for unambiguous keys.
 */
void console_keyboard_enhance_enable(void);

/**
 * @brief Disable the progressive keyboard-enhancement protocol.
 */
void console_keyboard_enhance_disable(void);

/**
 * @brief Enable SGR mouse reporting (button press/release, drag, and scroll).
 */
void console_mouse_enable(void);

/**
 * @brief Disable mouse reporting.
 */
void console_mouse_disable(void);

/**
 * @brief Enable bracketed-paste mode (pasted text is wrapped in \x1b[200~ / \x1b[201~).
 */
void console_paste_enable(void);

/**
 * @brief Disable bracketed-paste mode.
 */
void console_paste_disable(void);

// --- High-level color functions (RGB support where available) ---
/**
 * @brief Set the 24-bit RGB foreground color.
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 */
void console_foreground_rgb(U8 const r, U8 const g, U8 const b);

/**
 * @brief Set the 24-bit RGB background color.
 * @param r Red channel.
 * @param g Green channel.
 * @param b Blue channel.
 */
void console_background_rgb(U8 const r, U8 const g, U8 const b);

/**
 * @brief Set the foreground color from the 256-color palette.
 * @param color_index Palette index (0-255).
 */
void console_foreground_256(U8 const color_index);

/**
 * @brief Set the background color from the 256-color palette.
 * @param color_index Palette index (0-255).
 */
void console_background_256(U8 const color_index);

// --- Text formatting ---
/**
 * @brief Enable bold text.
 */
void console_format_bold(void);

/**
 * @brief Enable dim (faint) text.
 */
void console_format_dim(void);

/**
 * @brief Enable italic text.
 */
void console_format_italic(void);

/**
 * @brief Enable underlined text.
 */
void console_format_underline(void);

/**
 * @brief Reset all text attributes and colors to the terminal default.
 */
void console_format_clear(void);

// --- Cursor control ---
/**
 * @brief Move the cursor to an absolute (row, col) position (1-based).
 * @param row Target row.
 * @param col Target column.
 */
void console_cursor_position(U16 const row, U16 const col);

/**
 * @brief Hide the cursor.
 */
void console_cursor_hide(void);

/**
 * @brief Show the cursor.
 */
void console_cursor_show(void);

/**
 * @brief Move the cursor up by a number of rows.
 * @param value Number of rows to move.
 */
void console_cursor_up(U16 const value);

/**
 * @brief Move the cursor down by a number of rows.
 * @param value Number of rows to move.
 */
void console_cursor_down(U16 const value);

/**
 * @brief Move the cursor right by a number of columns.
 * @param value Number of columns to move.
 */
void console_cursor_forward(U16 const value);

/**
 * @brief Move the cursor left by a number of columns.
 * @param value Number of columns to move.
 */
void console_cursor_backward(U16 const value);

// --- Screen operations ---
/**
 * @brief Clear the entire screen and move the cursor to the home position.
 */
void console_clear_screen(void);

/**
 * @brief Clear the entire current line.
 */
void console_clear_line(void);

/**
 * @brief Save the current cursor position.
 */
void console_save_cursor_pos(void);

/**
 * @brief Restore the cursor to the saved position.
 */
void console_restore_cursor_pos(void);

// --- Utility functions ---
/**
 * @brief Flush stdout.
 */
void console_flush(void);

/**
 * @brief Test whether a stream refers to a terminal.
 * @param stream Stream to test (must not be NULL).
 * @return true when the stream is a terminal.
 */
bool console_is_terminal(FILE *const stream);

#endif // CONSOLE_H
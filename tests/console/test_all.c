/*
 * test_console.c - Test program for the enhanced console module
 */

#include <console/console.h>

int main(void) {
    printf("Testing enhanced console module...\n\n");
    
    // Initialize console (may return false when stdout is not a terminal/pipe env)
    bool const init_success = console_init();

    if (!init_success) {
        printf("console_init() returned false (stdout is not a terminal — expected in CI/pipe env).\n");
    } else {
        printf("Console initialized successfully.\n");
    }

    printf("\n");
    
    // Test RGB colors
    printf("RGB Color Tests:\n");
    console_foreground_rgb(255, 0, 0);     // Red
    printf("Red text ");
    console_foreground_rgb(0, 255, 0);     // Green
    printf("Green text ");
    console_foreground_rgb(0, 0, 255);     // Blue
    printf("Blue text\n");
    console_format_clear();
    
    // Test 256 colors
    printf("\n256 Color Tests:\n");
    console_foreground_256(196);           // Bright red
    printf("Bright red ");
    console_foreground_256(46);            // Bright green
    printf("Bright green ");
    console_foreground_256(21);            // Bright blue
    printf("Bright blue\n");
    console_format_clear();
    
    // Test text formatting
    printf("\nFormatting Tests:\n");
    console_format_bold();
    printf("Bold text ");
    console_format_clear();
    
    console_format_underline();
    printf("Underlined text ");
    console_format_clear();
    
    console_format_italic();
    printf("Italic text ");
    console_format_clear();
    
    console_format_dim();
    printf("Dim text ");
    console_format_clear();
    
    printf("\nAll formatting reset.\n");
    
    // Test background colors
    printf("\nBackground Color Tests:\n");
    console_background_rgb(255, 0, 0);     // Red background
    console_foreground_rgb(255, 255, 255); // White text
    printf(" Red Background ");
    console_format_clear();
    printf(" Normal ");
    
    console_background_rgb(0, 255, 0);     // Green background
    console_foreground_rgb(0, 0, 0);       // Black text
    printf(" Green Background ");
    console_format_clear();
    printf(" Normal\n");
    
    // Test cursor positioning (save position, move, return)
    console_save_cursor_pos();
    console_cursor_position(10, 20);
    printf("Text at position (10,20)");
    console_restore_cursor_pos();
    printf("\nReturned to original position\n");
    
    // Test screen operations
    printf("\nTerminal info:\n");
    printf("Is stdout a terminal: %s\n", 
           console_is_terminal(stdout) ? "Yes" : "No");
    
    // Cleanup: console_restore_all() now also disables focus reporting and keyboard
    // enhancement (design-review fix) before calling console_uninit() internally.
    console_restore_all();

    // Idempotency check: a second call must be a no-op, not a crash/hang.
    console_restore_all();

    printf("\nConsole cleaned up. Test completed!\n");
    
    return 0;
}
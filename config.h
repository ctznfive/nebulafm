#ifndef CONFIG
#define CONFIG

#define REFRESH 50 // Refresh every 5 seconds
#define HIDDENVIEW 1 // Display (1) or hide (0) hidden files

/* Key definitions */
#define KEY_BACKWARD 'h' // Go to the parent directory
#define KEY_DOWNWARD 'j' // Go down
#define KEY_UPWARD 'k' // Go up
#define KEY_FORWARD 'l' // Open a file or a child directory
#define KEY_HIDE 'z' // Show or hide hidden files
#define KEY_MULT ' ' // Select a file or directory
#define KEY_DEL 'd' // Delete a file or directory
#define KEY_DEL_CONF 'D' // Confirm the deletion of the file or directory
#define KEY_CPY 'y' // Copy files
#define KEY_MV 'v' // Move files
#define KEY_RNM 'a' // Rename files
#define KEY_TOP 'g' // Go to the beginning of the current file list
#define KEY_BTM 'G' // Go to the end of the current file list
#define KEY_HIGH 'H' // Move cursor to header (top) line
#define KEY_MIDDLE 'M' // Move cursor to middle line
#define KEY_LAST 'L' // Move cursor to last line
#define KEY_PAGEDOWN 'J' // Move down by a page
#define KEY_PAGEUP 'K' // Move up by a page
#define KEY_SHELL '!' // Open the shell in the current directory
#define KEY_SELALL 'V' // Add all files to the clipboard
#define KEY_SELEMPTY 'R' // Clear clipboard
#define KEY_MAKEDIR 'm' // Create a new directory
#define KEY_MAKEFILE 'f' // Create a new file
#define KEY_VIEW 'i' // Preview file or directory
#define KEY_ADDBKMR 'b' // Add a new bookmark
#define KEY_OPENBKMR '\'' // Open bookmark list
#define KEY_DELBKMR 'Z' // Delete the bookmark
#define KEY_SEARCH '/' // Search in the current directory
#define KEY_SEARCHNEXT 'n' // The next match in the file list

#endif

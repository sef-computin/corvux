
#include "terminal.h"
#include "editor.h"
#include "errors.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

void enableRawMode(struct termios *term_original){
  if (tcgetattr(STDIN_FILENO, term_original) == -1) die("tcgetattr");
  // atexit(disableRawMode);

  struct termios termios_raw = *term_original;
  termios_raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  termios_raw.c_oflag &= ~(OPOST);
  termios_raw.c_cflag |= (CS8);
  termios_raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  termios_raw.c_cc[VMIN] = 0;
  termios_raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_raw) == -1) die("tcsetattr");

}

void disableRawMode(struct termios *term_original){
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, term_original) == -1) die("tcsetattr");
}

int getWindowSize(int *rows, int *cols){
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols); 
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
};

#ifndef terminal
#define terminal

#include <termios.h>

void enableRawMode(struct termios *term_original);
void disableRawMode(struct termios *term_original);
int getWindowSize(int *rows, int *cols);
int getCursorPosition(int *rows, int *cols);


#endif // !terminal

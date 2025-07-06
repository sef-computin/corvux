/*

 Corvux editor
 by sef-comp


 Based on Kilo editor by antirez
 https://github.com/antirez/kilo

*/


#include "editor.h"
#include "errors.h"
#include "lexer.h"
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

struct termios termios_orig;


void disableRawMode(){
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_orig) == -1) die("tcsetattr");
}

void enableRawMode(){
  if (tcgetattr(STDIN_FILENO, &termios_orig) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios termios_raw = termios_orig;
  termios_raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  termios_raw.c_oflag &= ~(OPOST);
  termios_raw.c_cflag |= (CS8);
  termios_raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  termios_raw.c_cc[VMIN] = 0;
  termios_raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_raw) == -1) die("tcsetattr");
}

int main(int argc, char *argv[]){
  enableRawMode();
  
  initEditor();
  initLexer();

  if(argc>=2){
    editorOpen(argv[1]);
  }

  mainLoop();

  editorFree();
  
  return 0;
}

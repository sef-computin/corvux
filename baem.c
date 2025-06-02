/* 
  Baem text editor
  Made by sefcomp

  Based on Kilo editor by antirez
*/


#include <termios.h>
#include <unistd.h>
#include "editor.h"
#include "terminal.h"

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


int main(int argc, char* argv[]){
  struct termios term;

  enableRawMode(&term);
  EditorConfig e = *initEditor();


  while(1){
    // editorRefreshScreen();
    // editorProcessKeypress();
  }

  disableRawMode(&term);

  return 0;
}

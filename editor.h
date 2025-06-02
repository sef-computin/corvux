#ifndef editor
#define editor

#include <termios.h>
#include <time.h>

typedef struct editorSyntax{
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;  
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
} editorSyntax;

typedef struct erow{
  int idx;
  int size;
  int render_size;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

typedef struct EditorConfig{
  int cursor_x, cursor_y;
  int render_position_x;
  int screen_rows, screen_cols;
  int col_offset;
  int row_offset;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;

  editorSyntax *syntax;
  struct termios termios_orig;

} EditorConfig;


EditorConfig* initEditor();

void freeEditor(EditorConfig* editor);

int editorReadKey();

void editorFreeRow(erow *row); 


#endif // !editor

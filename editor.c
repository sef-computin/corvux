
#include "editor.h"
#include "errors.h"
#include "terminal.h"
#include <stdlib.h>

EditorConfig* initEditor(){
  EditorConfig *e;

  e->cursor_x = 0;
  e->cursor_y = 0;
  e->render_position_x = 0;
  e->numrows = 0;
  e->row = NULL;
  e->dirty = 0;
  e->row_offset = 0;
  e->col_offset = 0;
  e->filename = NULL;
  e->statusmsg[0] = '\0';
  e->statusmsg_time = 0;
  e->syntax = NULL;

  if (getWindowSize(&e->screen_rows, &e->screen_cols) == -1) die("getWindowSize");
  e->screen_rows -= 2;

  return e;
}

void editorFreeRow(erow *row){
  free(row->render);
  free(row->chars);
  free(row->hl);
}


void freeEditor(EditorConfig *e){
  int i;
  for (i = 0; i < e->numrows; i++){
    editorFreeRow(&e->row[i]);
  }
  free(e->row);
  free(e->filename);
}

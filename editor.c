#include "editor.h"
#include "errors.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "0.0.2"
#define TAB_STOP 3
#define LEFT_PADDING 5
#define QUIT_PERSISTENCE 3


enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};


typedef struct {
  int idx;
  int size;
  int render_size;
  char *chars;
  char *render;
} erow;


struct editorSyntax {
  char *filetype;
  int flags;
};

struct editorConfig{
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

  // struct editorSyntax *syntax;
} Editor;


struct abuf {
  char* b;
  int len;
};

#define ABUF_INIT {NULL, 0};

void abAppend(struct abuf *ab, const char *s, int len){
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab){
  free(ab->b);
}


void editorDrawMessageBar(struct abuf *ab){
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(Editor.statusmsg);

  msglen = msglen < Editor.screen_cols ? msglen : Editor.screen_cols; 
  if (msglen && time(NULL) - Editor.statusmsg_time < 5) abAppend(ab, Editor.statusmsg, msglen);
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
                     Editor.filename ? Editor.filename : "[No Name]", Editor.numrows,
                     Editor.dirty ? "(*)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                      Editor.cursor_y+1, Editor.numrows);

  len = len < Editor.screen_cols ? len : Editor.screen_cols;

  abAppend(ab, status, len);

  while (len < Editor.screen_cols){
    if (Editor.screen_cols - len == rlen){
      abAppend(ab, rstatus, rlen);
      break;
    } else{
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorFreeRow(erow *row){
  free(row->render);
  free(row->chars);
}

int editorRowCxToRx(erow *row, int cx){
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t'){
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    }
    rx++;
  }
  return rx;
} 

int editorRowRxToCx(erow *row, int rx){
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++){
    if (row->chars[cx] == '\t'){
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    }
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row){
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++){
    if (row->chars[j] == '\t') tabs++;
  }
  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++){
    if (row->chars[j] == '\t'){
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    }else{
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->render_size = idx;

  // editorUpdateSyntax(row);
}

void editorInsertRow(char *s, int at, size_t len) {
  if (at < 0 || at > Editor.numrows) return; 

  Editor.row = realloc(Editor.row, sizeof(erow) * (Editor.numrows + 1));
  memmove(&Editor.row[at+1], &Editor.row[at], sizeof(erow) * (Editor.numrows - at));

  for (int j = at + 1; j <= Editor.numrows; j++) Editor.row[j].idx++;

  Editor.row[at].idx = at;

  Editor.row[at].size = len;
  Editor.row[at].chars = malloc(len + 1);
  memcpy(Editor.row[at].chars, s, len);
  Editor.row[at].chars[len] = '\0';

  Editor.row[at].render_size = 0;
  Editor.row[at].render = NULL;
  editorUpdateRow(&Editor.row[at]);

  Editor.numrows++;
}

void editorInsertNewline(){
  if (Editor.cursor_x == 0){
    editorInsertRow("", Editor.cursor_y, 0);
  } else {
    erow *row = &Editor.row[Editor.cursor_y];
    editorInsertRow(&row->chars[Editor.cursor_x], Editor.cursor_y + 1, row->size - Editor.cursor_x);
    row = &Editor.row[Editor.cursor_y];
    row->size = Editor.cursor_x;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  Editor.cursor_y++;
  Editor.cursor_x = 0;
}

void editorDeleteRow(int at){
  if (at < 0 || at >= Editor.numrows) return;
  editorFreeRow(&Editor.row[at]);
  memmove(&Editor.row[at], &Editor.row[at+1], sizeof(erow) * (Editor.numrows - at - 1));
  for (int j = at; j < Editor.numrows - 1; j++) Editor.row[j].idx--;
  Editor.numrows--;
}

void editorRowAppendString(erow *row, char *s, size_t len){
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  Editor.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size+2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorRowDeleteChar(erow *row, int at){
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at+1], row->size - at);
  row->size--;
  editorUpdateRow(row);
}

void editorInsertChar(int c){
  if (Editor.cursor_y == Editor.numrows){
    editorInsertRow("", Editor.numrows, 0);
  }
  editorRowInsertChar(&Editor.row[Editor.cursor_y], Editor.cursor_x, c);
  Editor.cursor_x++;
}

void editorDeleteChar(){
  if (Editor.cursor_y == Editor.numrows) return;
  if (Editor.cursor_x == 0 && Editor.cursor_y == 0) return;
  
  erow *row = &Editor.row[Editor.cursor_y];
  if (Editor.cursor_x > 0){
    editorRowDeleteChar(row, Editor.cursor_x - 1);
    Editor.cursor_x--;
  } else{
    Editor.cursor_x = Editor.row[Editor.cursor_y - 1].size;
    editorRowAppendString(&Editor.row[Editor.cursor_y - 1], row->chars, row->size);
    editorDeleteRow(Editor.cursor_y);
    Editor.cursor_y--;
  }
}

void editorScroll(){
  Editor.render_position_x = 0;
  if (Editor.cursor_y < Editor.numrows){
    Editor.render_position_x = editorRowCxToRx(&Editor.row[Editor.cursor_y], Editor.cursor_x);
  }

  if (Editor.cursor_y < Editor.row_offset){
    Editor.row_offset = Editor.cursor_y;
  }
  if (Editor.cursor_y >= Editor.row_offset + Editor.screen_rows){
    Editor.row_offset = Editor.cursor_y - Editor.screen_rows + 1;
  }

  if (Editor.render_position_x < Editor.col_offset){
    Editor.col_offset = Editor.render_position_x;
  }
  if (Editor.render_position_x >= Editor.col_offset + Editor.screen_cols) {
    Editor.col_offset = Editor.render_position_x - Editor.screen_cols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < Editor.screen_rows; y++) {
    int filerow = y + Editor.row_offset;
    if (filerow >= Editor.numrows){
      if (Editor.numrows == 0 && y == Editor.screen_rows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Baem editor -- version %s", EDITOR_VERSION);
        if (welcomelen > Editor.screen_cols) welcomelen = Editor.screen_cols;
        int padding = (Editor.screen_cols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
    }
    } else {
      int len = Editor.row[filerow].render_size - Editor.col_offset;

      abAppend(ab, "\x1b[90m", 5);
      char buf[24];
      int buf_len = snprintf(buf, sizeof(buf), "%d", Editor.row[filerow].idx+1);
      int padding = LEFT_PADDING - buf_len - 1;
      while (padding--){ abAppend(ab, " ", 1); }
      abAppend(ab, buf, buf_len);
      abAppend(ab, " \x1b[m", 4);

      if (len < 0) len = 0;
      if (len > Editor.screen_cols) len = Editor.screen_cols;

      char *c = &Editor.row[filerow].render[Editor.col_offset];
      int j;
      for (j = 0; j < len; j++){ 
        abAppend(ab, &c[j], 1);
      }
      abAppend(ab, "\x1b[39m", 5);

    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

int editorReadKey(){
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9'){
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~'){
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else{
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O'){
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
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
}

void editorMoveCursor(int key){
  erow *row = (Editor.cursor_y >= Editor.numrows) ? NULL : &Editor.row[Editor.cursor_y];

  switch (key){
    case ARROW_LEFT:
      if (Editor.cursor_x > 0){
        Editor.cursor_x--;
      } else if (Editor.cursor_y > 0){
        Editor.cursor_y--;
        Editor.cursor_x = Editor.row[Editor.cursor_y].size;
      }
      break;
    case ARROW_DOWN:
      Editor.cursor_y = Editor.cursor_y + 1 < Editor.numrows ? Editor.cursor_y + 1 : Editor.numrows; 
      break;
    case ARROW_UP:
      Editor.cursor_y = Editor.cursor_y - 1 > 0 ? Editor.cursor_y - 1 : 0;
      break;
    case ARROW_RIGHT:
      if (row && Editor.cursor_x < row->size){
        Editor.cursor_x++;
      } else if (row && Editor.cursor_x == row->size){
        Editor.cursor_y++;
        Editor.cursor_x = 0;
      }
      break;
  }
  row = (Editor.cursor_y >= Editor.numrows) ? NULL : &Editor.row[Editor.cursor_y];
  int rowlen = row ? row->size : 0;
  if (Editor.cursor_x > rowlen){
    Editor.cursor_x = rowlen;
  }
}

void editorFree(){
  int i;
  for (i = 0; i < Editor.numrows; i++){
    editorFreeRow(&Editor.row[i]);
  }
  free(Editor.row);
  free(Editor.filename);
}

void initEditor(){
  Editor.cursor_x = 0;
  Editor.cursor_y = 0;
  Editor.render_position_x = 0;
  Editor.numrows = 0;
  Editor.row = NULL;
  Editor.dirty = 0;
  Editor.row_offset = 0;
  Editor.col_offset = 0;
  Editor.filename = NULL;
  Editor.statusmsg[0] = '\0';
  Editor.statusmsg_time = 0;

  atexit(editorFree);
  if (getWindowSize(&Editor.screen_rows, &Editor.screen_cols) == -1) die("getWindowSize");
  Editor.screen_rows -= 2;
}

void editorRefreshScreen(){
  struct abuf ab = ABUF_INIT;

  // if (getWindowSize(&Editor.screen_rows, &Editor.screen_cols) == -1) die("getWindowSize");
  // Editor.screen_rows -= 2;

  editorScroll();

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  abAppend(&ab, "\x1b[H", 3); // cursor to the start
  
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  if (Editor.cursor_y >= Editor.numrows){
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (Editor.cursor_y) + 1,
                                            (Editor.render_position_x) + 1);
  } else {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (Editor.cursor_y) + 1,
                                              (Editor.render_position_x + LEFT_PADDING) + 1);
  }
  
  abAppend(&ab, buf, strlen(buf));


  // TODO: rendering x pos
  // if (Editor.cursor_y >= Editor.numrows){
  //   snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Editor.cursor_y + 1, Editor.cursor_x + 1);
  //   abAppend(&ab, buf, strlen(buf));
  // } else {
  //   snprintf(buf, sizeof(buf), "\x1b[%d;%dH", Editor.cursor_y + 1, Editor.cursor_x + LEFT_PADDING + 1);
  //   abAppend(&ab, buf, strlen(buf));
  // }

  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}



int editorProcessKeypress(){
  static int quit_times = QUIT_PERSISTENCE;

  int c = editorReadKey();

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      return -1;
      break;
  case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDeleteChar();
      break;
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    default:
      editorInsertChar(c);
  }

  quit_times = QUIT_PERSISTENCE;
  return 0;
}


int mainLoop(){
  while(1){
    editorRefreshScreen();
    int ret = editorProcessKeypress();
    if (ret == -1){ break; }
  }

  return 0;
}

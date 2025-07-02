#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "editor.h"
#include "errors.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "0.0.3"
#define TAB_STOP 3
#define LEFT_PADDING 5
#define QUIT_PERSISTENCE 3

int LOGO[] = {
    22, 6, -1, 
    20, 10, -1,
    19, 13, -1,
    19, 17, -1,
    19, 19, -1,
    20, 12, -1,
    19, 9, -1,
    19, 9, -1,
    17, 13, -1,
    16, 14, -1,
    12, 18, -1,
    12, 20, -1,
    11, 21, -1,
    9, 21, -1,
    9, 21, -1,
    6, 22, -1,
    4, 21, -1,
    12, 5, 5, 3, -1,
    14, 3, 7, 1, -1,
    16, 1, 7, 1, -1,
    11, 9, 1, 7, -1,
    -2
  };

#define LOGO_WIDTH 45


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

#define FOREACH_MODE(MODE) \
        MODE(NORMAL) \
        MODE(INSERT) \
        MODE(VISUAL) \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum EditorMode {
    FOREACH_MODE(GENERATE_ENUM)
};

static const char *MODES_STRING[] = {
    FOREACH_MODE(GENERATE_STRING)
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
  char editorMode;
  char *filename;
  // char *command_buf;
  char statusmsg[80];
  time_t statusmsg_time;

  // struct editorSyntax *syntax;
} Editor;



/*  prototypes  */
void editorSetStatusMessage(const char *fmt, ...);
int editorReadKey();
void editorFree();
void initEditor();
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));


/* append buffer */
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

/*  row operations  */

int editorRowCxToRx(erow *row, int cx){
  int rx = 0;

  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t'){
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    }
    rx++;
  }
  
  // line number
  rx += LEFT_PADDING;

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
  cx -= LEFT_PADDING;
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
  // Editor.row[at].hl = NULL;
  // Editor.row[at].hl_open_comment = 0;
  editorUpdateRow(&Editor.row[at]);

  Editor.numrows++;
  Editor.dirty++;
}

void editorFreeRow(erow *row){
  free(row->render);
  free(row->chars);
  // free(row->hl);
}

void editorDeleteRow(int at){
  if (at < 0 || at >= Editor.numrows) return;
  editorFreeRow(&Editor.row[at]);
  memmove(&Editor.row[at], &Editor.row[at+1], sizeof(erow) * (Editor.numrows - at - 1));
  for (int j = at; j < Editor.numrows - 1; j++) Editor.row[j].idx--;
  Editor.numrows--;
  Editor.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size+2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  Editor.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len){
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  Editor.dirty++;
}

void editorRowDeleteChar(erow *row, int at){
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at],&row->chars[at+1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  Editor.dirty++;
}


/*  editor operations  */

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

/*  file i/o  */

char *editorRowsToString(int *buflen){
  int totlen = 0;
  int j;

  for (j = 0; j < Editor.numrows; j++){
    totlen += Editor.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;

  for (j = 0; j < Editor.numrows; j++){
    memcpy(p, Editor.row[j].chars, Editor.row[j].size);
    p += Editor.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename){
  if (Editor.filename != NULL) free(Editor.filename);
  Editor.filename = strdup(filename);

  // editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("open");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1){
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(line, Editor.numrows, linelen);
  }

  free(line);
  fclose(fp);
  Editor.dirty = 0;
}

void editorSave(){
  if (Editor.filename == NULL) {
    Editor.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (Editor.filename == NULL){
      editorSetStatusMessage("Save aborted");
      return;
    }
    // editorSelectSyntaxHighlight();
  };
  
  int len;
  char *buf = editorRowsToString(&len);
  
  int fd = open(Editor.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1){
    if (ftruncate(fd, len) != -1){
      if (write(fd, buf, len) == len){
        close(fd);
        free(buf);
        Editor.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*  output  */

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

int editorDrawLogo(struct abuf *ab){
  
  int padding = (Editor.screen_cols - LOGO_WIDTH) / 2;
  if (padding < 0) padding = 0;
  int p = padding;
  if (p) {
     abAppend(ab, "~", 1);
     p--;
  }
  while (p--) abAppend(ab, " ", 1);


  int rows = 1;
  int len = sizeof(LOGO) / sizeof(int);

  int current_color = 1;
  for (int i = 0; i < len; i++){
    if (LOGO[i] == -2){
      break;
    }
    if (LOGO[i] == -1){
      rows++;
      current_color = 1;
      // abAppend(ab, "\x1b[m", 3);
      abAppend(ab, "\x1b[K", 3);
      abAppend(ab, "\r\n", 2);
      int p = padding;
      if (p) {
          abAppend(ab, "~", 1);
          p--;
      }
      while (p--) abAppend(ab, " ", 1);
      continue;
    }
    if (LOGO[i] >= 0){
      for (int j = 0; j<LOGO[i]; j++) abAppend(ab, " ", 1);
      if (current_color > 0){
        abAppend(ab, "\x1b[7m", 4); 
      } else {
        abAppend(ab, "\x1b[m", 3);
      }
      current_color*=-1;
    }
  }


  return rows;
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < Editor.screen_rows; y++) {
    int filerow = y + Editor.row_offset;
    if (filerow >= Editor.numrows){
      if (Editor.numrows == 0 && y == Editor.screen_rows / 4) {
        y += editorDrawLogo(ab);
        y++;
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Corvux editor -- version %s", EDITOR_VERSION);
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

void editorDrawMessageBar(struct abuf *ab){
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(Editor.statusmsg);

  msglen = msglen < Editor.screen_cols ? msglen : Editor.screen_cols; 
  if (msglen && time(NULL) - Editor.statusmsg_time < 5) abAppend(ab, Editor.statusmsg, msglen);
}


void editorDrawStatusBar(struct abuf *ab) {
  // abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  char mstatus[9];

  abAppend(ab, "\x1b[46m", 5);
  int modelen = snprintf(mstatus, sizeof(mstatus), "|%s|", MODES_STRING[Editor.editorMode]);
  abAppend(ab, mstatus, modelen);
  // abAppend(ab, " ", 1);
  int cols_left = Editor.screen_cols - modelen - 1; 

  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\x1b[7m", 4);  
  abAppend(ab, " ", 1);
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
                     Editor.filename ? Editor.filename : "[No Name]", Editor.numrows,
                     Editor.dirty ? "(*)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
                      Editor.cursor_y+1, Editor.numrows);


  len = len < cols_left ? len : cols_left;

  abAppend(ab, status, len);

  cols_left -= len;
  while(cols_left > 0){
    if (cols_left == rlen){
      abAppend(ab, rstatus, rlen);
      break;
    } else{
      abAppend(ab, " ", 1);
      cols_left--;
    }
  }

  
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorSetStatusMessage(const char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(Editor.statusmsg, sizeof(Editor.statusmsg), fmt, ap);
  va_end(ap);
  Editor.statusmsg_time = time(NULL);
}

/*  input  */

char *editorPrompt(char *prompt, void (*callback)(char *, int)){
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';
  while(1){
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b'){
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
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

int editorProcessCommand(){
  char* command;
  command = editorPrompt(":%s", NULL);

  if (command == NULL){
    return 0;
  }
  if (strchr(command, 'w')){
    editorSave();
  }
  if (strchr(command, 'q')){
    if (!Editor.dirty || strchr(command, '!')){
      return 1;
    } 
  }

  return 0;
}

int editorProcessNormalMode(int c){

  static int quit_times = QUIT_PERSISTENCE;

  switch (c) {
    case ':':
      if (editorProcessCommand() == 1){ 
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        return -1; }
      break;
    case 'h':
      editorMoveCursor(ARROW_LEFT);
      break;

    case 'j':
      editorMoveCursor(ARROW_DOWN);
      break;

    case 'k':
      editorMoveCursor(ARROW_UP);
      break;

    case 'l':
      editorMoveCursor(ARROW_RIGHT);
      break;

    case 'x':
      editorMoveCursor(ARROW_RIGHT);
      editorDeleteChar();
      break;

    case 'i':
    case 'a':
      if (c == 'a') editorMoveCursor(ARROW_RIGHT);
      Editor.editorMode = INSERT;
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;
    
    case CTRL_KEY('o'):
      if (Editor.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press %d more times to quit.", quit_times);
        quit_times--;
        return 0;
      }
      char *fname = editorPrompt("Open file: %s (ESC to cancel)", NULL);
      if (fname == NULL){
        editorSetStatusMessage("Operation aborted");
        free(fname);
        break;
      }
      initEditor();
      editorOpen(fname);
      free(fname);
      break;

    case CTRL_KEY('n'):
      if (Editor.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press %d more times to quit.", quit_times);
        quit_times--;
        return 0;
      }
      initEditor();
      break;

    case CTRL_KEY('q'):
      if (Editor.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press %d more times to quit.", quit_times);
        quit_times--;
        return 0;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      return -1;

  }

  quit_times = QUIT_PERSISTENCE;
  return 0;
}

int editorProcessInsertMode(int c){

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;
    
    
    case HOME_KEY:
      Editor.cursor_x = 0;
      break;
    case END_KEY:
      if (Editor.cursor_y < Editor.numrows) Editor.cursor_x = Editor.row[Editor.cursor_y].size;
      break;

    case CTRL_KEY('l'):
      break;
    // case CTRL_KEY('f'):
    //   editorFind();
    //   break;

    
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDeleteChar();
      break;

    default:
      if (!iscntrl(c) || c == '\t') editorInsertChar(c);
      break;
  }

  return 0;
}

int editorProcessKeypress(){
  int c = editorReadKey();

  switch (c) {
    case CTRL_KEY('x'):
    case '\x1b':
      Editor.editorMode = NORMAL;
      return 0;
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      return 0;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP){
          Editor.cursor_y = Editor.row_offset;
        } else if (c == PAGE_DOWN){
          Editor.cursor_y = Editor.row_offset + Editor.screen_rows - 1;
          Editor.cursor_y = Editor.cursor_y > Editor.numrows ? Editor.numrows : Editor.cursor_y; 
        }
        int times = Editor.screen_rows;
        while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

  }

  switch (Editor.editorMode) {
    case NORMAL:
      return editorProcessNormalMode(c);
    case INSERT:
      return editorProcessInsertMode(c);
  }

  return 0;
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
  Editor.editorMode = NORMAL;
  Editor.dirty = 0;
  Editor.row_offset = 0;
  Editor.col_offset = 0;
  Editor.filename = NULL;
  Editor.statusmsg[0] = '\0';
  Editor.statusmsg_time = 0;

  // atexit(editorFree);
  if (getWindowSize(&Editor.screen_rows, &Editor.screen_cols) == -1) die("getWindowSize");
  Editor.screen_rows -= 2;
}

void editorRefreshScreen(){
  struct abuf ab = ABUF_INIT;

  editorScroll();

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  abAppend(&ab, "\x1b[H", 3); // cursor to the start
  
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (Editor.cursor_y - Editor.row_offset) + 1,
                                            (Editor.render_position_x - Editor.col_offset) + 1);
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

  if (Editor.editorMode == INSERT) {
    abAppend(&ab, "\x1b[\x36 q", 5);
  } else {
    abAppend(&ab, "\x1b[\x32 q", 5);
  }

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

int mainLoop(){

  // if (filename) editorOpen(filename);
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-O = open | Ctrl-N = new file | Ctrl-Q = quit");

  while(1){
    editorRefreshScreen();
    int ret = editorProcessKeypress();
    if (ret == -1){ break; }
  }

  editorFree();

  return 0;
}

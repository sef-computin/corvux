#include <string.h>
#include <unistd.h>

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

enum TOKEN_TYPE{
  PLAIN = 1,
  VARIABLE,
  KEYWORD,
  NUMBER,
  STRING,
  DTYPE,
  FNAME
};

struct syntaxRules{
  char *filetype;
  char **extensions;
  int (*get_token_type)(char *token, int token_len);
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct lexer{
  char *input;
  int len;
  int pos;
  struct syntaxRules syntax;
} lexer;

struct tokenMap{
  char *token;
  int type;
};


char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
  "switch", "#define", "#include", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case", NULL
};
char *C_HL_dtypes[] = {
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

int C_GET_TOKEN_TYPE(char *token, int len){
  for (int i = 0; C_HL_dtypes[i]; i++){
    if (strncmp(token, C_HL_dtypes[i], len)){
      return DTYPE;
    }
  } 

  for (int i = 0; C_HL_keywords[i]; i++){
    if (strncmp(token, C_HL_keywords[i], len)){
      return KEYWORD;
    }
  } 

  return PLAIN;
};

struct syntaxRules SXDB[] = {
  {
    "c",
    C_HL_extensions,
    C_GET_TOKEN_TYPE,
    "//",
    "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
  },
};





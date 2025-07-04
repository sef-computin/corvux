#include "lexer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct syntaxRules{
  char *filetype;
  char **extensions;
  char **separators;
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
  struct syntaxRules *syntax;
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

char *C_HL_separators[] = {
  " ", ".", ",", "->", NULL
};

int C_GET_TOKEN_TYPE(char *token, int len){
  if (token == NULL || len == 0) return 0;

  if ((token[0] == '\'' && token[len-1] == '\'') || (token[0] == '\"' && token[len-1] == '\"')){
    return STRING;
  }
  
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

  for (int i = 0; i < len; i++){
    if (!isdigit(token[i])) break;
    if (i == len-1){
      return NUMBER;
    }
  }

  return PLAIN;
};

struct syntaxRules SXDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_separators,
    C_GET_TOKEN_TYPE,
    "//",
    "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
  },
};

lexer Lexer;

void initLexer(){
  Lexer.input = NULL;
  Lexer.len = 0;
  Lexer.pos = 0;
  Lexer.syntax = NULL;
}

void freeLexer(){
  free(Lexer.input);
  free(Lexer.syntax);
}

int setInput(char *input, int len){
  if (input == NULL || len <= 0){
    return -1;
  }

  Lexer.input = input;
  Lexer.len = len;
  Lexer.pos = 0;
  return 0;
}

int getNextToken(int *token_len){
  *token_len = 0;
  
  if (Lexer.pos >= Lexer.len){
    return EOF;
  }
  if (Lexer.input == NULL){
    return -1;
  }

  if (Lexer.syntax == NULL){
    *token_len = Lexer.len;
    return PLAIN;
  }

  for (int i = Lexer.pos; i < Lexer.len; i++){
    for (int j = 0; Lexer.syntax->separators[j]; j++){
      int sep_len = strlen(Lexer.syntax->separators[j]);
      if (strncmp(&Lexer.input[i], Lexer.syntax->separators[j], sep_len)){
        *token_len = i - Lexer.pos;
        int token_type = Lexer.syntax->get_token_type(&Lexer.input[Lexer.pos], *token_len); 
        Lexer.pos = (i + sep_len) < Lexer.len ? i + sep_len : Lexer.len;
        return token_type;
      }
    }
  }

  *token_len = Lexer.len - Lexer.pos;
  int token_type = Lexer.syntax->get_token_type(&Lexer.input[Lexer.pos], *token_len); 
  Lexer.pos = Lexer.len;
  return token_type;
}

#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#define HL_HIGHLIGHT_NUMBERS   (1<<0)
#define HL_HIGHLIGHT_STRINGS   (1<<1)
#define HL_HIGHLIGHT_CONSTANTS (1<<2)

struct syntaxRules{
  char *filetype;
  char **extensions;
  char **separators;
  int (*get_token_type)(char *token, int token_len, int flags);
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
  "struct", "union", "typedef", "static", "enum", "class", "default", "case", NULL
};
char *C_HL_dtypes[] = {
  "int", "long", "double", "float", "char", "unsigned", "signed",
  "void", NULL
};

char *C_HL_separators[] = {
  " ", ".", ",", "->", ";", ":", "(", ")", "[", "]", "{", "}", 
  "=", "<", ">", NULL
};

int C_GET_TOKEN_TYPE(char *token, int token_len, int flags){
  if (token == NULL || token_len == 0) return 0;

  if (flags & HL_HIGHLIGHT_STRINGS){
    if ((token[0] == '\'' && token[token_len-1] == '\'') || (token[0] == '\"' && token[token_len-1] == '\"')){
      return STRING;
    }
  }
  
  for (int i = 0; C_HL_dtypes[i]; i++){
    if (token_len != strlen(C_HL_dtypes[i])) continue;
    if (!strncmp(token, C_HL_dtypes[i], token_len)){
      return DTYPE;
    }
  } 

  for (int i = 0; C_HL_keywords[i]; i++){
    if (token_len != strlen(C_HL_keywords[i])) continue;;
    if (!strncmp(token, C_HL_keywords[i], token_len)){
      return KEYWORD;
    }
  }

  if (flags & HL_HIGHLIGHT_NUMBERS){
    for (int i = 0; i < token_len; i++){
      if (i == 0 && token[i] == '-') continue;
      if (!isdigit(token[i])) break;
      if (i == token_len-1){
        return NUMBER;
      }
    }
  }

  if (flags & HL_HIGHLIGHT_CONSTANTS){
    for (int i = 0; i < token_len; i++){
      if (!isdigit(token[i]) && token[i] != '_' && !isupper(token[i])){break;}
      if (i == token_len-1) return CONSTANT;
    }

  }
  // if (token_len==4 && !strncmp(token, "NULL", 4)) return NUMBER;

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
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_CONSTANTS,
  },
};

#define SXDB_ENTRIES (sizeof(SXDB) / sizeof(SXDB[0]))

lexer Lexer;

void lexerClear(){
  Lexer.input = NULL;
  Lexer.pos = 0;
  Lexer.len = 0;
}

void initLexer(){
  lexerClear();
  Lexer.syntax = NULL;
}

int lexerSetInput(char *input, int len){
  if (input == NULL || len <= 0){
    return -1;
  }

  Lexer.input = input;
  Lexer.len = len;
  Lexer.pos = 0;
  return 0;
}

int lexerGetNextToken(int *token_len){
  *token_len = 0;
  
  if (Lexer.pos >= Lexer.len){
    lexerClear();
    return EOF;
  }

  if (Lexer.input == NULL){
    return -1;
  }


  if (Lexer.syntax == NULL){
    *token_len = Lexer.len;
    return PLAIN;
  }
  
  char *scs = Lexer.syntax->singleline_comment_start;
  char *mcs = Lexer.syntax->multiline_comment_start;
  char *mce = Lexer.syntax->multiline_comment_end;
  
  int scs_len = scs ? strlen(scs): 0;
  int mcs_len = mcs ? strlen(mcs): 0;
  int mce_len = mce ? strlen(mce): 0;

  int in_string = -1;

  for (int i = Lexer.pos; i < Lexer.len; i++){
    if (Lexer.input[i] == '\"') in_string *= -1;
    if (in_string == 1) continue;

    if (!strncmp(&Lexer.input[i], scs, scs_len)){
      *token_len = scs_len; 
      Lexer.pos += scs_len;
      return COMMENT;
    }

    if (!strncmp(&Lexer.input[i], mcs, mcs_len)){
      *token_len = mcs_len;
      Lexer.pos += mcs_len;
      return MCOM_START;
    }

    if (!strncmp(&Lexer.input[i], mce, mce_len)){
      *token_len = mce_len;
      Lexer.pos += mce_len;
      return MCOM_END;
    }

    for (int j = 0; Lexer.syntax->separators[j]; ++j){
      int sep_len = strlen(Lexer.syntax->separators[j]);
      if (!strncmp(&Lexer.input[i], Lexer.syntax->separators[j], sep_len)){
        *token_len = i - Lexer.pos;
        int token_type = Lexer.syntax->get_token_type(&Lexer.input[Lexer.pos], *token_len, Lexer.syntax->flags); 
        Lexer.pos = (i + sep_len) < Lexer.len ? i + sep_len : Lexer.len;
        return token_type;
      }
    }
  }

  *token_len = Lexer.len - Lexer.pos;
  int token_type = Lexer.syntax->get_token_type(&Lexer.input[Lexer.pos], *token_len, Lexer.syntax->flags); 
  Lexer.pos = Lexer.len;
  return token_type;
}

int lexerGetPos(){
  return Lexer.pos;
}

char *lexerGetSyntaxName(){
  return Lexer.syntax ? Lexer.syntax->filetype : NULL;
}

int lexerSetSyntax(char *extension){
 
  for (unsigned int j = 0; j < SXDB_ENTRIES; j++){
    struct syntaxRules *s = &SXDB[j];
    unsigned int i = 0;
    while (s->extensions[i]){
      if (!strcmp(s->extensions[i], extension)){
        Lexer.syntax = s;
        return 0;
      }
      i++;
    }
  }
  return -1;
}

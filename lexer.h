#ifndef lexer

  enum TOKEN_TYPE{
    PLAIN = 1,
    VARIABLE,
    KEYWORD,
    NUMBER,
    STRING,
    DTYPE,
    FNAME,
    EOF
  };

  void initLexer();

  int lexerSetInput(char *input, int len);
  int lexerSetSyntax(char *extension);
  char *lexerGetSyntaxName();
  int lexerGetNextToken(int* token_len);
  int lexerGetPos();
  


#endif // !lexer

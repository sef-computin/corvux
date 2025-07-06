#ifndef lexer

  enum TOKEN_TYPE{
    PLAIN = 1,
    VARIABLE,
    KEYWORD,
    NUMBER,
    STRING,
    DTYPE,
    FNAME,
    CONSTANT,
    COMMENT,
    MCOM_START,
    MCOM_END,
    EOF
  };

  void initLexer();

  int lexerSetInput(char *input, int len);
  int lexerSetSyntax(char *extension);
  char *lexerGetSyntaxName();
  int lexerGetNextToken(int* token_len);
  int lexerGetPos();
  


#endif // !lexer

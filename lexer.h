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
  void freeLexer();

  int setInput(char *input, int len);
  int setSyntax(char *extension);
  int getNextToken(int* token_len);


#endif // !lexer

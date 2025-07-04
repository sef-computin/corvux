corvux: corvux.c errors.c editor.c lexer.c
	clang corvux.c errors.c editor.c lexer.c -o corvux -Wall -Wextra -pedantic -std=c99

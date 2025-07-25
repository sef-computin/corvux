corvux: corvux.c errors.c editor.c lexer.c 
	clang corvux.c errors.c editor.c lexer.c -o corvux -Wall -Wextra -std=c99
corvux-deb: corvux.c errors.c editor.c lexer.c 
	clang -g corvux.c errors.c editor.c lexer.c -o corvux-deb -Wall -Wextra -std=c99

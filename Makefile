corvux: corvux.c errors.c editor.c
	clang corvux.c errors.c editor.c -o corvux -Wall -Wextra -pedantic -std=c99

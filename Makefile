baem: baem.c errors.c editor.c
	clang baem.c errors.c editor.c -o baem -Wall -Wextra -pedantic -std=c99

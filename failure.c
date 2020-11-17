#include <stdlib.h>
#include <stdio.h>

/*
 * ARGS: 
 *      msg: char*, a null-terminated string to print to stderr
 * DOES: Prints the given message and exits with error
 * RETURNS: Nothing
 */
void exit_failure(char* msg)
{
    fprintf(stderr, msg);
    exit(EXIT_FAILURE);
}

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "clientlist.h"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    // Create a new list and ensure length is 0
    ClientList cl = ClientList_new();
    assert(ClientList_length(cl) == 0);

    // Add item to list and ensure correct length
    cl = ClientList_push(cl, 0);
    assert(ClientList_length(cl) == 1);

// Add item to list and ensure correct length
    cl = ClientList_push(cl, 1);
    assert(ClientList_length(cl) == 2);

    // Add item to list and remove non-existent item. Ensure correct length.
    cl = ClientList_push(cl, 2);
    assert(ClientList_length(cl) == 3);
    cl = ClientList_remove(cl, 3);
    assert(ClientList_length(cl) == 3);

    // Remove existing item and ensure corrent length.
    cl = ClientList_remove(cl, 1);
    assert(ClientList_length(cl) == 2);

    // deallocate memory and ensure pointer is set to NULL
    ClientList_free(&cl);
    assert(cl == NULL);

    printf("All tests passed\n");
    return EXIT_SUCCESS;
}
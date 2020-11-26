#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main (void){
    const char * string = "hello!";
    const char * string2 = " world";
    const char * string3 = " this is me!";

    //assert (1 == 0); Just to make sure assert works fine.
    int fd3writeres;
    printf("Attempting to open a file with 3?\n");
    int fd3 = open("text.txt", O_CREAT | O_RDWR, 0777);
    assert(fd3 == 0); //Should be first file descriptor returned.

    printf("fd3 is %d \n", fd3);


    printf("Writing 7 bytes to fd3\n");
    fd3writeres = write(fd3, string, 6 );
    printf("write return was %zd\n", fd3writeres);
    assert (fd3writeres == 6); // Shouldnt be an issue writing here.

    printf("dup2 6 = fd3 now\n");
    assert (dup2(fd3, 6) != -1);

    printf("write return was %zd\n", write(fd3, string2, 6 ));

    printf("Closing file descriptor 3\n");
    printf("close return of fd3 was %d\n", close(fd3));
    
    printf("Attempting to write to fd6\n");
    printf("write return was %zd\n", write(6, string3, 12 ));

    int close1res;
    close1res = close(6);
    printf("close return of fd6 was %d\n", close1res);
    assert(close1res == 0); //Should close successfully.

    int close2res;
    close2res = close(6);
    assert (close2res == -1); // Already closed, should close unsuccessfully.
    printf("Attempt 2: close return of fd6 was %d\n", close2res);


    printf("\n\n Attempting to write to a closed file \n");
    fd3writeres = write(fd3, string, 6 );
    assert (fd3writeres == -1); // Can't write to a closed file.

    int fd6writeres = write(6, string, 6 );
    assert (fd6writeres == -1); // Can't write to a closed file.

    printf("All assertions passed\n");
    return 0;
}

/* Notes, 
    - write will write however many bytes it is given, it doesnt stop at null terminators.
    - When we close the fd3, we can still write to fd6 and it still shares the pointer the old fd had.
        This means a ref_count is maintained in the FileEntry to not close if there is another
        fd referencing it.
    
 */

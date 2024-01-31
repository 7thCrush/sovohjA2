/**
 * Buffering incoming data to separate out complete lines.
 * Uses newline (\n) as line-end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define LINESIZ 250

#include "linebuffer.h"


/**
 * Allocate a new buffer.
 * \return A newly allocated buffer, NULL on failure. */
struct linebuf *linebuf_new(void) {
    struct linebuf *lb = malloc(sizeof(struct linebuf));
    if (lb == NULL) return NULL;
    lb->buf = malloc(2*LINESIZ);
    if (lb->buf == NULL) {
        free(lb);
        return NULL;
    }
    lb->size = 2*LINESIZ;
    lb->end = 0;
    return lb;
}

/**
 * Free the buffer.
 * \param lb Linebuffer to free. */
void linebuf_free(struct linebuf *lb) {
    if (lb == NULL) return;
    if (lb->buf != NULL) free(lb->buf);
    free(lb);
}

/**
 * Read more data into the buffer.
 * \param lb Linebuffer to read to.
 * \param fd File descriptor to read from.
 * \return Number of bytes read, or -1 on error. 0 if EOF. */
int linebuf_readdata(struct linebuf *lb,int fd) {
    if (lb == NULL) return -1;
    if ((lb->size-lb->end) < LINESIZ) { // Too little buffer left, increase buffer size
        char *tmp = realloc(lb->buf,lb->size+LINESIZ);
        if (tmp == NULL) {
//		printF("linebuf: realloc() failed\n");
            return -1;
        }
        lb->buf = tmp;
        lb->size += LINESIZ;
    }
    int ret = read(fd,lb->buf+lb->end,lb->size-lb->end);
    if (ret > 0) {
        lb->end += ret;
    }
    return ret;
}

/**
 * Try to read a line from the buffer.
 * \param lb Linebuffer to read from.
 * \return A newly allocated string (free with free()) or NULL if there
 * is no complete line in the buffer. */
char *linebuf_getline(struct linebuf *lb) {
    int i;
    for (i = 0; i < lb->end; i++) {
        if (lb->buf[i] == '\n') {  // Found end of line
            char *str = strndup(lb->buf,i+1);
            lb->end -= (i+1);  // Updata size of rest of the data.
            memmove(lb->buf,lb->buf+i+1,lb->end);  // Move contents of buffer
            return str;            
        }
    }
    // Couldn't find a newline, thus no full lines in the buffer.
    return NULL;
}


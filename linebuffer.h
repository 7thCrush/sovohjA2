#ifndef LINEBUFFER_H
#define LINEBUFFER_H

struct linebuf {
    char *buf;	// Allocated buffer.
    int size;	// Size of allocated buffer.
    int end;	// Amount of data in the buffer.
};

struct linebuf *linebuf_new(void);
void linebuf_free(struct linebuf *lb);
int linebuf_readdata(struct linebuf *lb,int fd);
char *linebuf_getline(struct linebuf *lb);

#endif

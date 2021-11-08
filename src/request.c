#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)
#define Q_MAX (1000)

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t full = PTHREAD_COND_INITIALIZER;

//
//	TODO: add code to create and manage the buffer
//

// Request Buffer
// ----------------------------------------------------------------
typedef struct Request_t {
    char *filename;
    int filesize;
    int fd;
    struct Request_t *next;
} Request;

void makeRequest(Request *r, char *filename, int filesize, int fd) {
    r->filename = strdup(filename);
    r->filesize = filesize;
    r->fd = fd;
    r->next = NULL;
}

void printRequest(Request r) {
    printf("Request: filename = %s, filesize = %d, fd = %d\n", r.filename, r.filesize, r.fd);
}
// ----------------------------------------------------------------

// Buffer
// ----------------------------------------------------------------
typedef struct Buffer_t {
    Request *front;
    Request *rear;
    int count;
} Buffer;

// Check if Buffer is empty
int BufferIsEmpty(Buffer* buf) {
    if(buf->front == NULL && buf->rear == NULL) {
        return 1;
    }
    return 0;
}

// Check if Buffer is full
int BufferIsFull(Buffer* buf) {
    if(buf->count == buffer_max_size) {
        return 1;
    }
    return 0;
}
// ----------------------------------------------------------------

// First In First Out (FIFO)
// ----------------------------------------------------------------
void insertFIFO(Buffer *buf, char *filename, int filesize, int fd) {
    // Create and make request
    Request *r = (Request*)malloc(sizeof(Request));
    makeRequest(r, filename, filesize, fd);

    if(BufferIsFull(buf)) {
        printf("Buffer is full\n");
        return;
    }
    else if(BufferIsEmpty(buf)) {
        buf->front = r;
        buf->rear = r;
    }
    else {
        buf->rear->next = r;
        buf->rear = r;
    }
    buf->count++;
}

void deleteFIFO(Buffer *buf, Request *r) {
    if(BufferIsEmpty(buf)) {
        printf("Buffer is empty\n");
        return;
    }

    // Backup data into 'r'
    makeRequest(r, buf->front->filename, buf->front->filesize, buf->front->fd);
    Request *temp = buf->front;
    if(buf->front == buf->rear) {
        buf->front = NULL;
        buf->rear = NULL;
    }
    else {
        buf->front = buf->front->next;
    }
    free(temp);
    buf->count--;
}
// ----------------------------------------------------------------

// Smallest File First (SFF)
// ----------------------------------------------------------------
void insertSFF(Buffer *buf, char *filename, int filesize, int fd) {
	// Create and make Request
    Request *r = (Request*)malloc(sizeof(Request));
    makeRequest(r, filename, filesize, fd);

    if(BufferIsFull(buf)) {
        printf("Buffer is full\n");
        return;
    }
    else if(BufferIsEmpty(buf)) {
        buf->front = r;
        buf->rear = r;
    }
    else {
		// Insert at start condition
        if(r->filesize < buf->front->filesize) {
            r->next = buf->front;
            buf->front = r;
        }
        else { // Travel till required position and insert
            Request *ptr = buf->front;
            while(ptr->next != NULL && ptr->next->filesize < r->filesize) {
                ptr = ptr->next;
            }
            r->next = ptr->next;
            ptr->next = r;
        }
    }
    buf->count++;
}

void deleteSFF(Buffer *buf, Request *r) {
    if(BufferIsEmpty(buf)) {
        printf("Buffer is empty\n");
        return;
    }

    // Backup data into 'r'
    makeRequest(r, buf->front->filename, buf->front->filesize, buf->front->fd);
    Request *temp = buf->front;
    if(buf->front == buf->rear) { // Single Request present 
        buf->front = NULL;
        buf->rear = NULL;
    }
    else {
        buf->front = buf->front->next;
    }
    free(temp);
    buf->count--;
}
// ----------------------------------------------------------------


// Global Buffers for FIFO and SFF
// ----------------------------------------------------------------
Buffer b_temp = { .count = 0, .front = NULL, .rear = NULL };
Buffer *buffer = &b_temp;
// ----------------------------------------------------------------

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>OSTEP WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
		readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
		strcpy(filetype, "image/jpeg");
    else 
		strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread logic)
//
void* thread_request_serve_static(void* arg)
{
	// TODO: write code to actualy respond to HTTP requests
    while(1) {
        sleep(1);
        pthread_mutex_lock(&mutex);
        while(buffer && buffer->count == 0)
            pthread_cond_wait(&full, &mutex);
        Request *r = (Request*)malloc(sizeof(Request));
        if(scheduling_algo)
            deleteSFF(buffer, r);
        else
            deleteFIFO(buffer, r);
        printf("Request for %s of size %d is removed from the buffer\n", r->filename, r->filesize);

        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&mutex);

        if(r) {
            // Serve request
            request_serve_static(r->fd, r->filename, r->filesize);
            close_or_die(r->fd);
        }
    }
}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
	// get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

	// verify if the request type is GET or not
    if (strcasecmp(method, "GET")) {
		request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
		return;
    }
    request_read_headers(fd);
    
	// check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
    // Security check to not allow up in the file system - ////////////////////////
    if(strstr(filename, "..") != NULL) {
        request_error(fd, filename, "403", "Forbidden", "Traversing up in file system is prohibited");
        return;
    }

	// get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
		request_error(fd, filename, "404", "Not found", "server could not find this file");
		return;
    }
    
	// verify if requested content is static
    if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}
		
		// TODO: write code to add HTTP requests in the buffer based on the scheduling policy
        pthread_mutex_lock(&mutex);
        while(buffer->count == buffer_max_size)
            pthread_cond_wait(&empty, &mutex);
        if(scheduling_algo)
            insertSFF(buffer, filename, sbuf.st_size, fd);
        else
            insertFIFO(buffer, filename, sbuf.st_size, fd);
        printf("Request for %s of size %d is added to the buffer\n",filename, sbuf.st_size);
        if(scheduling_algo)
            printf("Added size SFF = %d\n", buffer->count);
        else
            printf("Added size FIFO = %d\n", buffer->count);
        pthread_cond_signal(&full);
        pthread_mutex_unlock(&mutex);
    } else {
		request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}

#ifndef SERVER_H
#define SERVER_H

// Standard headers needed by the declarations below
#include <stddef.h>			// size_t
#include <sys/types.h>	// ssize_t (for write_all prototype if you ever change it)
#include <sys/socket.h> // socket types (for handle_client arg)

// -----------------------------------------------------------------------------
// Constants (macros so header doesn't define storage)
// -----------------------------------------------------------------------------
#define CRLF "\r\n"
#define CRLFCRLF "\r\n\r\n"
#define SP " "
#define THREAD_COUNT 32

// -----------------------------------------------------------------------------
// Connection policy
// -----------------------------------------------------------------------------
enum Connection
{
	CLOSE = 0,
	KEEP_ALIVE = 1,
};

// -----------------------------------------------------------------------------
// String slice primitives (non-owning views into a buffer)
// -----------------------------------------------------------------------------
typedef struct string
{
	const char *data; // pointer INTO some existing buffer; not null-terminated
	size_t len;				// number of bytes
} string;

typedef struct split_string
{
	string *strings; // heap array of string slices (caller frees)
	size_t len;			 // number of slices
} split_string;

// -----------------------------------------------------------------------------
// Request-line view
// -----------------------------------------------------------------------------
typedef struct request_line
{
	string method;	// e.g. "GET"
	string path;		// e.g. "/index.html"
	string version; // e.g. "HTTP/1.1"
	int ok;					// 1 if parsed and method allowed; else 0
} request_line_view;

typedef struct tpool_work
{
	int client_socket;
	struct tpool_work *next;
} tpool_work;

typedef struct thread_pool
{
	size_t count;
	pthread_t *threads;

	tpool_work *first;
	tpool_work *last;

	pthread_mutex_t mtx;
	pthread_cond_t has_job;

	int isExit;
} thread_pool;

// -----------------------------------------------------------------------------
// Parsing / utilities
// -----------------------------------------------------------------------------

/**
 * Split a byte string by a byte delimiter (no allocations for substrings;
 * only the array of slices is allocated). Caller must free(result.strings).
 */
split_string split_string_by(const char *s, size_t s_len,
														 const char *split, size_t split_len);

/** Debug helper: print each slice on its own line. */
void print_split_string(split_string s);

/**
 * Parse "METHOD SP REQUEST-TARGET SP HTTP-VERSION".
 * Accepts only a fixed set of methods (GET, POST, PUT, DELETE, HEAD).
 * Returns .ok = 1 on success, 0 on failure.
 */
request_line_view get_req_line(split_string req_line_parts);

/*
@brief Extracts the value of the given header from the split_string

@param list should contain only the reuestline and headers of the http request
@param header the given header we are searching for
@return The value of the header. If header not specified then string.data = NULL and string.len = 0.
*/
string get_Header(split_string list, string header);

/**
 * Decide keep-alive/close based on the "Connection" header you parsed from `headers`.
 * Your current implementation defaults to KEEP_ALIVE if the header is absent.
 *
 * IMPORTANT: Do not free `headers.strings` inside this function; the caller owns it.
 * (Keep this purely as a decision helper.)
 */
enum Connection check_connection_header(split_string headers);

// -----------------------------------------------------------------------------
// Routing / responses
// -----------------------------------------------------------------------------

/**
 * Build a filesystem path under docroot "pages".
 *  "/"            -> "pages/index.html"
 *  "/foo"         -> "pages/foo/index.html"
 *  "/foo/bar"     -> "pages/foo/bar/index.html"
 * Returns 0 on success, 1 on error (bad path or snprintf failure).
 */
int build_route_path(string path, char *out, size_t out_len);

/**
 * Write exactly `len` bytes, handling partial writes.
 * Returns 0 on success, 1 on error.
 */
int write_all(int fd, const void *buf, size_t len);

/**
 * Send a 200 OK with file contents.
 * Chooses Connection: keep-alive/close based on `connection`.
 * Returns 0 on success, 1 on error (open/fstat/read/write).
 */
int send_file_respond(int client_fd, const char *filepath, enum Connection connection);

/** Fallback 404 body in-memory (used if `pages/error.html` cannot be opened). */
int worstcase(int client_fd);

/**
 * Send a 404 Not Found response from "pages/error.html" if present,
 * otherwise falls back to worstcase().
 * Returns 0 on success path, 1 on write error.
 */
int send_404_respond(int client_fd);

/**
 * Handle one TCP client with HTTP/1.1 (persistent) semantics.
 * Uses SO_RCVTIMEO for idle keep-alive timeout and loops for multiple requests
 * on the same connection until Connection: close or timeout/EOF.
 * Returns 0 on normal close, 1 on fatal error.
 */
int handle_client(int client_socket);

int que_list_push(thread_pool *tp, int new_client_socket);
void que_list_pop(thread_pool *tp, int *out_fd);
void *worker(void *arg);

#endif // SERVER_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include "server.h"

#define THREAD_COUNT 32

request_line_view get_req_line(split_string req_line)
{
	string null_string = {
			.data = NULL,
			.len = 0};

	request_line_view result = {.method = null_string, .version = null_string, .path = null_string, .ok = 0};

	if (req_line.len != 3)
	{
		return result;
	}

	string method = req_line.strings[0];
	string path = req_line.strings[1];
	string version = req_line.strings[2];

	const char *allowed[] = {"GET", "POST", "PUT", "DELETE", "HEAD"};
	int is_allowed = 0;

	for (size_t i = 0; i < 5; i++)
	{
		size_t a_len = strlen(allowed[i]);
		if (a_len == method.len && memcmp(allowed[i], method.data, a_len) == 0)
		{
			is_allowed = 1;
			break;
		}
	}
	if (!is_allowed)
	{
		return result;
	}
	result.method = method;
	result.path = path;
	result.version = version;
	result.ok = 1;
	return result;
}

string get_header(split_string list, string header)
{

	string result = {.data = NULL, .len = 0};
	if (list.len < 2)
		return result;
	for (size_t i = 1; i < list.len; i++)
	{
		string target = list.strings[i];
		char *p = memchr(target.data, ':', target.len);
		size_t target_header_len = p - target.data;
		if (!p)
			continue;

		if (target_header_len == header.len && memcmp(target.data, header.data, target_header_len) == 0)
		{
			size_t header_len = target.len - target_header_len - 1;
			if (header_len < 1)
				return result;
			result.len = target.len - target_header_len - 2; // -2 for the space in between.
			result.data = p + 2;														 // +2 to get over the space
			return result;
		}
	}

	return result;
}

enum Connection check_connection_header(split_string headers)
{
	string connection_string;
	connection_string.data = "Connection";
	connection_string.len = 10;

	string result = get_header(headers, connection_string);

	if (!result.data)
	{
		return KEEP_ALIVE;
	}

	if (result.len == strlen("keep-alive") && memcmp("keep-alive", result.data, result.len) == 0)
	{
		return KEEP_ALIVE;
	}
	return CLOSE;
}

split_string split_string_by(const char *s, size_t s_len, const char *split, size_t split_len)
{
	split_string result;
	result.len = 0;
	result.strings = NULL;

	int count = 1;
	const char *p = s;
	size_t p_len = s_len;

	while ((p = memmem(p, p_len, split, split_len)) != NULL)
	{
		count++;
		p += split_len;
		p_len = s_len - (p - s);
	}
	result.strings = calloc(count, sizeof(string));

	if (!result.strings)
	{
		return result;
	}

	result.len = count;

	p = s;
	const char *prev = s;
	p_len = s_len;

	for (size_t i = 0; (p = memmem(p, p_len, split, split_len)) != NULL; i++)
	{
		string sub_result;
		sub_result.len = p - prev;
		sub_result.data = prev;

		result.strings[i] = sub_result;

		prev = p + split_len;
		p = p + split_len;
		p_len = s_len - (p - s);
	}

	result.strings[count - 1].len = (size_t)(&s[s_len] - prev);
	result.strings[count - 1].data = prev;

	return result;
}

void print_split_string(split_string s)
{
	for (size_t i = 0; i < s.len; i++)
	{
		string string = s.strings[i];
		printf("\n[%.*s]\n", (int)string.len, string.data);
	}
}

int build_route_path(string path, char *out, size_t out_len)
{
	if (path.len == 0)
	{
		return 1;
	}
	if (path.data[0] != '/')
	{
		return 1;
	}

	if (path.len == 1 && path.data[0] == '/')
	{
		size_t count = snprintf(out, out_len, "pages/index.html");
		if (count < 0)
		{
			return 1;
		}
		return 0;
	}

	size_t count = snprintf(out, out_len, "pages%.*s/index.html", (int)path.len, path.data);
	if (count < 0)
	{
		return 1;
	}
	return 0;
}

int write_all(int fd, const void *buf, size_t len)
{

	const char *p = buf;

	while (len)
	{
		ssize_t count = write(fd, p, len);
		if (count < 0)
		{
			return 1;
		}
		p += (size_t)count;
		len -= (size_t)count;
	}
	return 0;
}

int send_file_respond(int client_fd, const char *filepath, enum Connection connection)
{
	int filepath_fd;
	if ((filepath_fd = open(filepath, O_RDONLY)) < 0)
	{
		return 1;
	}
	struct stat st;
	if (fstat(filepath_fd, &st) < 0)
	{
		close(filepath_fd);
		return 1;
	}
	char head[256];
	char *connection_header = connection == KEEP_ALIVE ? "Connection: keep-alive\r\n" : "Connection: close\r\n";

	int head_len = snprintf(head, 256, "HTTP/1.1 200 OK\r\n"
																		 "Content-Type: text/html; charset=utf-8\r\n"
																		 "Content-Length: %lld\r\n%s"
																		 "\r\n",
													(long long)st.st_size, connection_header);
	if (head_len < 0)
	{
		close(filepath_fd);
		return 1;
	}

	char body[65536];

	size_t count = 0;
	for (;;)
	{
		ssize_t sub_count = read(filepath_fd, body + count, sizeof(body) - count);
		if (sub_count < 0)
		{
			close(filepath_fd);
			return 1;
		}
		if (sub_count == 0)
		{
			close(filepath_fd);
			break;
		}
		count += (size_t)sub_count;
	}
	if (write_all(client_fd, head, head_len))
	{
		return 1;
	}
	if (write_all(client_fd, body, count))
	{
		return 1;
	}
	return 0;
}

int worstcase(int client_fd)
{
	const char *body = "<h1>404 page not found</h1>";
	char head[256];
	int head_len = snprintf(head, sizeof(head), "HTTP/1.1 404 Not Found\r\n"
																							"Content-Type: text/html; charset=utf-8\r\n"
																							"Content-Length: %d\r\n"
																							"Connection: close\r\n"
																							"\r\n",
													(int)strlen(body));
	if (write_all(client_fd, head, head_len))
	{
		return 1;
	}
	if (write_all(client_fd, body, strlen(body)))
	{
		return 1;
	}
	return 0;
}

int send_404_respond(int client_fd)
{

	int error_fd;
	if ((error_fd = open("pages/error.html", O_RDONLY)) < 0)
	{
		if (worstcase(client_fd))
		{
			return 1;
		}
		return 0;
	}
	struct stat st;
	if (fstat(error_fd, &st) < 0)
	{
		if (worstcase(client_fd))
		{
			close(error_fd);
			return 1;
		}
		close(error_fd);
		return 0;
	}
	char body[65536];
	char head[256];
	int head_len = snprintf(head, sizeof(head),
													"HTTP/1.1 404 Not Found\r\n"
													"Content-Type: text/html; charset=utf-8\r\n"
													"Content-Length: %lld\r\n"
													"Connection: close\r\n"
													"\r\n",
													(long long)st.st_size);

	size_t count = 0;
	for (;;)
	{
		ssize_t sub_count = read(error_fd, body + count, sizeof(body) - count);
		if (sub_count < 0)
		{
			close(error_fd);
			return 1;
		}
		if (sub_count == 0)
		{
			close(error_fd);
			break;
		}
		count += (size_t)sub_count;
	}

	if (write_all(client_fd, head, head_len))
	{
		return 1;
	}
	if (write_all(client_fd, body, count))
	{
		return 1;
	}
	return 0;
}

int handle_client(int client_socket)
{
	struct timeval tval = {.tv_sec = 5, .tv_usec = 0};
	if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tval, sizeof(tval)) < 0)
		return 1;

	for (;;)
	{
		int max = 1024;
		char read_buf[max];
		size_t count = 0;
		for (;;)
		{
			ssize_t sub_count = read(client_socket, &read_buf[count], sizeof(read_buf) - 1 - count);
			if (sub_count < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					return 0;
				}
				perror("read()");
				return 1;
			}
			if (sub_count == 0)
			{
				return 0;
			}
			count += sub_count;
			if (memmem(read_buf, count, CRLFCRLF, strlen(CRLFCRLF)) != NULL)
			{
				break;
			}
		}

		read_buf[count] = '\0';
		split_string headers_and_requestline = split_string_by(read_buf, (size_t)count, CRLFCRLF, strlen(CRLFCRLF));
		// print_split_string(headers_and_requestline);
		if (headers_and_requestline.len < 1)
		{
			char *res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			int n = write_all(client_socket, res, strlen(res));
			free(headers_and_requestline.strings);
			if (n)
			{
				return 1;
			}
			continue;
		}

		split_string result = split_string_by(headers_and_requestline.strings[0].data, headers_and_requestline.strings[0].len, CRLF, strlen(CRLF));

		if (result.len < 1)
		{
			char *res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			int n = write_all(client_socket, res, strlen(res));

			free(result.strings);
			free(headers_and_requestline.strings);
			if (n)
			{
				return 1;
			}
			continue;
		}
		string request_line = result.strings[0];
		split_string split_req_line = split_string_by(request_line.data, request_line.len, SP, strlen(SP));
		request_line_view req_line_view = get_req_line(split_req_line);

		if (!req_line_view.ok)
		{
			char *res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
			int n = write_all(client_socket, res, strlen(res));

			free(result.strings);
			free(headers_and_requestline.strings);
			if (n)
			{
				return 1;
			}
			continue;
		}
		if (memcmp("GET", req_line_view.method.data, 3) != 0)
		{
			char *res = "HTTP/1.1 405 ONLY GET METHOD ALLOWED\r\nContent-Length: 0\r\n\r\n";
			int n = write_all(client_socket, res, strlen(res));
			free(headers_and_requestline.strings);
			free(split_req_line.strings);
			free(result.strings);
			if (n)
			{
				return 1;
			}
			continue;
		}

		char path[100];
		if (build_route_path(req_line_view.path, path, 100) == 1)
		{
			char *res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
			int n = write_all(client_socket, res, strlen(res));
			free(result.strings);
			free(split_req_line.strings);
			free(headers_and_requestline.strings);

			if (n)
			{
				return 1;
			}
			continue;
		}
		enum Connection con = check_connection_header(result);

		if (send_file_respond(client_socket, path, con))
		{
			send_404_respond(client_socket);
			con = CLOSE;
		}
		free(headers_and_requestline.strings);
		free(result.strings);
		free(split_req_line.strings);
		if (con == CLOSE)
		{
			return 0;
		}
	}
}

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

int que_list_push(thread_pool *tp, int new_client_socket)
{
	tpool_work *new_work_p = calloc(1, sizeof(tpool_work));
	if (!new_work_p)
	{
		return 1;
	}
	new_work_p->client_socket = new_client_socket;
	new_work_p->next = NULL;

	if (tp->first == NULL && tp->last == NULL)
	{
		tp->first = new_work_p;
		tp->last = new_work_p;
		return 0;
	}
	tp->last->next = new_work_p;
	tp->last = new_work_p;
	return 0;
}
void que_list_pop(thread_pool *tp, int *out_fd)
{
	if (tp->first == NULL)
	{
		return;
	}
	*out_fd = tp->first->client_socket;
	if (tp->first == tp->last)
	{

		free(tp->first);
		tp->first = NULL;
		tp->last = NULL;
		return;
	}
	tpool_work *second = tp->first->next;
	free(tp->first);
	tp->first = second;
	return;
}

void *worker(void *arg)
{

	thread_pool *tp = (thread_pool *)arg;
	for (;;)
	{
		pthread_mutex_lock(&tp->mtx);
		if (tp->isExit)
		{
			pthread_mutex_unlock(&tp->mtx);
			break;
		}
		while (tp->first == NULL)
		{
			pthread_cond_wait(&tp->has_job, &tp->mtx);
		}

		int client_socket = -1;
		que_list_pop(tp, &client_socket);

		if (client_socket == -1)
		{
			// need to error handle
			printf("minä olen täällä miten tämä on mahdollista????\n");
		}
		pthread_mutex_unlock(&tp->mtx);
		handle_client(client_socket);
		close(client_socket);
	}
	return NULL;
}

int main(void)
{
	int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_socket < 0)
	{
		perror("socket()");
		return 1;
	}
	int opt = 1;
	if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		perror("setsockopt");
		close(tcp_socket);
		return 1;
	}
	printf("socket created\n");
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	addr.sin_port = htons(8080);
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof(addr);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(tcp_socket, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		perror("bind()");
		close(tcp_socket);
		return 1;
	}
	printf("socket binded\n");
	if (listen(tcp_socket, 1000) < 0)
	{
		perror("listen()");
		close(tcp_socket);
		return 1;
	}
	printf("socket listening...\n");

	struct thread_pool t_pool;
	t_pool.count = THREAD_COUNT;
	t_pool.first = NULL;
	t_pool.last = NULL;
	t_pool.isExit = 0;
	if (pthread_cond_init(&t_pool.has_job, NULL) != 0)
	{
		return 1;
	}
	if (pthread_mutex_init(&t_pool.mtx, NULL) != 0)
		return 1;

	pthread_t *p_threads = calloc(THREAD_COUNT, sizeof(pthread_t));
	if (!p_threads)
		return 1;
	t_pool.threads = p_threads;

	for (int i = 0; i < THREAD_COUNT; i++)
	{
		if (pthread_create(&t_pool.threads[i], NULL, worker, &t_pool) != 0)
		{
			perror("pthread_create()");
			return 1;
		}
	}

	while (1)
	{
		// printf("\n↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓\n");
		// printf("waiting for a connection...\n");
		int client_socket = accept(tcp_socket, NULL, NULL);
		// printf("Jee accepted...\n");

		if (client_socket < 0)
		{
			pthread_mutex_lock(&t_pool.mtx);
			t_pool.isExit = 1;
			pthread_mutex_unlock(&t_pool.mtx);
			perror("accept()");
			break;
		}

		pthread_mutex_lock(&t_pool.mtx);
		que_list_push(&t_pool, client_socket);
		pthread_cond_signal(&t_pool.has_job);
		pthread_mutex_unlock(&t_pool.mtx);

		// count++;
		// printf("count: %d\n", count);
		// printf("\n↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑\n");
	}

	for (int i = 0; i < THREAD_COUNT; i++)
	{
		pthread_join(t_pool.threads[i], NULL);
	}
	free(t_pool.threads);

	tpool_work *current_node = t_pool.first;
	while (current_node->next != NULL)
	{
		tpool_work *placeholder = current_node;
		close(current_node->client_socket);
		free(current_node);
		current_node = placeholder->next;
	}
	pthread_mutex_destroy(&t_pool.mtx);
	pthread_cond_destroy(&t_pool.has_job);
	close(tcp_socket);
}

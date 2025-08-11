#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

const char *CRLF = "\r\n";
const char *CRLFCRLF = "\r\n\r\n";
const char *SP = " ";

typedef struct string
{
	const char *data;
	size_t len;
} string;

typedef struct split_string
{
	string *strings;
	size_t len;
} split_string;

typedef struct request_line
{
	string method;
	string path;
	string version;
	int ok;
} request_line_view;

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

int send_file_respond(int client_fd, const char *filepath)
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
	int head_len = snprintf(head, 256, "HTTP/1.1 200 OK\r\n"
																		 "Content-Type: text/html; charset=utf-8\r\n"
																		 "Content-Length: %lld\r\n"
																		 "Connection: close\r\n"
																		 "\r\n",
													(long long)st.st_size);
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
	int max = 1024;
	char read_buf[max];
	size_t count = 0;
	for (;;)
	{
		ssize_t sub_count = read(client_socket, &read_buf[count], sizeof(read_buf) - 1 - count);
		if (sub_count < 0)
		{
			perror("read()");
			close(client_socket);
			return 1;
		}
		if (sub_count == 0)
		{
			break;
		}
		count += sub_count;
		if (memmem(read_buf, count, CRLFCRLF, strlen(CRLFCRLF)) != NULL)
		{
			break;
		}
	}

	read_buf[count] = '\0';

	split_string result = split_string_by(read_buf, (size_t)count, CRLF, strlen(CRLF));
	printf("\n----------\n");

	if (result.len < 1)
	{
		char *res = "HTTP/1.0 400  \r\n\r\n";
		int n = write_all(client_socket, res, strlen(res));

		free(result.strings);
		if (n)
		{
			return 1;
		}
		return 0;
	}
	string request_line = result.strings[0];
	split_string split_req_line = split_string_by(request_line.data, request_line.len, SP, strlen(SP));
	request_line_view req_line_view = get_req_line(split_req_line);

	if (!req_line_view.ok)
	{
		char *res = "HTTP/1.0 400  \r\n\r\n";
		int n = write_all(client_socket, res, strlen(res));

		free(result.strings);
		if (n)
		{
			return 1;
		}
		return 0;
	}
	if (memcmp("GET", req_line_view.method.data, 3) != 0)
	{
		char *res = "HTTP/1.0 405 ONLY GET METHOD ALLOWED\r\n\r\n";
		write_all(client_socket, res, strlen(res));

		free(split_req_line.strings);
		free(result.strings);
		return 0;
	}

	char path[100];
	if (build_route_path(req_line_view.path, path, 100) == 1)
	{
		char *res = "HTTP/1.0 400  \r\n\r\n";
		int n = write_all(client_socket, res, strlen(res));
		free(result.strings);
		if (n)
		{
			return 1;
		}
		return 0;
	}
	if (send_file_respond(client_socket, path))
	{
		send_404_respond(client_socket);
	}
	free(result.strings);
	return 0;
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
	if (listen(tcp_socket, 10) < 0)
	{
		perror("listen()");
		close(tcp_socket);
		return 1;
	}
	printf("socket listening...\n");
	int count = 1;
	while (1)
	{
		printf("here is the couunt: %d", count);
		count++;
		printf("waiting for a connection...\n");
		int client_socket = accept(tcp_socket, NULL, NULL);
		if (client_socket < 0)
		{
			perror("accept()");
			close(tcp_socket);
			return 1;
		}

		if (handle_client(client_socket) != 0)
		{
			close(client_socket);
			close(tcp_socket);
			return 1;
		}
		close(client_socket);
		printf("accepted\n");
	}
}

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "connection.h"
#include "servermanager.h"
#include "event.h"
#include "debug.h"

static void event_readable_callback(int fd, void *arg)
{
	connection *conn = (connection *)arg;
	char buf[65536];
	debug_msg("file: %s, line: %d", __FILE__, __LINE__);
	/* readӦ������һ����� */
	ssize_t	n = read(fd, buf, 65536);
	if (n > 0)
	{
		if (conn->server->readable_callback)
		{
			/* �û��趨��д�ص���push����,��ֹinput_buffer���ű� */
			char *p = array_push_n(conn->input_buffer, n);
			strncpy(p, buf, n);
			
			conn->server->readable_callback(conn);
		}
	}
	else if (n == 0)
	{
		/* FIN */
		connection_free(conn);
	}
	else
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		debug_sys("file: %s, line: %d", __FILE__, __LINE__);
	}
}

static void event_writable_callback(int fd, void *arg)
{
	connection *conn = (connection *)arg;
	array *buffer = conn->output_buffer;
	ssize_t n_write = write(fd, buffer->elts, buffer->nelts);
	if (n_write > 0)
	{
		if (n_write == buffer->nelts)
		{
			/* ȫ�����ͳ�ȥ,���buffer */
			write_event_disable(conn->conn_event);
			buffer->nelts = 0;
		}
		else
		{
			/* ���ַ���,δ���Ͳ��ְ��Ƶ�buffer��ͷ */
			memcpy(buffer->elts, buffer->elts + n_write, buffer->nelts * buffer->size);
			buffer->nelts -= n_write;
		}
	}
	else if (n_write < 0)
	{
		if (errno != EWOULDBLOCK)
			debug_sys("file: %s, line: %d", __FILE__, __LINE__);
		else
			debug_msg("file: %s, line: %d", __FILE__, __LINE__);
	}
}

connection *create_connection(int conn_fd, server *server)
{
	connection *conn = malloc(sizeof(connection));
	if (conn == NULL)
	{
		debug_ret("file: %s, line: %d", __FILE__, __LINE__);
		return NULL;
	}

	/* Ĭ�ϴ�С�ο�muduo */
	conn->input_buffer = array_create(1024, sizeof(char));
	if (conn->input_buffer == NULL)
	{
		debug_ret("file: %s, line: %d", __FILE__, __LINE__);
		free(conn);
		return NULL;
	}
	conn->output_buffer = array_create(1024, sizeof(char));
	if (conn->output_buffer == NULL)
	{
		debug_ret("file: %s, line: %d", __FILE__, __LINE__);
		array_free(conn->input_buffer);
		free(conn);
		return NULL;
	}

	conn->conn_event = create_event(server->manager, conn_fd, EPOLLIN | EPOLLPRI,
				event_readable_callback, conn, event_writable_callback, conn);
	if (conn->conn_event == NULL)
	{
		debug_ret("file: %s, line: %d", __FILE__, __LINE__);
		array_free(conn->input_buffer);
		array_free(conn->output_buffer);
		free(conn);
		return NULL;
	}

	/* ��TCP��ص�event����Ҫ��Ӧ��connection */
	conn->conn_event->conn = conn;
	event_add(conn->conn_event);
	
	conn->fd = conn_fd;
	conn->server = server;
	conn->client_addr = server->listener->client_addr;
	
	socklen_t addr_len = sizeof(conn->server_addr.addr);
	int ret = getsockname(conn_fd, (struct sockaddr *)&conn->server_addr.addr, &addr_len);
	if (ret < 0)
	{
		/* ��ֹ���� */
		debug_sys("file: %s, line: %d", __FILE__, __LINE__);
	}

	/* ���½��������ӷ���server->connections������ */
	conn->next = server->connections;
	conn->prev = NULL;
	server->connections = conn;
	if (conn->next)
		conn->next->prev = conn;
	
	return conn;
}

/* ��server���Ƴ�һ���ض���connection */
void connection_remove(connection *conn)
{
	server *server = conn->server;
	
	if (conn->prev == NULL)
	{
		server->connections = conn->next;
		if (conn->next != NULL)
			conn->next->prev = NULL;
	}
	else
	{
		conn->prev->next = conn->next;
		if (conn->next != NULL)
			conn->next->prev = conn->prev;
	}
	conn->prev = NULL;
	conn->next = NULL;
}

void connection_free(connection *conn)
{
	connection_remove(conn);
	event_free(conn->conn_event);
	free(conn);
}

void connection_send(connection *conn, char *buf, size_t len)
{
	array *output_buffer = conn->output_buffer;
	ssize_t n_write;
	if (output_buffer->nelts == 0)
	{
		/* ���buffer��û������,ֱ��write */
		n_write = write(conn->fd, buf, len);
		if (n_write < 0)
		{
			n_write = 0;
			
			/* <<UNP>> P341 */
			if (errno != EWOULDBLOCK)
				debug_sys("file: %s, line: %d", __FILE__, __LINE__);
		}
	}
	if (n_write < len)
	{
		/* ������� */
		char *p = array_push_n(output_buffer, len - n_write);
		strncpy(p, buf + n_write, len - n_write);

		/* socket��������д��ص�����event_writable_callback() */
		write_event_enable(conn->conn_event);

		debug_msg("file: %s, line: %d", __FILE__, __LINE__);
	}
}

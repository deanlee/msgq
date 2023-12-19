#pragma once
#include <cstddef>
#include <vector>

int ipc_connect(const char* socket_path);
int ipc_bind(const char* socket_path);
int ipc_send(int fd, void *buf, size_t buf_size, int* fds = nullptr, int num_fds = 0);
int ipc_recv(int fd, void *buf, size_t buf_size, int *fds = nullptr, int num_fds = 0, int *out_num_fds = nullptr);

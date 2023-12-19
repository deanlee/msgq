#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#define getsocket() socket(AF_UNIX, SOCK_STREAM, 0)
#else
#define getsocket() socket(AF_UNIX, SOCK_SEQPACKET, 0)
#endif

#include "cereal/visionipc/ipc.h"

int ipc_connect(const char *socket_path) {
  int sock = getsocket();
  if (sock < 0) return -1;

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
  int err = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (err != 0) {
    close(sock);
    return -1;
  }
  return sock;
}

int ipc_bind(const char *socket_path) {
  unlink(socket_path);

  int sock = getsocket();
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
  int err = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
  assert(err == 0);

  err = listen(sock, 3);
  assert(err == 0);

  return sock;
}

int ipc_send(int fd, void *buf, size_t buf_size, int *fds, int num_fds) {
  struct iovec iov = {.iov_base = buf, .iov_len = buf_size};
  struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
  char control_buf[CMSG_SPACE(sizeof(int) * num_fds)];
  if (num_fds > 0) {
    msg.msg_control = control_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * num_fds);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fds);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * num_fds);
  }
  return sendmsg(fd, &msg, 0);
}

int ipc_recv(int fd, void *buf, size_t buf_size, int *fds, int num_fds, int *out_num_fds) {
  struct iovec iov = {.iov_base = buf, .iov_len = buf_size};
  struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
  char control_buf[CMSG_SPACE(sizeof(int) * num_fds)];
  if (num_fds > 0) {
    msg.msg_control = control_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * num_fds);
  }
  int r = recvmsg(fd, &msg, 0);
  if (r < 0) return r;

  if (msg.msg_controllen > 0) {
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    assert(cmsg);
    assert(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS);
    int recv_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    assert(recv_fds > 0);
    if (msg.msg_flags) {
      return -1;
    }
    *out_num_fds = recv_fds;
    memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * recv_fds);
  }
  // if (msg.msg_flags) {
  //     for (int i=0; i<recv_fds; i++) {
  //       close(fds[i]);
  //     }
  //     return -1;
  //   }

  return r;
}

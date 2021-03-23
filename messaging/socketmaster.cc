#include <assert.h>
#include <time.h>
#include "messaging.hpp"
#include "services.h"

static inline uint64_t nanos_since_boot() {
  struct timespec t;
  clock_gettime(CLOCK_BOOTTIME, &t);
  return t.tv_sec * 1000000000ULL + t.tv_nsec;
}

static const service *get_service(const char *name) {
  for (const auto &it : services) {
    if (strcmp(it.name, name) == 0) return &it;
  }
  return nullptr;
}

static inline bool inList(const std::initializer_list<const char *> &list, const char *value) {
  for (auto &v : list) {
    if (strcmp(value, v) == 0) return true;
  }
  return false;
}

class MessageContext {
public:
  MessageContext() { ctx_ = Context::create(); }
  ~MessageContext() { delete ctx_; }
  Context *ctx_;
};
MessageContext ctx;

struct SubMaster::SubMessage {
  std::string name;
  SubSocket *socket = nullptr;
  int freq = 0;
  bool updated = false, alive = false, valid = false, ignore_alive;
  uint64_t rcv_time = 0, rcv_frame = 0;
  void *allocated_msg_reader = nullptr;
  capnp::FlatArrayMessageReader *msg_reader = nullptr;
  AlignedBuffer aligned_buf;
  cereal::Event::Reader event;
};

SubMaster::SubMaster(const std::initializer_list<const char *> &service_list, const char *address,
                     const std::initializer_list<const char *> &ignore_alive) {
  poller_ = Poller::create();
  for (auto name : service_list) {
    const service *serv = get_service(name);
    assert(serv != nullptr);
    SubSocket *socket = SubSocket::create(ctx.ctx_, name, address ? address : "127.0.0.1", true);
    assert(socket != 0);
    poller_->registerSocket(socket);
    SubMessage *m = new SubMessage{
      .socket = socket,
      .freq = serv->frequency,
      .ignore_alive = inList(ignore_alive, name),
      .allocated_msg_reader = malloc(sizeof(capnp::FlatArrayMessageReader))};
    messages_[socket] = m;
    services_[name] = m;
  }
}

int SubMaster::update(int timeout) {
  if (++frame == UINT64_MAX) frame = 1;
  for (auto &kv : messages_) kv.second->updated = false;

  int updated = 0;
  auto sockets = poller_->poll(timeout);
  uint64_t current_time = nanos_since_boot();
  for (auto s : sockets) {
    Message *msg = s->receive(true);
    if (msg == nullptr) continue;

    SubMessage *m = messages_.at(s);
    if (m->msg_reader) {
      m->msg_reader->~FlatArrayMessageReader();
    }
    m->msg_reader = new (m->allocated_msg_reader) capnp::FlatArrayMessageReader(m->aligned_buf.align(msg));
    delete msg;
    m->event = m->msg_reader->getRoot<cereal::Event>();
    m->updated = true;
    m->rcv_time = current_time;
    m->rcv_frame = frame;
    m->valid = m->event.getValid();

    ++updated;
  }

  for (auto &kv : messages_) {
    SubMessage *m = kv.second;
    m->alive = (m->freq <= (1e-5) || ((current_time - m->rcv_time) * (1e-9)) < (10.0 / m->freq));
  }
  return updated;
}

bool SubMaster::all_(const std::initializer_list<const char *> &service_list, bool valid, bool alive) {
  for (auto &kv : messages_) {
    SubMessage *m = kv.second;
    if (service_list.size() > 0 && !inList(service_list, m->name.c_str())) continue;

    if ((valid && !m->valid) || (alive && !(m->alive && !m->ignore_alive))) {
      return false;
    }
  }
  return true;
}

void SubMaster::drain() {
  while (true) {
    auto polls = poller_->poll(0);
    if (polls.size() == 0)
      break;

    for (auto sock : polls) {
      delete sock->receive(true);
    }
  }
}

bool SubMaster::updated(const char *name) const {
  return services_.at(name)->updated;
}

uint64_t SubMaster::rcv_frame(const char *name) const {
  return services_.at(name)->rcv_frame;
}

cereal::Event::Reader &SubMaster::operator[](const char *name) {
  return services_.at(name)->event;
};

SubMaster::~SubMaster() {
  delete poller_;
  for (auto &kv : messages_) {
    SubMessage *m = kv.second;
    if (m->msg_reader) {
      m->msg_reader->~FlatArrayMessageReader();
    }
    free(m->allocated_msg_reader);
    delete m->socket;
    delete m;
  }
}


// SubMessage::SubMessage(const char *name, const char *address, bool conflate) : name_(name) {
//   // alignedBuffer_ = kj::heapArray<capnp::word>(1024);
//   allocated_msg_reader_ = malloc(sizeof(capnp::FlatArrayMessageReader));

//   socket_ = SubSocket::create(ctx.ctx_, name, address ? address : "127.0.0.1", conflate);
//   assert(socket_ != nullptr);
// }

// // bool SubMessage::receive(bool non_blocking) {
// //   Message *msg = socket_->receive(non_blocking);
// //   if (msg == NULL) return false;

// //   if (msg->getSize() % sizeof(capnp::word) != 0) {
// //     printf("Subsocket(%s) : message length is not a multiple of eight bytes\n", name_.c_str());
// //   }

// //   // Technically we don't know if the bytes are aligned so we'd better copy them to a new
// //   // array.  Note that if we have a non-whole number of words we chop off the straggler bytes.
// //   // This is fine because if those bytes are actually part of the message we will hit an error
// //   // later and if they are not then who cares?
// //   // words = kj::heapArray<word>(allBytes.size() / sizeof(word));
// //   // memcpy(words.begin(), allBytes.begin(), words.size() * sizeof(word));
// //   const size_t size = (msg->getSize() + sizeof(capnp::word) - 1) / sizeof(capnp::word);
// //   if (alignedBuffer_.size() < size) {
// //     alignedBuffer_ = kj::heapArray<capnp::word>(size);
// //   }
// //   memcpy(alignedBuffer_.begin(), msg->getData(), msg->getSize());
// //   delete msg;

// //   if (msg_reader_) {
// //     msg_reader_->~FlatArrayMessageReader();
// //   }
// //   msg_reader_ = new (allocated_msg_reader_) capnp::FlatArrayMessageReader(alignedBuffer_.slice(0, size));
// //   event_ = msg_reader_->getRoot<cereal::Event>();
// //   return true;
// // }

// bool SubMessage::receive(bool non_blocking) {
//   Message *msg = socket_->receive(non_blocking);
//   if (msg == NULL) return false;

//   if (msg->getSize() % sizeof(capnp::word) != 0) {
//     printf("Subsocket(%s) : message length is not a multiple of eight bytes\n", name_.c_str());
//   }

//   char *data = msg->getData();
//   kj::ArrayPtr<const capnp::word> words;
//   if (reinterpret_cast<uintptr_t>(data) % sizeof(void*) == 0) {
//     // Hooray, data is aligned.
//     words = kj::ArrayPtr<const capnp::word>(reinterpret_cast<const capnp::word*>(data), msg->getSize() / sizeof(capnp::word));
//   } else {
//     // Ugh, data not aligned. Make a copy.
//     printf("*****data is not aligned****\n");
//     // Technically we don't know if the bytes are aligned so we'd better copy them to a new
//     // array.  Note that if we have a non-whole number of words we chop off the straggler bytes.
//     // This is fine because if those bytes are actually part of the message we will hit an error
//     // later and if they are not then who cares?
//     const size_t size = msg->getSize() / sizeof(capnp::word);
//     if (alignedBuffer_.size() < size) {
//       alignedBuffer_ = kj::heapArray<capnp::word>(size);
//     }
//     memcpy(alignedBuffer_.begin(), msg->getData(), size * sizeof(capnp::word));
//     words = alignedBuffer_;
//   }

//   if (msg_reader_) {
//     msg_reader_->~FlatArrayMessageReader();
//   }
//   msg_reader_ = new (allocated_msg_reader_) capnp::FlatArrayMessageReader(words);
//   event_ = msg_reader_->getRoot<cereal::Event>();

//   delete msg_;
//   msg_ = msg;
//   return true;
// }


// void SubMessage::drain() {
//   while (true) {
//     Message *msg = socket_->receive(true);
//     if (msg == NULL) break;
    
//     delete msg;
//   }
// }

// SubMessage::~SubMessage() {
//   if (msg_reader_) {
//     msg_reader_->~FlatArrayMessageReader();
//   }
//   free(allocated_msg_reader_);
//   delete socket_;
//   delete msg_;
// }

// struct SubMaster::Message {
//   Message(const char *name, const char *address, int freq, bool ignore_alive)
//       : message(name, address, true), freq(freq), ignore_alive(ignore_alive) {}
//   SubMessage message;
//   int freq = 0;
//   bool updated = false, alive = false, ignore_alive = false;
//   uint64_t rcv_time = 0, rcv_frame = 0;
// };

// SubMaster::SubMaster(const std::initializer_list<const char *> &service_list, const char *address,
//                      const std::initializer_list<const char *> &ignore_alive) {
//   poller_ = Poller::create();
//   for (auto name : service_list) {
//     const service *serv = get_service(name);
//     assert(serv != nullptr);

//     SubMaster::Message *m = new SubMaster::Message(name, address, serv->frequency, inList(ignore_alive, name));
//     poller_->registerSocket(m->message.socket_);
//     messages_[m->message.socket_] = m;
//     services_[name] = m;
//   }
// }

// int SubMaster::update(int timeout) {
//   if (++frame == UINT64_MAX) frame = 1;
//   for (auto &kv : messages_) kv.second->updated = false;

//   int updated = 0;
//   auto sockets = poller_->poll(timeout);
//   uint64_t current_time = nanos_since_boot();
//   for (auto s : sockets) {
//     SubMaster::Message *m = messages_.at(s);
//     if (!m->message.receive(true)) continue;

//     m->updated = true;
//     m->rcv_time = current_time;
//     m->rcv_frame = frame;
//     ++updated;
//   }

//   for (auto &kv : messages_) {
//     SubMaster::Message *m = kv.second;
//     m->alive = (m->freq <= (1e-5) || ((current_time - m->rcv_time) * (1e-9)) < (10.0 / m->freq));
//   }
//   return updated;
// }

// bool SubMaster::all_(const std::initializer_list<const char *> &service_list, bool valid, bool alive) {
//   int found = 0;
//   for (auto &kv : messages_) {
//     SubMaster::Message *m = kv.second;
//     if (service_list.size() == 0 || inList(service_list, m->message.name_.c_str())) {
//       found += (!valid || m->message.getEvent().getValid()) && (!alive || (m->alive && !m->ignore_alive));
//     }
//   }
//   return service_list.size() == 0 ? found == messages_.size() : found == service_list.size();
// }

// void SubMaster::drain() {
//   while (true) {
//     auto polls = poller_->poll(0);
//     if (polls.size() == 0)
//       break;

//     for (auto sock : polls) {
//       delete sock->receive(true);
//     }
//   }
// }

// bool SubMaster::updated(const char *name) const { return services_.at(name)->updated; }

// const cereal::Event::Reader &SubMaster::operator[](const char *name) { 
//   return services_.at(name)->message.getEvent(); 
// };

// uint64_t SubMaster::rcv_frame(const char *name) const {
//   return services_.at(name)->rcv_frame;
// }

// SubMaster::~SubMaster() {
//   delete poller_;
//   for (auto &kv : messages_) {
//     delete kv.second;
//   }
// }

PubMaster::PubMaster(const std::initializer_list<const char *> &service_list) {
  for (auto name : service_list) {
    assert(get_service(name) != nullptr);
    PubSocket *socket = PubSocket::create(ctx.ctx_, name);
    assert(socket);
    sockets_[name] = socket;
  }
}

int PubMaster::send(const char *name, MessageBuilder &msg) {
  auto bytes = msg.toBytes();
  return send(name, bytes.begin(), bytes.size());
}

PubMaster::~PubMaster() {
  for (auto s : sockets_) delete s.second;
}

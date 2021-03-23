#pragma once
#include <cstddef>
#include <time.h>
#include <map>
#include <string>
#include <vector>
#include <kj/common.h>
#include <capnp/serialize.h>
#include "../gen/cpp/log.capnp.h"

#ifdef __APPLE__
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
#endif

#define MSG_MULTIPLE_PUBLISHERS 100

bool messaging_use_zmq();

class Context {
public:
  virtual void * getRawContext() = 0;
  static Context * create();
  virtual ~Context(){};
};

class Message {
public:
  virtual void init(size_t size) = 0;
  virtual void init(char * data, size_t size) = 0;
  virtual void close() = 0;
  virtual size_t getSize() = 0;
  virtual char * getData() = 0;
  virtual ~Message(){};
};


class SubSocket {
public:
  virtual int connect(Context *context, std::string endpoint, std::string address, bool conflate=false, bool check_endpoint=true) = 0;
  virtual void setTimeout(int timeout) = 0;
  virtual Message *receive(bool non_blocking=false) = 0;
  virtual void * getRawSocket() = 0;
  static SubSocket * create();
  static SubSocket * create(Context * context, std::string endpoint, std::string address="127.0.0.1", bool conflate=false, bool check_endpoint=true);
  virtual ~SubSocket(){};
};

class PubSocket {
public:
  virtual int connect(Context *context, std::string endpoint, bool check_endpoint=true) = 0;
  virtual int sendMessage(Message *message) = 0;
  virtual int send(char *data, size_t size) = 0;
  static PubSocket * create();
  static PubSocket * create(Context * context, std::string endpoint, bool check_endpoint=true);
  static PubSocket * create(Context * context, std::string endpoint, int port, bool check_endpoint=true);
  virtual ~PubSocket(){};
};

class Poller {
public:
  virtual void registerSocket(SubSocket *socket) = 0;
  virtual std::vector<SubSocket*> poll(int timeout) = 0;
  static Poller * create();
  static Poller * create(std::vector<SubSocket*> sockets);
  virtual ~Poller(){};
};

// class AlignedArray {
//  public:
//   AlignedArray(const char *data, const size_t size) {
//     if (reinterpret_cast<uintptr_t>(data) % sizeof(void *) == 0) {
//       // Hooray, data is aligned.
//       words_ = kj::ArrayPtr<const capnp::word>(reinterpret_cast<const capnp::word *>(data), size / sizeof(capnp::word));
//     } else {
//       // Ugh, data not aligned. Make a copy.
//       printf("*****data is not aligned****\n");
//       // Technically we don't know if the bytes are aligned so we'd better copy them to a new
//       // array.  Note that if we have a non-whole number of words we chop off the straggler bytes.
//       // This is fine because if those bytes are actually part of the message we will hit an error
//       // later and if they are not then who cares?
//       const size_t words_size = size / sizeof(capnp::word);
//       alignedBuffer_ = kj::heapArray<capnp::word>(words_size);
//       memcpy(alignedBuffer_.begin(), data, words_size * sizeof(capnp::word));
//       words_ = alignedBuffer_;
//     }
//   }
//   inline operator kj::ArrayPtr<const capnp::word>() { return words_; }
//   ~AlignedArray() {}

//  private:
//   kj::Array<capnp::word> alignedBuffer_;
//   kj::ArrayPtr<const capnp::word> words_;
// };

// class SubMessage {
// public:
//   SubMessage(const char *name, const char *address = NULL, int timeout = -1, bool conflate = false);
//   bool receive(bool non_blocking = false);
//   const cereal::Event::Reader &getEvent() { return event; };
//   void drain();
//   ~SubMessage();

// private:
//   friend class SubMaster;
//   std::string name;
//   SubSocket *socket = nullptr;
//   void *allocated_msg_reader = nullptr;
//   capnp::FlatArrayMessageReader *msg_reader = nullptr;
//   kj::Array<capnp::word> buf;
//   cereal::Event::Reader event;

//   // used by class SubMaster
//   int freq = 0;
//   bool updated = false, alive = false, valid = false, ignore_alive;
//   uint64_t rcv_time = 0, rcv_frame = 0;
// };

class SubMaster {
public:
  SubMaster(const std::initializer_list<const char *> &service_list,
            const char *address = nullptr, const std::initializer_list<const char *> &ignore_alive = {});
  int update(int timeout = 1000);
  inline bool allAlive(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, false, true); }
  inline bool allValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, false); }
  inline bool allAliveAndValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, true); }
  void drain();
  ~SubMaster();

  uint64_t frame = 0;
  bool updated(const char *name) const;
  uint64_t rcv_frame(const char *name) const;
  cereal::Event::Reader &operator[](const char *name);

private:
  bool all_(const std::initializer_list<const char *> &service_list, bool valid, bool alive);
  Poller *poller_ = nullptr;
  struct SubMessage;
  std::map<SubSocket *, SubMessage *> messages_;
  std::map<std::string, SubMessage *> services_;
};

// class SubMessage {
//  public:
//   SubMessage(const char *name, const char *address = NULL, bool conflate = false);
//   void setTimeout(int timeout) { socket_->setTimeout(timeout); }
//   bool receive(bool non_blocking = false);
//   const cereal::Event::Reader &getEvent() { return event_; };
//   void drain();
//   ~SubMessage();

//  private:
//   friend class SubMaster;
//   std::string name_;
//   SubSocket *socket_ = nullptr;
//   void *allocated_msg_reader_ = nullptr;
//   capnp::FlatArrayMessageReader *msg_reader_ = nullptr;
//   kj::Array<capnp::word> alignedBuffer_;
//   cereal::Event::Reader event_;
//   Message * msg_ = nullptr;
// };

// class SubMaster {
// public:
//   SubMaster(const std::initializer_list<const char *> &service_list,
//             const char *address = nullptr, const std::initializer_list<const char *> &ignore_alive = {});
//   int update(int timeout = 1000);
//   inline bool allAlive(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, false, true); }
//   inline bool allValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, false); }
//   inline bool allAliveAndValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, true); }
//   void drain();
//   const cereal::Event::Reader &operator[](const char *name);
//   ~SubMaster();

//   uint64_t frame = 0;
//   bool updated(const char *name) const;
//   uint64_t rcv_frame(const char *name) const;

// private:
//   bool all_(const std::initializer_list<const char *> &service_list, bool valid, bool alive);
//   Poller *poller_ = nullptr;
//   struct Message;
//   std::map<SubSocket *, SubMaster::Message *> messages_;
//   std::map<std::string, SubMaster::Message *> services_;
// };

// class SubMaster {
//  public:
//   SubMaster(const std::initializer_list<const char *> &service_list,
//             const char *address = nullptr, const std::initializer_list<const char *> &ignore_alive = {});
//   int update(int timeout = 1000);
//   inline bool allAlive(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, false, true); }
//   inline bool allValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, false); }
//   inline bool allAliveAndValid(const std::initializer_list<const char *> &service_list = {}) { return all_(service_list, true, true); }
//   bool updated(const char *name) const;
//   void drain();
//   cereal::Event::Reader &operator[](const char *name);
//   ~SubMaster();

//  private:
//   bool all_(const std::initializer_list<const char *> &service_list, bool valid, bool alive);
//   Poller *poller_ = nullptr;
//   uint64_t frame_ = 0;
//   struct SubMessage;
//   std::map<SubSocket *, SubMessage *> messages_;
//   std::map<std::string, SubMessage *> services_;
// };


#define STACK_SEGEMENT_WORD_SIZE 512
class MessageBuilder : public capnp::MessageBuilder {
 public:
  MessageBuilder() : firstSegment(true), nextMallocSize(2048), stackSegment{} {}

  ~MessageBuilder() {
    for (auto ptr : moreSegments) {
      free(ptr);
    }
  }

  kj::ArrayPtr<capnp::word> allocateSegment(uint minimumSize) {
    if (firstSegment) {
      firstSegment = false;
      uint size = kj::max(minimumSize, STACK_SEGEMENT_WORD_SIZE);
      if (size <= STACK_SEGEMENT_WORD_SIZE) {
        return kj::ArrayPtr<capnp::word>(stackSegment + 1, size);
      }
    }
    uint size = kj::max(minimumSize, nextMallocSize);
    capnp::word *result = (capnp::word *)calloc(size, sizeof(capnp::word));
    moreSegments.push_back(result);
    nextMallocSize += size;
    return kj::ArrayPtr<capnp::word>(result, size);
  }

  cereal::Event::Builder initEvent(bool valid = true) {
    cereal::Event::Builder event = initRoot<cereal::Event>();
    struct timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    uint64_t current_time = t.tv_sec * 1000000000ULL + t.tv_nsec;
    event.setLogMonoTime(current_time);
    event.setValid(valid);
    return event;
  }

  kj::ArrayPtr<kj::byte> toBytes() {
    auto segments = getSegmentsForOutput();
    if (segments.size() == 1 && segments[0].begin() == stackSegment + 1) {
      const size_t segment_size = segments[0].size();
      uint32_t *table = (uint32_t *)stackSegment;
      table[0] = 0;
      table[1] = segment_size;
      return kj::ArrayPtr<capnp::word>(stackSegment, segment_size + 1).asBytes();
    } else {
      array = capnp::messageToFlatArray(segments);
      return array.asBytes();
    }
  }

 protected:
  kj::Array<capnp::word> array;
  std::vector<capnp::word *> moreSegments;
  bool firstSegment;
  size_t nextMallocSize;
  // the first word of stackSegment is used internally to set the head table.
  alignas(void *) capnp::word stackSegment[1 + STACK_SEGEMENT_WORD_SIZE];
};

// class MessageBuilder : public capnp::MallocMessageBuilder {
//  public:
//   MessageBuilder() = default;

//   cereal::Event::Builder initEvent(bool valid = true) {
//     cereal::Event::Builder event = initRoot<cereal::Event>();
//     struct timespec t;
//     clock_gettime(CLOCK_BOOTTIME, &t);
//     uint64_t current_time = t.tv_sec * 1000000000ULL + t.tv_nsec;
//     event.setLogMonoTime(current_time);
//     event.setValid(valid);
//     return event;
//   }

//   kj::ArrayPtr<capnp::byte> toBytes() {
//     heapArray_ = capnp::messageToFlatArray(*this);
//     return heapArray_.asBytes();
//   }

//  private:
//   kj::Array<capnp::word> heapArray_;
// };

class PubMaster {
public:
  PubMaster(const std::initializer_list<const char *> &service_list);
  inline int send(const char *name, capnp::byte *data, size_t size) { return sockets_.at(name)->send((char *)data, size); }
  int send(const char *name, MessageBuilder &msg);
  ~PubMaster();

private:
  std::map<std::string, PubSocket *> sockets_;
};

class AlignedBuffer {
public:
  kj::ArrayPtr<const capnp::word> align(const char *data, const size_t size) {
    words_size = size / sizeof(capnp::word) + 1;
    if (aligned_buf.size() < words_size) {
      aligned_buf = kj::heapArray<capnp::word>(words_size < 512 ? 512 : words_size);
    }
    memcpy(aligned_buf.begin(), data, size);
    return aligned_buf.slice(0, words_size);
  }
  inline kj::ArrayPtr<const capnp::word> align(Message *m) {
    return align(m->getData(), m->getSize());
  }
private:
  kj::Array<capnp::word> aligned_buf;
  size_t words_size;
};

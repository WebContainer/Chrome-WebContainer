#include <iostream>

#include "base/threading/thread.h"

// https://cs.chromium.org/chromium/src/mojo/edk/BUILD.gn?sq=package:chromium&dr&l=8-9
#include "mojo/edk/embedder/embedder.h"
#include "base/macros.h"

// #include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/wait.h"
// #include "mojo/public/cpp/bindings/interface_request.h"
#include "groundwater/groundwater.mojom.h"

class GroundwaterImpl : public groundwater::Groundwater {
public:
  explicit GroundwaterImpl(groundwater::GroundwaterRequest request)
      : binding_(this, std::move(request)) {}
  ~GroundwaterImpl() override {}
  void Log(const std::string& message) override {
    LOG(ERROR) << "[Logger] " << message;
  }
private:
  mojo::Binding<groundwater::Groundwater> binding_;

  DISALLOW_COPY_AND_ASSIGN(GroundwaterImpl);
};

class GSocketImpl : public groundwater::GroundwaterSocket {
public:
  explicit GSocketImpl(groundwater::GroundwaterSocketRequest request)
      : binding_(this, std::move(request)) {}
  ~GSocketImpl() override {}

  // client synchronous method
  bool Socket(groundwater::SocketPtr* out_result) override {
    // out_result->fd = 101;

    return true;
  };
  
  // async client/server method
  using SocketCallback = base::OnceCallback<void(groundwater::SocketPtr)>;
  void Socket(SocketCallback callback) override {
    // callback;
  };

private:
  mojo::Binding<groundwater::GroundwaterSocket> binding_;

  DISALLOW_COPY_AND_ASSIGN(GSocketImpl);
};

int main() {
  // https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/
  mojo::edk::Init();
  base::Thread ipc_thread("ipc!");
  
  {
    mojo::MessagePipe pipe;

    // NOTE: Because pipes are bi-directional there is no implicit semantic
    // difference between |handle0| or |handle1| here. They're just two ends of a
    // pipe. The choice to treat one as a "client" and one as a "server" is entirely
    // a the API user's decision.
    mojo::ScopedMessagePipeHandle client = std::move(pipe.handle0);
    mojo::ScopedMessagePipeHandle server = std::move(pipe.handle1);
  }

  {
    mojo::ScopedMessagePipeHandle client;
    mojo::ScopedMessagePipeHandle server;
    mojo::CreateMessagePipe(nullptr, &client, &server);
  }

  {
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    mojo::CreateDataPipe(nullptr, &producer, &consumer);

    uint32_t num_bytes = 7;
    producer->WriteData("hihihi", &num_bytes, MOJO_WRITE_DATA_FLAG_NONE);

    // Some time later...

    char buffer[64];
    uint32_t num_bytes_read = 64;
    consumer->ReadData(buffer, &num_bytes_read, MOJO_READ_DATA_FLAG_NONE);

    std::cout << buffer << std::endl;
  }
  
  // {
  //   groundwater::GroundwaterPtr logger;
  //   auto request = mojo::MakeRequest(&logger);

  //   GroundwaterImpl groundwater(std::move(request));
  //   groundwater.Log("Hello World");
  // }

  {
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    mojo::CreateDataPipe(nullptr, &producer, &consumer);

    // Some time later...
    uint32_t num_bytes = 8;
    producer->WriteData("hihihi\0", &num_bytes, MOJO_WRITE_DATA_FLAG_NONE);
      
    MojoResult result = mojo::Wait(consumer.get(), MOJO_HANDLE_SIGNAL_READABLE);
    DCHECK_EQ(result, MOJO_RESULT_OK);

    char buffer[64];
    uint32_t num_bytes_read = 64;
    consumer->ReadData(buffer, &num_bytes_read, MOJO_READ_DATA_FLAG_NONE);
    
    std::cout << buffer << std::endl;
  }

  return 0;
}

#include <iostream>
#include <string>     // std::string, std::to_string

#include "base/threading/thread.h"

// https://cs.chromium.org/chromium/src/mojo/edk/BUILD.gn?sq=package:chromium&dr&l=8-9
#include "mojo/edk/embedder/embedder.h"
#include "base/macros.h"

// #include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/wait.h"
// #include "mojo/public/cpp/bindings/interface_request.h"
#include "groundwater/groundwater.mojom.h"

// https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/README.md#Connecting-Two-Processes
#include "base/process/process_handle.h"
#include "base/threading/thread.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/outgoing_broker_client_invitation.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"

// execve
#include <unistd.h>

base::ProcessHandle LaunchCoolChildProcess(mojo::edk::ScopedPlatformHandle channel) {
  int fildes[2];
  
  pipe(fildes);
  
  // On POSIX, our ProcessHandle will just be the PID.
  // https://cs.chromium.org/chromium/src/base/process/process_handle.h?type=cs&q=base::ProcessHandle&sq=package:chromium&l=40
  int pid = fork();

  if (pid == 0) {
    close(fildes[0]);
    execlp("mocker-client", "mocker-client", std::to_string(fildes[1]).c_str());
  }

  close(fildes[1]);
  return pid;
};


int main() {
  // https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/
  mojo::edk::Init();
  base::Thread ipc_thread("ipc!");
  
  // Example of synchronous send/recieve
  // {
  //   // Initialize the pipes
  //   // TODO: we want one of these pipes to be in our child process.
  //   mojo::ScopedDataPipeProducerHandle producer;
  //   mojo::ScopedDataPipeConsumerHandle consumer;
  //   mojo::CreateDataPipe(nullptr, &producer, &consumer);

  //   // one side sends some data
  //   uint32_t num_bytes = 8;
  //   producer->WriteData("hihihi\0", &num_bytes, MOJO_WRITE_DATA_FLAG_NONE);

  //   // the other side can wait synchronously for data
  //   MojoResult result = mojo::Wait(consumer.get(), MOJO_HANDLE_SIGNAL_READABLE);
  //   DCHECK_EQ(result, MOJO_RESULT_OK);

  //   // This is how we read data out of the pipe
  //   char buffer[64];
  //   uint32_t num_bytes_read = 64;
  //   consumer->ReadData(buffer, &num_bytes_read, MOJO_READ_DATA_FLAG_NONE);
    
  //   std::cout << buffer << std::endl;
  // }

  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));

  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // This is essentially always an OS pipe (domain socket pair, Windows named
  // pipe, etc.)
  mojo::edk::PlatformChannelPair channel;

  // This is a scoper which encapsulates the intent to connect to another
  // process. It exists because process connection is inherently asynchronous,
  // things may go wrong, and the lifetime of any associated resources is bound
  // by the lifetime of this object regardless of success or failure.
  mojo::edk::OutgoingBrokerClientInvitation invitation;

  base::ProcessHandle child_handle =
      LaunchCoolChildProcess(channel.PassClientHandle());

  // At this point it's safe for |invitation| to go out of scope and nothing
  // will break.
  invitation.Send(
    child_handle, 
    mojo::edk::ConnectionParams(
      mojo::edk::TransportProtocol::kLegacy,
      channel.PassServerHandle()
    )
  );

  std::cout << "server" << std::endl;

  return 0;
}

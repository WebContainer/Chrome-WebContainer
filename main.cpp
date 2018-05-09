#include <iostream>
#include <string>     // std::string, std::to_string
#include <vector>

#include <errno.h>

#include "base/threading/thread.h"

// https://cs.chromium.org/chromium/src/mojo/edk/BUILD.gn?sq=package:chromium&dr&l=8-9
#include "mojo/edk/embedder/embedder.h"
#include "base/macros.h"
#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/path_service.h"

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

#include "api.h"

// base::ProcessHandle LaunchCoolChildProcess(mojo::edk::ScopedPlatformHandle channel) {
//   int fildes[2];
  
//   pipe(fildes);
  
//   // On POSIX, our ProcessHandle will just be the PID.
//   // https://cs.chromium.org/chromium/src/base/process/process_handle.h?type=cs&q=base::ProcessHandle&sq=package:chromium&l=40
//   int pid = fork();

//   if (pid == 0) {
//     close(fildes[0]);
//     int out = execl("./out/Default/mocker-client", "mocker-client", std::to_string(fildes[1]).c_str());
    
//     std::cout << strerror(errno) << std::endl;

//     exit(out);
//   }

//   close(fildes[1]);
//   return pid;
// };

class SystemCallsImpl : public groundwater::SystemCalls {
public:
  explicit SystemCallsImpl(groundwater::SystemCallsRequest request)
    : binding_(this, std::move(request)) {}
  void Open(const std::string& filepath, OpenCallback callback) override {
    std::cout << "Opened " << filepath << std::endl;
    std::move(callback).Run(42L);
  }
  void Socket(SocketCallback callback) override {
    std::cout << "Socket" << std::endl;
  }

  void Close(int64_t fd, CloseCallback callback) override {
    std::cout << "Close" << std::endl;
  }

  void Read(int64_t fd, int64_t numBytes, ReadCallback callback) override {
    std::cout << "Read" << std::endl;
  }

private:
  mojo::Binding<groundwater::SystemCalls> binding_;

  DISALLOW_COPY_AND_ASSIGN(SystemCallsImpl);
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
  // mojo::edk::PlatformChannelPair channel;

  // This is a scoper which encapsulates the intent to connect to another
  // process. It exists because process connection is inherently asynchronous,
  // things may go wrong, and the lifetime of any associated resources is bound
  // by the lifetime of this object regardless of success or failure.
  mojo::edk::OutgoingBrokerClientInvitation invitation;

  // base::ProcessHandle child_handle =
      // LaunchCoolChildProcess(channel.PassClientHandle());

  mojo::ScopedMessagePipeHandle primordial_pipe = invitation.AttachMessagePipe("pretty_cool_pipe");

  // Launch Child Process
  base::FilePath client_exe;
  base::PathService::Get(base::DIR_EXE, &client_exe);
  client_exe = client_exe.AppendASCII("mocker-client");
  base::CommandLine command_line(client_exe);
  base::LaunchOptions options;
  mojo::edk::PlatformChannelPair channel;
  channel.PrepareToPassClientHandleToChildProcess(&command_line, &options.fds_to_remap);
  
  base::Process p = base::LaunchProcess(command_line, options);
  DCHECK(p.IsValid());
  channel.ChildProcessLaunched();

  // At this point it's safe for |invitation| to go out of scope and nothing
  // will break.
  invitation.Send(
    p.Handle(), 
    mojo::edk::ConnectionParams(
      mojo::edk::TransportProtocol::kLegacy,
      channel.PassServerHandle()
    )
  );

  base::MessageLoop message_loop;
  base::RunLoop run_loop;

  SystemCallsImpl impl(groundwater::SystemCallsRequest(std::move(primordial_pipe)));

  run_loop.Run();

  /*
  // message loop
  while(1) {
    // Wait for next message.
    // This will return if the child process disappears.
    mojo::Wait(primordial_pipe.get(), MOJO_HANDLE_SIGNAL_READABLE, MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED, nullptr);

    std::vector<uint8_t> bytes;

    MojoResult result = mojo::ReadMessageRaw(primordial_pipe.get(), &bytes, nullptr, MOJO_READ_MESSAGE_FLAG_NONE);
    
    // If the child exits, this will not be MOJO_RESULT_OK so we should break.
    if (result != MOJO_RESULT_OK) break;

    // Pointer magic to recast our incoming bytes to the right struct
    struct SystemCall* syscall = reinterpret_cast<struct SystemCall*>(&bytes[0]);

    // We should switch
    // std::string message(bytes.begin(), bytes.end());
    // std::cout << "server received: " << message << std::endl;
    switch(syscall->name) {
    case SystemCallName::Open:
      std::cout << "server::syscall::open" << std::endl;
      break;
    default:
      std::cout << "server::syscall::UNKNOWN:" << syscall->name << std::endl;
    }

    // Return result
    struct SystemCallReturn syscall_return {
      .code = 1
    };
    mojo::WriteMessageRaw(primordial_pipe.get(),
        static_cast<const void*>(&syscall_return),
        sizeof(syscall_return),
        nullptr,
        0,
        MOJO_WRITE_MESSAGE_FLAG_NONE);
    
  };

  std::cout << "server::end" << std::endl;
  */

  p.WaitForExit(nullptr);

  return 0;
}

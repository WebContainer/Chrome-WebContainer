#include <iostream>
#include <string> // std::string, std::to_string
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "base/threading/thread.h"

// https://cs.chromium.org/chromium/src/mojo/edk/BUILD.gn?sq=package:chromium&dr&l=8-9
#include "base/command_line.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process_handle.h"
#include "base/threading/thread.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/outgoing_broker_client_invitation.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/wait.h"

#include "webcontainer/webcontainer.mojom.h"

// https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/README.md#Connecting-Two-Processes

base::Closure quitClosure;

class SystemCallsImpl : public webcontainer::SystemCalls {
public:
  explicit SystemCallsImpl(webcontainer::SystemCallsRequest request)
      : binding_(this, std::move(request)) {}

  void Open(const std::string &filepath, OpenCallback callback) override {
    int fd = open(filepath.c_str(), O_RDONLY);

    std::move(callback).Run(fd);
  }

  void Close(int64_t fd, CloseCallback callback) override {
    close(fd);
    std::move(callback).Run();
  }

  void Read(int64_t fd, int64_t numBytes, ReadCallback callback) override {
    unsigned char buf[1000000];

    if (numBytes > 1000000) {
      numBytes = 1000000;
    }

    ssize_t len = read(fd, buf, numBytes);
    std::vector<unsigned char> vec(buf, buf + len);

    std::move(callback).Run(vec);
  }

  void Exit(ExitCallback callback) override {
    quitClosure.Run();
    std::move(callback).Run();
  }

  void Print(const std::string &message, PrintCallback callback) override {
    std::cout << message << std::endl;
    std::move(callback).Run();
  }

private:
  mojo::Binding<webcontainer::SystemCalls> binding_;

  DISALLOW_COPY_AND_ASSIGN(SystemCallsImpl);
};

int main(int argc, char **argv) {
  CHECK(base::CommandLine::Init(argc, argv));

  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();

  if (cmd->GetArgs().size() < 2) {
    std::cout << "Usage: " << argv[0] << " INIT_JS_BUNDLE WASM_BUNDLE [OPTIONS]" << std::endl;
    return 1;
  }

  // https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/
  mojo::edk::Init();
  base::Thread ipc_thread("ipc!");

  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));

  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // This is a scoper which encapsulates the intent to connect to another
  // process. It exists because process connection is inherently asynchronous,
  // things may go wrong, and the lifetime of any associated resources is bound
  // by the lifetime of this object regardless of success or failure.
  mojo::edk::OutgoingBrokerClientInvitation invitation;

  mojo::ScopedMessagePipeHandle primordial_pipe =
      invitation.AttachMessagePipe("pretty_cool_pipe");

  // Launch Child Process
  base::FilePath client_exe;
  base::PathService::Get(base::DIR_EXE, &client_exe);
  client_exe = client_exe.AppendASCII("webcontainerc");
  base::CommandLine command_line(client_exe);
  base::LaunchOptions options;
  mojo::edk::PlatformChannelPair channel;
  channel.PrepareToPassClientHandleToChildProcess(&command_line,
                                                  &options.fds_to_remap);

  command_line.AppendSwitchPath(
      "initrd",
      base::FilePath(base::CommandLine::ForCurrentProcess()->GetArgs()[0]));

  command_line.AppendSwitchPath(
      "wasm-bundle",
      base::FilePath(base::CommandLine::ForCurrentProcess()->GetArgs()[1]));

  command_line.AppendSwitchASCII(
      "wasm-args",
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("wasm-args"));

  base::Process p = base::LaunchProcess(command_line, options);
  CHECK(p.IsValid());
  channel.ChildProcessLaunched();

  // At this point it's safe for |invitation| to go out of scope and nothing
  // will break.
  invitation.Send(p.Handle(), mojo::edk::ConnectionParams(
                                  mojo::edk::TransportProtocol::kLegacy,
                                  channel.PassServerHandle()));

  base::MessageLoop message_loop;
  base::RunLoop run_loop;

  SystemCallsImpl impl(
      webcontainer::SystemCallsRequest(std::move(primordial_pipe)));

  std::cout << "BEGIN_RUN" << std::endl;

  quitClosure = run_loop.QuitClosure();
  
  run_loop.Run();

  // On POSIX, if the process has been signaled then |exit_code| is set to -1.
  int childExitCode = 0;
  if (!p.WaitForExit(&childExitCode)) {
    return -2;
  }

  std::cout << "DONE_EXIT:" << childExitCode << std::endl;

  return childExitCode;
}

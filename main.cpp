#include <iostream>
#include <string>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "base/command_line.h"
#include "base/logging.h"
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
#include "mojo/public/cpp/system/handle.h"
#include "mojo/public/cpp/system/platform_handle.h"

#include "mojo/public/c/system/platform_handle.h"

#include "webcontainer/webcontainer.mojom.h"
#include "shared.h"

// Notes:
// https://cs.chromium.org/chromium/src/mojo/edk/BUILD.gn?sq=package:chromium&dr&l=8-9
// https://chromium.googlesource.com/chromium/src/+/master/mojo/edk/embedder/README.md#Connecting-Two-Processes

// After this has been set, call `quitClosure->Run()` will terminate the associated RunLoop
// This means `run_loop->Run()` will stop blocking and the process can exit
base::Closure quitClosure;
int64_t exitCode = 0;

class SystemCallsImpl : public webcontainer::SystemCalls {
public:
  explicit SystemCallsImpl(webcontainer::SystemCallsRequest request)
      : binding_(this, std::move(request)) {}

  void Open(const std::string &filepath, OpenCallback callback) override {
    int fd = open(filepath.c_str(), O_RDONLY);

    std::move(callback).Run(fd);
  }

  void OpenHandle(const std::string &filepath, OpenHandleCallback callback) override {
    DLOG(INFO) << "OpenHandle::Filepath: " << filepath;
    int fd = open(filepath.c_str(), O_RDONLY);
    mojo::ScopedHandle handle = mojo::WrapPlatformFile(fd);    
    std::move(callback).Run(std::move(handle));
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

  void Write(int64_t fd, const std::vector<unsigned char>& buf, WriteCallback callback) override {
    const unsigned char * start = buf.data();
    size_t numBytes = buf.size();
    
    ssize_t len = write(fd, start, numBytes);

    DLOG_IF(WARNING, len < 0) << "Error Writing to FD:" << fd;
    
    std::move(callback).Run();
  }

  void Exit(int64_t _exitCode, ExitCallback callback) override {
    exitCode = _exitCode;
    quitClosure.Run();
    std::move(callback).Run();
  }

  void Print(const std::string &message, PrintCallback callback) override {
    std::cout << message << std::endl;
    std::move(callback).Run();
  }

  void Log(const std::string &message, LogCallback callback) override {
    std::cerr << message << std::endl;
    std::move(callback).Run();
  }

private:
  mojo::Binding<webcontainer::SystemCalls> binding_;

  DISALLOW_COPY_AND_ASSIGN(SystemCallsImpl);
};

int main(int argc, char **argv) {
  CHECK(base::CommandLine::Init(argc, argv));
  base::CommandLine *cmd = base::CommandLine::ForCurrentProcess();

  if (cmd->GetArgs().size() < 2) {
    LOG(WARNING) << "Insufficient Arguments";
    std::cout << "Usage: " << argv[0] << " INIT_JS_BUNDLE WASM_BUNDLE [OPTIONS]"
              << std::endl;
    return 1;
  }

  std::string webcontainerc_command = "webcontainerc";
  if (cmd->HasSwitch("webcontainerc-command")) {
    webcontainerc_command = cmd->GetSwitchValueASCII("webcontainerc-command");
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
      invitation.AttachMessagePipe(WEBCONTAINER_SYSTEM_CALL_PIPE);

  // --------------------
  // Launch Child Process
  // --------------------

  base::FilePath client_exe;
  base::PathService::Get(base::DIR_EXE, &client_exe);
  client_exe = client_exe.AppendASCII(webcontainerc_command);
  base::CommandLine command_line(client_exe);
  base::LaunchOptions options;
  mojo::edk::PlatformChannelPair channel;
  channel.PrepareToPassClientHandleToChildProcess(&command_line,
                                                  &options.fds_to_remap);

  // setup the child process to launch to requested initrd and wasm bundles
  command_line.AppendSwitchPath(
      "initrd",
      base::FilePath(cmd->GetArgs()[0]));

  command_line.AppendSwitchPath(
      "wasm-bundle",
      base::FilePath(cmd->GetArgs()[1]));

  command_line.AppendSwitchASCII(
      "wasm-args",
      cmd->GetSwitchValueASCII("wasm-args"));
  
  // Actually start child process with given command_line
  base::Process p = base::LaunchProcess(command_line, options);
  CHECK(p.IsValid());
  
  // Something about the Mojo IPC handshake
  channel.ChildProcessLaunched();

  // At this point it's safe for |invitation| to go out of scope and nothing
  // will break.
  invitation.Send(p.Handle(), mojo::edk::ConnectionParams(
                                  mojo::edk::TransportProtocol::kLegacy,
                                  channel.PassServerHandle()));

  // Mojo Magic necessary before SystemCallsImpl can be instantiated.
  base::MessageLoop message_loop;
  base::RunLoop run_loop;

  SystemCallsImpl impl(
      webcontainer::SystemCallsRequest(std::move(primordial_pipe)));

  DLOG(INFO) << "BEGIN_RUN";

  // INFO: https://cs.chromium.org/chromium/src/base/run_loop.h?type=cs&q=QuitClosure&sq=package:chromium&l=115-118
  quitClosure = run_loop.QuitClosure();

  // This blocks until quitClosure->Run() is called asynchronously
  run_loop.Run();

  // Copy the exit code from the child `webcontainerc` process
  // On POSIX, if the process has been signaled then |exit_code| is set to -1.
  int childExitCode = 0;
  if (!p.WaitForExit(&childExitCode)) {
    LOG(WARNING) << "Error waiting for child to exit";
    return -2;
  }

  DLOG_IF(ERROR, childExitCode != 0) << "Bad Exit Code";

  // webcontainerd exits with the same code as the wasm-bundle
  return exitCode;
}

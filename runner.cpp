// This file runs the WASM binary

#include <iostream>

#include "base/threading/thread.h"
#include "base/command_line.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/edk/embedder/incoming_broker_client_invitation.h"
#include "mojo/edk/embedder/connection_params.h"
#include "mojo/public/cpp/system/wait.h"

// You write this. It acquires the ScopedPlatformHandle that was passed by
// whomever launched this process (i.e. LaunchCoolChildProcess above).
mojo::edk::ScopedPlatformHandle GetChannelHandle(int fd) {
    return mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd));
}

int main(int argc, char** argv) {
  mojo::edk::Init();
  base::CommandLine::Init(argc, argv);

  // TODO: load WASM binary
  // TODO: setup v8
  // TODO: setup sandbox

  // std::cout << argv[1] << std::endl;

  // Setup IPC with privileged process

  // int fd = atoi(argv[1]);

  base::Thread ipc_thread("ipc!");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));

  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  auto invitation = mojo::edk::IncomingBrokerClientInvitation::AcceptFromCommandLine(
      mojo::edk::TransportProtocol::kLegacy
  );

  mojo::ScopedMessagePipeHandle primordial_pipe =
      invitation->ExtractMessagePipe("pretty_cool_pipe");

  if (primordial_pipe.is_valid()) {
    std::cout << "is_valid()" << std::endl;
  }

  mojo::Wait(primordial_pipe.get(), MOJO_HANDLE_SIGNAL_READABLE,
      MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED, nullptr);

  std::vector<uint8_t> bytes;
  mojo::ReadMessageRaw(primordial_pipe.get(), &bytes, nullptr, MOJO_READ_MESSAGE_FLAG_NONE);
  std::cout << "client received: " << std::string(bytes.begin(), bytes.end()) << std::endl;

  // TODO: setup v8 globals

  // TODO: run WASM

  std::cout << "client" << std::endl;

  return 0;
}

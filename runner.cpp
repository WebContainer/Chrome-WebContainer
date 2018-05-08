// This file runs the WASM binary

#include <iostream>

#include "base/threading/thread.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/edk/embedder/incoming_broker_client_invitation.h"
#include "mojo/edk/embedder/connection_params.h"

// You write this. It acquires the ScopedPlatformHandle that was passed by
// whomever launched this process (i.e. LaunchCoolChildProcess above).
mojo::edk::ScopedPlatformHandle GetChannelHandle(int fd) {
    return mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd));
}

int main(int argc, char** argv) {
  mojo::edk::Init();

  // TODO: load WASM binary
  // TODO: setup v8
  // TODO: setup sandbox

  std::cout << argv[1] << std::endl;

  // Setup IPC with privileged process

  int fd = atoi(argv[1]);

  base::Thread ipc_thread("ipc!");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));

  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  mojo::edk::IncomingBrokerClientInvitation::Accept(
       mojo::edk::ConnectionParams(mojo::edk::TransportProtocol::kLegacy, GetChannelHandle(fd)));

  // TODO: setup v8 globals

  // TODO: run WASM

  std::cout << "client" << std::endl;

  return 0;
}

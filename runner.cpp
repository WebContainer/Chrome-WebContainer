// This file runs the WASM binary

#include <iostream>

#include "base/threading/thread.h"
#include "base/command_line.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/edk/embedder/incoming_broker_client_invitation.h"
#include "mojo/edk/embedder/connection_params.h"
#include "mojo/public/cpp/system/wait.h"

#include "api.h"

#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

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

  // Setup IPC with privileged process

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
    std::cout << "client::is_valid()" << std::endl;
  }

  // from https://chromium.googlesource.com/v8/v8/+/master/samples/hello-world.cc

  // std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);

  v8::Platform* platform = v8::platform::CreateDefaultPlatform();

  v8::V8::InitializePlatform(platform);
  v8::V8::Initialize();

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);

  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    v8::Local<v8::Context> context = v8::Context::New(isolate);

    v8::Context::Scope context_scope(context);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, "'Hello' + ', World!'", v8::NewStringType::kNormal).ToLocalChecked();
    v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();

    // JavaScript executes!
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
    
    v8::String::Utf8Value utf8(isolate, result);
    std::cout << "server::v8::result::" << *utf8 << std::endl;
  }

  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
  delete create_params.array_buffer_allocator;


  int i = 0;
  while(i++ < 5) {
    // TODO: the loop will get removed, and converted into functions passed into our JS world

    // Issue a system call
    struct SystemCall syscall {
      .name = SystemCallName::Open,
      .message = "123456789"
    };

    MojoResult result = mojo::WriteMessageRaw(primordial_pipe.get(),
        static_cast<const void*>(&syscall),
        sizeof(syscall),
        nullptr,
        0,
        MOJO_WRITE_MESSAGE_FLAG_NONE);
    
    if (result != MOJO_RESULT_OK) break; // TODO: conver to error

    // Wait for response
    mojo::Wait(primordial_pipe.get(), MOJO_HANDLE_SIGNAL_READABLE, MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED, nullptr);
    std::vector<uint8_t> bytes;
    result = mojo::ReadMessageRaw(primordial_pipe.get(), &bytes, nullptr, MOJO_READ_MESSAGE_FLAG_NONE);
    if (result != MOJO_RESULT_OK) break; // TODO: conver to error
    struct SystemCallReturn* syscall_result = reinterpret_cast<struct SystemCallReturn*>(&bytes[0]);

    // print our response
    std::cout << "client::result::" << syscall_result->code << std::endl;
    
    // don't kill my laptop
    sleep(1);
  }

  // TODO: setup v8 globals

  // TODO: run WASM

  std::cout << "client::end" << std::endl;

  return 0;
}

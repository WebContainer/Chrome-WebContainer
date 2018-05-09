// This file runs the WASM binary

#include <iostream>

#include "base/threading/thread.h"
#include "base/command_line.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/edk/embedder/incoming_broker_client_invitation.h"
#include "mojo/edk/embedder/connection_params.h"
#include "mojo/public/cpp/system/wait.h"
#include "groundwater/groundwater.mojom.h"
#include "base/run_loop.h"

#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

// You write this. It acquires the ScopedPlatformHandle that was passed by
// whomever launched this process (i.e. LaunchCoolChildProcess above).
mojo::edk::ScopedPlatformHandle GetChannelHandle(int fd) {
    return mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd));
}

groundwater::SystemCallsPtr system_calls_ptr;

// Enumerate syscalls we want to forward over Mojo.

// the open() call
void MojoOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
    std::cout<<"server::js::MojoOpen()"<<std::endl;
    v8::Isolate* isolate = info.GetIsolate();

    v8::Local<v8::Value> arg0 = info[0];
    v8::Local<v8::String> fileStr = arg0->ToString();
    v8::String::Utf8Value utf8Str(isolate, fileStr);

    char * charStr = *utf8Str;

    std::cout << "server::MojoOpen::" << charStr << std::endl;

    int64_t fd = 0;
    system_calls_ptr->Open(std::string(charStr), &fd);

    info.GetReturnValue().Set(v8::Number::New(isolate, fd));
}

// the socket() call
void MojoSocket(const v8::FunctionCallbackInfo<v8::Value> &info) {
    std::cout<<"server::js::MojoSocket()"<<std::endl;

    int64_t fd = 0;
    system_calls_ptr->Socket(&fd);
}

void Printf(const v8::FunctionCallbackInfo<v8::Value> &info) {
    v8::Isolate* isolate = info.GetIsolate();

    v8::Local<v8::Value> arg0 = info[0];
    v8::Local<v8::String> fileStr = arg0->ToString();
    v8::String::Utf8Value utf8Str(isolate, fileStr);
    std::cout << *utf8Str << std::endl;
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

  mojo::ScopedMessagePipeHandle primordial_pipe;
  primordial_pipe =
      invitation->ExtractMessagePipe("pretty_cool_pipe");

  if (primordial_pipe.is_valid()) {
    std::cout << "client::is_valid()" << std::endl;
  }

  base::MessageLoop message_loop;
  base::RunLoop run_loop;
  system_calls_ptr.Bind(groundwater::SystemCallsPtrInfo(std::move(primordial_pipe), 0));

  int64_t fd = 0;
  system_calls_ptr->Open(std::string("hello"), &fd);
  std::cout << "got fd " << fd << std::endl;

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
    
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
    global->Set(
        v8::String::NewFromUtf8(isolate, "open"),
        v8::FunctionTemplate::New(isolate, MojoOpen)
    );
    global->Set(
        v8::String::NewFromUtf8(isolate, "socket"),
        v8::FunctionTemplate::New(isolate, MojoSocket)
    );

    global->Set(
        v8::String::NewFromUtf8(isolate, "print"),
        v8::FunctionTemplate::New(isolate, Printf)
    );

    v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);

    v8::Context::Scope context_scope(context);
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, "print(open(\"foo\")); socket();", v8::NewStringType::kNormal).ToLocalChecked();
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

  // TODO: setup v8 globals

  // TODO: run WASM

  std::cout << "client::end" << std::endl;

  return 0;
}

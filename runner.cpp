// This file runs the WASM binary

#include <iostream>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/threading/thread.h"
#include "mojo/edk/embedder/connection_params.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/incoming_broker_client_invitation.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/system/wait.h"
#include "sandbox/mac/sandbox_compiler.h"
#include "webcontainer/webcontainer.mojom.h"

#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

// You write this. It acquires the ScopedPlatformHandle that was passed by
// whomever launched this process (i.e. LaunchCoolChildProcess above).
mojo::edk::ScopedPlatformHandle GetChannelHandle(int fd) {
  return mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd));
}

webcontainer::SystemCallsPtr system_calls_ptr;
bool continue_js_loop = true;
int32_t exit_code = 0;

void EnableSandbox() {
  std::string sandbox_profile("(version 1)");
  sandbox::SandboxCompiler sandbox_compiler(sandbox_profile);
  std::string err_str;
  bool success = sandbox_compiler.CompileAndApplyProfile(&err_str);
  DLOG_IF(ERROR, !success) << "Failed to enable sandbox: " << err_str;
}

// Enumerate syscalls we want to forward over Mojo.

// the open() call
void MojoOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
  v8::Isolate *isolate = info.GetIsolate();

  v8::Local<v8::Value> arg0 = info[0];
  v8::Local<v8::String> fileStr = arg0->ToString();
  v8::String::Utf8Value utf8Str(isolate, fileStr);

  char *charStr = *utf8Str;

  int64_t fd = 0;
  system_calls_ptr->Open(std::string(charStr), &fd);

  info.GetReturnValue().Set(v8::Number::New(isolate, fd));
}

void MojoExit(const v8::FunctionCallbackInfo<v8::Value> &info) {
  exit_code = info[0]->Int32Value();
  continue_js_loop = false;
  system_calls_ptr->Exit();
}

void MojoRead(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int32_t fd = info[0]->Int32Value();
  int32_t size = info[1]->Int32Value();

  std::vector<uint8_t> bytes;
  system_calls_ptr->Read(fd, size, &bytes);

  v8::Local<v8::ArrayBuffer> ab =
      v8::ArrayBuffer::New(info.GetIsolate(), bytes.size());
  void *start = ab->GetContents().Data();
  size_t length = ab->GetContents().ByteLength();

  std::memcpy(start, &bytes[0], length);

  info.GetReturnValue().Set(ab);
}

void MojoClose(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int32_t fd = info[0]->Int32Value();
  system_calls_ptr->Close(fd);
}

void Printf(const v8::FunctionCallbackInfo<v8::Value> &info) {
  v8::Isolate *isolate = info.GetIsolate();
  v8::Local<v8::Value> arg0 = info[0];
  v8::Local<v8::String> fileStr = arg0->ToString();
  v8::String::Utf8Value utf8Str(isolate, fileStr);

  system_calls_ptr->Print(std::string(*utf8Str));
}

#include <fstream>
#include <streambuf>
#include <string>

#include "base/files/file_util.h"

int main(int argc, char **argv) {

  CHECK(base::CommandLine::Init(argc, argv));
  mojo::edk::Init();

  base::CommandLine *command_line = base::CommandLine::ForCurrentProcess();

  base::FilePath fp = command_line->GetSwitchValuePath("wasm-bundle");
  base::FilePath initrdFile = command_line->GetSwitchValuePath("initrd");

  std::string initrd;

  CHECK(base::ReadFileToString(initrdFile, &initrd));

  CHECK(!fp.empty());

  std::cout << "WASM Bundle: " << fp.value() << std::endl;

  int64_t file_size = -1;
  CHECK(base::GetFileSize(fp, &file_size));
  std::vector<char> wasmbuff(file_size);
  int out = base::ReadFile(fp, wasmbuff.data(), file_size);
  CHECK(out >= 0);

  // Setup IPC with privileged process

  base::Thread ipc_thread("ipc!");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));

  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  auto invitation =
      mojo::edk::IncomingBrokerClientInvitation::AcceptFromCommandLine(
          mojo::edk::TransportProtocol::kLegacy);

  mojo::ScopedMessagePipeHandle primordial_pipe;
  primordial_pipe = invitation->ExtractMessagePipe("pretty_cool_pipe");

  CHECK(primordial_pipe.is_valid());

  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  v8::Platform *platform = v8::platform::CreateDefaultPlatform();
  v8::V8::InitializePlatform(platform);
  v8::V8::Initialize();

  EnableSandbox();

  base::MessageLoop message_loop;
  base::RunLoop run_loop;
  system_calls_ptr.Bind(
      webcontainer::SystemCallsPtrInfo(std::move(primordial_pipe), 0));

  // from
  // https://chromium.googlesource.com/v8/v8/+/master/samples/hello-world.cc

  // std::unique_ptr<v8::Platform> platform =
  // v8::platform::NewDefaultPlatform();

  // PumpMessageLoop
  // https://cs.chromium.org/chromium/src/v8/src/d8.cc?type=cs&q=PumpMessageLoop&l=2970-2973

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate *isolate = v8::Isolate::New(create_params);

  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
    v8::Local<v8::ObjectTemplate> libc = v8::ObjectTemplate::New(isolate);

    global->Set(v8::String::NewFromUtf8(isolate, "wlibc"), libc);

    libc->Set(v8::String::NewFromUtf8(isolate, "open"),
              v8::FunctionTemplate::New(isolate, MojoOpen));
    libc->Set(v8::String::NewFromUtf8(isolate, "read"),
              v8::FunctionTemplate::New(isolate, MojoRead));

    libc->Set(v8::String::NewFromUtf8(isolate, "close"),
              v8::FunctionTemplate::New(isolate, MojoClose));

    libc->Set(v8::String::NewFromUtf8(isolate, "print"),
              v8::FunctionTemplate::New(isolate, Printf));

    libc->Set(v8::String::NewFromUtf8(isolate, "exit"),
              v8::FunctionTemplate::New(isolate, MojoExit));

    v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);

    v8::Context::Scope context_scope(context);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(isolate, initrd.c_str(),
                                v8::NewStringType::kNormal)
            .ToLocalChecked();
    v8::Local<v8::Script> script =
        v8::Script::Compile(context, source).ToLocalChecked();

    v8::Local<v8::ArrayBuffer> wasmArrayBuffer =
        v8::ArrayBuffer::New(isolate, file_size);

    memcpy(wasmArrayBuffer->GetContents().Data(), wasmbuff.data(), file_size);
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__WASMBUNDLE__"),
                           wasmArrayBuffer);

    std::string wasmArgs = command_line->GetSwitchValueASCII("wasm-args");
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__WASMARGS__"),
                           v8::String::NewFromUtf8(isolate, wasmArgs.c_str()));

    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__GLOBAL__"),
                           context->Global());

    // JavaScript executes!
    script->Run(context).ToLocalChecked();

    // our quick and dirty event loop
    while (continue_js_loop && v8::platform::PumpMessageLoop(
        platform, isolate, v8::platform::MessageLoopBehavior::kWaitForWork)) {
      isolate->RunMicrotasks();
    }

  }

  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
  delete create_params.array_buffer_allocator;

  std::cout << "DONE:" << exit_code << std::endl;

  return exit_code;
}

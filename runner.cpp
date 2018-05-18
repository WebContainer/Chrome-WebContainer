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
#include "mojo/public/cpp/system/platform_handle.h"

#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

#include "shared.h"

// You write this. It acquires the ScopedPlatformHandle that was passed by
// whomever launched this process (i.e. LaunchCoolChildProcess above).
mojo::edk::ScopedPlatformHandle GetChannelHandle(int fd) {
  return mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd));
}

webcontainer::SystemCallsPtr system_calls_ptr;
bool continue_js_loop = true;

bool EnableSandbox() {
  std::string sandbox_profile("(version 1)");
  sandbox::SandboxCompiler sandbox_compiler(sandbox_profile);
  std::string err_str;
  return sandbox_compiler.CompileAndApplyProfile(&err_str);
}

// ------------------------------------------------
// Enumerate syscalls we want to forward over Mojo
// ------------------------------------------------

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

int openFileHandles_i = 3;
std::map<int, base::PlatformFile> openFileHandles;

void MojoOpenHandle(const v8::FunctionCallbackInfo<v8::Value> &info) {
  v8::Isolate *isolate = info.GetIsolate();

  v8::Local<v8::Value> arg0 = info[0];
  v8::Local<v8::String> fileStr = arg0->ToString();
  v8::String::Utf8Value utf8Str(isolate, fileStr);

  char *charStr = *utf8Str;

  DLOG(INFO) << "->OpenHandle";

  mojo::ScopedHandle handle;
  system_calls_ptr->OpenHandle(std::string(charStr), &handle);

  DLOG(INFO) << "<-OpenHandleDone";
  
  // base::PlatformFile* file = nullptr;
  base::PlatformFile platform_handle = base::kInvalidPlatformFile;

  DLOG(INFO) << "->Unwrap";

  //TODO check result
  mojo::UnwrapPlatformFile(std::move(handle), &platform_handle);

  openFileHandles[openFileHandles_i++] = platform_handle;

  DLOG(INFO) << "<-Unwrap";

  info.GetReturnValue().Set(v8::Number::New(isolate, openFileHandles_i - 1));
}

void MojoExit(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int64_t exit_code = info[0]->Int32Value();
  continue_js_loop = false;
  system_calls_ptr->Exit(exit_code);
}

void MojoReadHandle(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int32_t fd = info[0]->Int32Value();
  
  v8::ArrayBuffer* ab = v8::ArrayBuffer::Cast(*info[1]);

  int32_t size = info[2]->Int32Value();

  // system_calls_ptr->Read(fd, size, &bytes);
  base::PlatformFile pfile = openFileHandles[fd];
  base::File file(pfile);

  int bytesRead = file.Read(0, (char *) ab->GetContents().Data(), size);

  info.GetReturnValue().Set(bytesRead);
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

void MojoWrite(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int32_t fd = info[0]->Int32Value();
  v8::ArrayBuffer* buffer = v8::ArrayBuffer::Cast(*info[1]);
  size_t size = buffer->GetContents().ByteLength();
  std::vector<uint8_t> bytes(size);

  memcpy(bytes.data(), buffer->GetContents().Data(), size);

  system_calls_ptr->Write(fd, bytes);
}

void MojoClose(const v8::FunctionCallbackInfo<v8::Value> &info) {
  int32_t fd = info[0]->Int32Value();
  system_calls_ptr->Close(fd);
}

void MojoPrintf(const v8::FunctionCallbackInfo<v8::Value> &info) {
  v8::Isolate *isolate = info.GetIsolate();
  v8::Local<v8::Value> arg0 = info[0];
  v8::Local<v8::String> fileStr = arg0->ToString();
  v8::String::Utf8Value utf8Str(isolate, fileStr);

  system_calls_ptr->Print(std::string(*utf8Str));
}

void MojoLog(const v8::FunctionCallbackInfo<v8::Value> &info) {
  v8::Isolate *isolate = info.GetIsolate();
  v8::String::Utf8Value utf8Str(isolate, info[0]->ToString());

  system_calls_ptr->Log(std::string(*utf8Str));
}

#include <fstream>
#include <streambuf>
#include <string>

#include "base/files/file_util.h"

int main(int argc, char **argv) {

  CHECK(base::CommandLine::Init(argc, argv));
  mojo::edk::Init();

  base::CommandLine *command_line = base::CommandLine::ForCurrentProcess();

  // We inject the path for the wasm-bundle, and initrd-js-bundle.
  // No Sandbox is enabled yet, so we should have access to the filesystem.
  base::FilePath fp = command_line->GetSwitchValuePath("wasm-bundle");
  CHECK(!fp.empty());
  
  base::FilePath initrdFile = command_line->GetSwitchValuePath("initrd");
  std::string initrd;
  CHECK(base::ReadFileToString(initrdFile, &initrd));

  DLOG(INFO) << "WASM Bundle: " << fp.value();
  DLOG(INFO) << "Init JS Bundle: " << initrdFile.value();

  // Read the WASM bundle into memory
  int64_t file_size = -1;
  CHECK(base::GetFileSize(fp, &file_size));
  std::vector<char> wasmbuff(file_size);
  int out = base::ReadFile(fp, wasmbuff.data(), file_size);
  CHECK(out >= 0);

  // Setup IPC with privileged process.
  // Most of this is pieced together from Mojo documentation, examples, 
  // and a lot of luck.
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

  // Once the setup dance is done, you can extract named pipes.
  // For each Mojo _Interface_ I think you need a different named pipe.
  primordial_pipe = invitation->ExtractMessagePipe(WEBCONTAINER_SYSTEM_CALL_PIPE);
  CHECK(primordial_pipe.is_valid());

  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  v8::Platform *platform = v8::platform::CreateDefaultPlatform();
  v8::V8::InitializePlatform(platform);
  CHECK(v8::V8::Initialize());

  // This _must_ follow the v8 initialization.
  // Something about external data.
  CHECK(EnableSandbox());

  // -------------------------
  // SANDBOX ENABLED FROM HERE
  // -------------------------
  
  // This is all magic. I don't get why you need MessageLoop,
  // and RunLoop on the stack, but you do.
  // Just live with it.
  base::MessageLoop message_loop;
  base::RunLoop run_loop;  
  // The above is necessary to Bind your pointer to a pipe.
  // I think because messages are dispatched on a separate thread.
  system_calls_ptr.Bind(
      webcontainer::SystemCallsPtrInfo(std::move(primordial_pipe), 0));

  // From:
  // https://chromium.googlesource.com/v8/v8/+/master/samples/hello-world.cc

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate *isolate = v8::Isolate::New(create_params);

  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

    // We create a global `wlibc` which exposes relevant mojoable functions
    v8::Local<v8::ObjectTemplate> libc = v8::ObjectTemplate::New(isolate);
    global->Set(v8::String::NewFromUtf8(isolate, "wlibc"), libc);

    libc->Set(v8::String::NewFromUtf8(isolate, "open"),
              v8::FunctionTemplate::New(isolate, MojoOpen));
    libc->Set(v8::String::NewFromUtf8(isolate, "read"),
              v8::FunctionTemplate::New(isolate, MojoRead));
    libc->Set(v8::String::NewFromUtf8(isolate, "openHandle"),
              v8::FunctionTemplate::New(isolate, MojoOpenHandle));
    libc->Set(v8::String::NewFromUtf8(isolate, "readHandle"),
              v8::FunctionTemplate::New(isolate, MojoReadHandle));
    libc->Set(v8::String::NewFromUtf8(isolate, "close"),
              v8::FunctionTemplate::New(isolate, MojoClose));
    libc->Set(v8::String::NewFromUtf8(isolate, "print"),
              v8::FunctionTemplate::New(isolate, MojoPrintf));
    libc->Set(v8::String::NewFromUtf8(isolate, "log"),
              v8::FunctionTemplate::New(isolate, MojoLog));
    libc->Set(v8::String::NewFromUtf8(isolate, "exit"),
              v8::FunctionTemplate::New(isolate, MojoExit));
    libc->Set(v8::String::NewFromUtf8(isolate, "write"),
              v8::FunctionTemplate::New(isolate, MojoWrite));

    // Setup the JS script
    v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(isolate, initrd.c_str(),
                                v8::NewStringType::kNormal)
            .ToLocalChecked();
    v8::Local<v8::Script> script =
        v8::Script::Compile(context, source).ToLocalChecked();
      
    context->Global()->Set(
      v8::String::NewFromUtf8(isolate, "__DEBUG__"),
      v8::String::NewFromUtf8(
        isolate, 
        command_line->GetSwitchValueASCII("debug").c_str(), 
        v8::NewStringType::kNormal).ToLocalChecked()
    );

    // An ArrayBuffer of the WASM bundle injected into the JS context
    v8::Local<v8::ArrayBuffer> wasmArrayBuffer =
        v8::ArrayBuffer::New(isolate, file_size);
    memcpy(wasmArrayBuffer->GetContents().Data(), wasmbuff.data(), file_size);
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__WASMBUNDLE__"),
                           wasmArrayBuffer);
    
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__WASMBUNDLE_NAME__"), 
                           v8::String::NewFromUtf8(isolate, fp.value().c_str()));

    // The CLI switch --wasm-args=... will be passed to our wasm binary as argv values
    // Currently space-separated, and totally needs more work.
    // TODO: proper argv encoding/parsing
    std::string wasmArgs = command_line->GetSwitchValueASCII("wasm-args");
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__WASMARGS__"),
                           v8::String::NewFromUtf8(isolate, wasmArgs.c_str()));

    // We use __GLOBAL__ instead of global because the browserify process
    // creates its own `global` which would shadow ours.
    context->Global()->Set(v8::String::NewFromUtf8(isolate, "__GLOBAL__"),
                           context->Global());

    // JavaScript executes!
    //
    //----- ALERT -----//
    // This is the first time untrusted code executes
    //----- ALERT -----//
    script->Run(context).ToLocalChecked();

    // our quick and dirty event loop
    while (continue_js_loop &&
            // PumpMessageLoop
            // https://cs.chromium.org/chromium/src/v8/src/d8.cc?type=cs&q=PumpMessageLoop&l=2970-2973
            v8::platform::PumpMessageLoop(
               platform, isolate,
               v8::platform::MessageLoopBehavior::kWaitForWork)) {
      isolate->RunMicrotasks();
    }
  }

  // v8 cleanup from example on embedding v8
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
  delete create_params.array_buffer_allocator;

  // The exit code of the WASM bundle is sent via Mojo.
  // We use this exit code to indicate if webcontainerc is running okay.
  return 0;
}

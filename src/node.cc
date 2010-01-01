// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#include <node.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <node_events.h>
#include <node_dns.h>
#include <node_net.h>
#include <node_file.h>
#include <node_http.h>
#include <node_signal_handler.h>
#include <node_stat.h>
#include <node_timer.h>
#include <node_child_process.h>
#include <node_constants.h>
#include <node_stdio.h>
#include <node_natives.h>
#include <node_version.h>
#include <node_system.h>
#include <node_os.h>
#include <node_process.h>
#include <node_encodings.h>

#include <v8-debug.h>

using namespace v8;

extern char **environ;

namespace node {

static Persistent<Object> process;


static Persistent<String> listeners_symbol;
static Persistent<String> uncaught_exception_symbol;
static Persistent<String> emit_symbol;

static int dash_dash_index = 0;
static bool use_debug_agent = false;

// Extracts a C str from a V8 Utf8Value.
const char* ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<str conversion failed>";
}

static void ReportException(TryCatch *try_catch) {
  Handle<Message> message = try_catch->Message();
  if (message.IsEmpty()) {
    fprintf(stderr, "Error: (no message)\n");
    fflush(stderr);
    return;
  }

  Handle<Value> error = try_catch->Exception();
  Handle<String> stack;

  if (error->IsObject()) {
    Handle<Object> obj = Handle<Object>::Cast(error);
    Handle<Value> raw_stack = obj->Get(String::New("stack"));
    if (raw_stack->IsString()) stack = Handle<String>::Cast(raw_stack);
  }

  if (stack.IsEmpty()) {
    // Print (filename):(line number): (message).
    String::Utf8Value filename(message->GetScriptResourceName());
    const char* filename_string = ToCString(filename);
    int linenum = message->GetLineNumber();
    fprintf(stderr, "%s:%i\n", filename_string, linenum);
    // Print line of source code.
    String::Utf8Value sourceline(message->GetSourceLine());
    const char* sourceline_string = ToCString(sourceline);
    fprintf(stderr, "%s\n", sourceline_string);
    // Print wavy underline (GetUnderline is deprecated).
    int start = message->GetStartColumn();
    for (int i = 0; i < start; i++) {
      fprintf(stderr, " ");
    }
    int end = message->GetEndColumn();
    for (int i = start; i < end; i++) {
      fprintf(stderr, "^");
    }
    fprintf(stderr, "\n");

    message->PrintCurrentStackTrace(stderr);
  } else {
    String::Utf8Value trace(stack);
    fprintf(stderr, "%s\n", *trace);
  }
  fflush(stderr);
}

// Executes a str within the current v8 context.
Handle<Value> ExecuteString(v8::Handle<v8::String> source,
                            v8::Handle<v8::Value> filename) {
  HandleScope scope;
  TryCatch try_catch;

  Handle<Script> script = Script::Compile(source, filename);
  if (script.IsEmpty()) {
    ReportException(&try_catch);
    exit(1);
  }

  Handle<Value> result = script->Run();
  if (result.IsEmpty()) {
    ReportException(&try_catch);
    exit(1);
  }

  return scope.Close(result);
}

static void OnFatalError(const char* location, const char* message) {
  if (location) {
    fprintf(stderr, "FATAL ERROR: %s %s\n", location, message);
  } else {
    fprintf(stderr, "FATAL ERROR: %s\n", message);
  }
  exit(1);
}

static int uncaught_exception_counter = 0;

void FatalException(TryCatch &try_catch) {
  HandleScope scope;

  // Check if uncaught_exception_counter indicates a recursion
  if (uncaught_exception_counter > 0) {
    ReportException(&try_catch);
    exit(1);
  }

  if (listeners_symbol.IsEmpty()) {
    listeners_symbol = NODE_PSYMBOL("listeners");
    uncaught_exception_symbol = NODE_PSYMBOL("uncaughtException");
    emit_symbol = NODE_PSYMBOL("emit");
  }

  Local<Value> listeners_v = process->Get(listeners_symbol);
  assert(listeners_v->IsFunction());

  Local<Function> listeners = Local<Function>::Cast(listeners_v);

  Local<String> uncaught_exception_symbol_l = Local<String>::New(uncaught_exception_symbol);
  Local<Value> argv[1] = { uncaught_exception_symbol_l  };
  Local<Value> ret = listeners->Call(process, 1, argv);

  assert(ret->IsArray());

  Local<Array> listener_array = Local<Array>::Cast(ret);

  uint32_t length = listener_array->Length();
  // Report and exit if process has no "uncaughtException" listener
  if (length == 0) {
    ReportException(&try_catch);
    exit(1);
  }

  // Otherwise fire the process "uncaughtException" event
  Local<Value> emit_v = process->Get(emit_symbol);
  assert(emit_v->IsFunction());

  Local<Function> emit = Local<Function>::Cast(emit_v);

  Local<Value> error = try_catch.Exception();
  Local<Value> event_argv[2] = { uncaught_exception_symbol_l, error };

  uncaught_exception_counter++;
  emit->Call(process, 2, event_argv);
  // Decrement so we know if the next exception is a recursion or not
  uncaught_exception_counter--;
}

static ev_async eio_watcher;

// Called from the main thread.
static void EIOCallback(EV_P_ ev_async *watcher, int revents) {
  assert(watcher == &eio_watcher);
  assert(revents == EV_ASYNC);
  // Give control to EIO to process responses. In nearly every case
  // EIOPromise::After() (file.cc) is called once EIO receives the response.
  eio_poll();
}

// EIOWantPoll() is called from the EIO thread pool each time an EIO
// request (that is, one of the process.fs.* functions) has completed.
static void EIOWantPoll(void) {
  // Signal the main thread that EIO callbacks need to be processed.
  ev_async_send(EV_DEFAULT_UC_ &eio_watcher);
  // EIOCallback() will be called from the main thread in the next event
  // loop.
}

static ev_async debug_watcher;

static void DebugMessageCallback(EV_P_ ev_async *watcher, int revents) {
  HandleScope scope;
  assert(watcher == &debug_watcher);
  assert(revents == EV_ASYNC);
  ExecuteString(String::New("1+1;"),
                String::New("debug_poll"));
}

static void DebugMessageDispatch(void) {
  // This function is called from V8's debug thread when a debug TCP client
  // has sent a message.

  // Send a signal to our main thread saying that it should enter V8 to
  // handle the message.
  ev_async_send(EV_DEFAULT_UC_ &debug_watcher);
}


static void ExecuteNativeJS(const char *filename, const char *data) {
  HandleScope scope;
  TryCatch try_catch;
  ExecuteString(String::New(data), String::New(filename));
  // There should not be any syntax errors in these file!
  // If there are exit the process.
  if (try_catch.HasCaught())  {
    puts("There is an error in Node's built-in javascript");
    puts("This should be reported as a bug!");
    ReportException(&try_catch);
    exit(1);
  }
}

static Local<Object> Load(int argc, char *argv[]) {
  HandleScope scope;

  Local<Object> global = Context::GetCurrent()->Global();

  // Assign the global object to it's place as 'GLOBAL'
  global->Set(String::NewSymbol("GLOBAL"), global);

  // Create the Process constructor
  Local<FunctionTemplate> process_template = FunctionTemplate::New();
  node::EventEmitter::Initialize(process_template);

  // Instantiate the singleton Process as process
  process = Persistent<Object>::New(process_template->GetFunction()->NewInstance());

  // Assign the process object to its place.
  global->Set(String::NewSymbol("process"), process);

  // Initialize the C++ modules....................filenames of module
  Process::Initialize(process);                 // node_process.{h,cc}
  Events::Initialize(process);                  // node_events.{h,cc}
  FileSystem::Initialize(process);              // node_file.{h,cc}
  Os::Initialize(process);                      // node_os.{h,cc}
  System::Initialize(                           // node_system.{h,cc}
    process,
    argc - dash_dash_index, argv + dash_dash_index,
    environ
  );
  Stdio::Initialize(process);                   // node_stdio.{h,cc}
  Timer::Initialize(process);                   // node_timer.{h,cc}
  SignalHandler::Initialize(process);           // node_signal_handler.{h,cc}
  Stat::Initialize(process);                    // node_stat.{h,cc}
  ChildProcess::Initialize(process);            // node_child_process.{h,cc}
  Constants::Initialize(process);               // node_constants.{h,cc}

  // Create process.dns
  Local<Object> dns = Object::New();
  process->Set(String::NewSymbol("dns"), dns);
  DNS::Initialize(dns);                         // node_dns.{h,cc}

  // Create process.fs
  Local<Object> fs = Object::New();
  process->Set(String::NewSymbol("fs"), fs);
  File::Initialize(fs);                         // node_file.{h,cc}

  // Create process.tcp. Note this separate from lib/tcp.js which is the public
  // frontend.
  Local<Object> tcp = Object::New();
  process->Set(String::New("tcp"), tcp);
  Server::Initialize(tcp);                      // node_tcp.{h,cc}
  Connection::Initialize(tcp);                  // node_tcp.{h,cc}

  // Create process.http.  Note this separate from lib/http.js which is the
  // public frontend.
  Local<Object> http = Object::New();
  process->Set(String::New("http"), http);
  HTTPServer::Initialize(http);                 // node_http.{h,cc}
  HTTPConnection::Initialize(http);             // node_http.{h,cc}

  // Compile, execute the src/*.js files. (Which were included a static C
  // strings in node_natives.h)
  // In node.js we actually load the file specified in ARGV[1]
  // so your next reading stop should be node.js!
  ExecuteNativeJS("node.js", native_node);
}

static void PrintHelp() {
  printf("Usage: node [options] [--] script.js [arguments] \n"
         "  -v, --version    print node's version\n"
         "  --debug          enable remote debugging\n" // TODO specify port
         "  --cflags         print pre-processor and compiler flags\n"
         "  --v8-options     print v8 command line options\n\n"
         "Documentation can be found at http://tinyclouds.org/node/api.html"
         " or with 'man node'\n");
}

// Parse node command line arguments.
static void ParseArgs(int *argc, char **argv) {
  // TODO use parse opts
  for (int i = 1; i < *argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--") == 0) {
      dash_dash_index = i;
      break;
    } else if (strcmp(arg, "--debug") == 0) {
      argv[i] = reinterpret_cast<const char*>("");
      use_debug_agent = true;
      dash_dash_index = i;
    } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
      printf("%s\n", NODE_VERSION);
      exit(0);
    } else if (strcmp(arg, "--cflags") == 0) {
      printf("%s\n", NODE_CFLAGS);
      exit(0);
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      PrintHelp();
      exit(0);
    } else if (strcmp(arg, "--v8-options") == 0) {
      argv[i] = reinterpret_cast<const char*>("--help");
      dash_dash_index = i+1;
    }
  }
}

}  // namespace node


int main(int argc, char *argv[]) {
  // Parse a few arguments which are specific to Node.
  node::ParseArgs(&argc, argv);
  // Parse the rest of the args (up to the 'dash_dash_index' (where '--' was
  // in the command line))
  V8::SetFlagsFromCommandLine(&node::dash_dash_index, argv, false);

  // Error out if we don't have a script argument.
  if (argc < 2) {
    fprintf(stderr, "No script was specified.\n");
    node::PrintHelp();
    return 1;
  }

  // Ignore the SIGPIPE
  evcom_ignore_sigpipe();

  // Initialize the default ev loop.
  ev_default_loop(EVFLAG_AUTO);

  // Start the EIO thread pool:
  // 1. Initialize the ev_async watcher which allows for notification from
  // the thread pool (in node::EIOWantPoll) to poll for updates (in
  // node::EIOCallback).
  ev_async_init(&node::eio_watcher, node::EIOCallback);
  // 2. Actaully start the thread pool.
  eio_init(node::EIOWantPoll, NULL);
  // Don't handle more than 10 reqs on each eio_poll(). This is to avoid
  // race conditions. See test/mjsunit/test-eio-race.js
  eio_set_max_poll_reqs(10);
  // 3. Start watcher.
  ev_async_start(EV_DEFAULT_UC_ &node::eio_watcher);
  // 4. Remove a reference to the async watcher. This means we'll drop out
  // of the ev_loop even though eio_watcher is active.
  ev_unref(EV_DEFAULT_UC);

  V8::Initialize();
  HandleScope handle_scope;

  V8::SetFatalErrorHandler(node::OnFatalError);

#define AUTO_BREAK_FLAG "--debugger_auto_break"
  // If the --debug flag was specified then initialize the debug thread.
  if (node::use_debug_agent) {
    // First apply --debugger_auto_break setting to V8. This is so we can
    // enter V8 by just executing any bit of javascript
    V8::SetFlagsFromString(AUTO_BREAK_FLAG, sizeof(AUTO_BREAK_FLAG));
    // Initialize the async watcher for receiving messages from the debug
    // thread and marshal it into the main thread. DebugMessageCallback()
    // is called from the main thread to execute a random bit of javascript
    // - which will give V8 control so it can handle whatever new message
    // had been received on the debug thread.
    ev_async_init(&node::debug_watcher, node::DebugMessageCallback);
    // Set the callback DebugMessageDispatch which is called from the debug
    // thread.
    Debug::SetDebugMessageDispatchHandler(node::DebugMessageDispatch);
    // Start the async watcher.
    ev_async_start(EV_DEFAULT_UC_ &node::debug_watcher);
    // unref it so that we exit the event loop despite it being active.
    ev_unref(EV_DEFAULT_UC);

    // Start the debug thread and it's associated TCP server on port 5858.
    bool r = Debug::EnableAgent("node " NODE_VERSION, 5858);
    // Crappy check that everything went well. FIXME
    assert(r);
    // Print out some information. REMOVEME
    printf("debugger listening on port 5858\n"
           "Use 'd8 --remote_debugger' to access it.\n");
  }

  // Create the one and only Context.
  Persistent<Context> context = Context::New();
  Context::Scope context_scope(context);

  // Create all the objects, load modules, do everything.
  // so your next reading stop should be node::Load()!
  node::Load(argc, argv);

#ifndef NDEBUG
  // Clean up.
  context.Dispose();
  V8::Dispose();
#endif  // NDEBUG
  return 0;
}


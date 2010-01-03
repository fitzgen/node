// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#include <node.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <node_common.h>
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

extern v8::Persistent<v8::Object> process;

extern ev_async eio_watcher;
extern ev_async debug_watcher;

static int dash_dash_index = 0;
static bool use_debug_agent = false;

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


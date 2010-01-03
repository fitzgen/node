// Kris Kowal

#include <node.h>
#include <node_common.h>
#include <node_modules.h>
#include <node_narwhal_js.h>
#include <v8-debug.h>
#include <node_version.h>

#define AUTO_BREAK_FLAG "--debugger_auto_break"

using namespace v8;

extern char **environ;

namespace node {
    extern ev_async eio_watcher;
    extern ev_async debug_watcher;
}

void EvInitialize() {

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
  // 3. Start watcher.
  ev_async_start(EV_DEFAULT_UC_ &node::eio_watcher);
  // 4. Remove a reference to the async watcher. This means we'll drop out
  // of the ev_loop even though eio_watcher is active.
  ev_unref(EV_DEFAULT_UC);

}

void DebugInitialize() {
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

void Main(int argc, char *argv[]) {
  HandleScope scope;
  Local<Object> global = Context::GetCurrent()->Global();
  Local<Object> modules = node::Modules::New(argc, argv, environ);
  // Compile and execute src/node_narwhal.js, which will return the bootstrap
  // function.  Call that function with the module memo primed with native
  // modules.
  Local<Value> narwhal_argv[1] = {modules};
  // "native_node" comes from a static C string in node_native.h, that is
  // generated in a compile step from the content of "src/node.js"
  Local<Value> result = node::ExecuteNativeJS("node_narwhal.js", node::native_node_narwhal);
  Local<Function> factory = Local<Function>::Cast(result);
  factory->Call(global, 1, narwhal_argv);
}

int main(int argc, char *argv[]) {
  // TODO Parse a few arguments which are specific to Node.
  // TODO V8::SetFlagsFromCommandLine(&node::dash_dash_index, argv, false);
  bool use_debug_agent = false;

  EvInitialize();
  V8::Initialize();

  HandleScope handle_scope;

  V8::SetFatalErrorHandler(node::OnFatalError);

  // If the --debug flag was specified then initialize the debug thread.
  if (use_debug_agent)
    DebugInitialize();

  // Create the one and only Context.
  Persistent<Context> context = Context::New();
  Context::Scope context_scope(context);

  // Create all the objects, load modules, do everything.
  // so your next reading stop should be Main()!
  Main(argc, argv);

#ifndef NDEBUG
  // Clean up.
  context.Dispose();
  V8::Dispose();
#endif // NDEBUG

  return 0;
}


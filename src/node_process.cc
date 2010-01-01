
#include <node_process.h>

#include <node.h>
#include <unistd.h>
#include <dlfcn.h> /* dlopen(), dlsym() */
#include <errno.h> /* errno */

using namespace v8;
using namespace node;

Handle<Value> ByteLength(const Arguments& args);
Handle<Value> DLOpen(const Arguments& args);
Handle<Value> MemoryUsage(const Arguments& args);

Handle<Object> Process::process;

Persistent<String> rss_symbol;
Persistent<String> vsize_symbol;
Persistent<String> heap_total_symbol;
Persistent<String> heap_used_symbol;

typedef void (*extInit)(Handle<Object> exports);

void Process::Initialize(Handle<Object> target) {
  Process::process = target;
  target->Set(String::NewSymbol("pid"), Integer::New(getpid()));
  NODE_SET_METHOD(target, "_byteLength", ByteLength);
  NODE_SET_METHOD(target, "dlopen", DLOpen);
  NODE_SET_METHOD(target, "memoryUsage", MemoryUsage);
}

Handle<Value> ByteLength(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::Error(String::New("Bad argument.")));
  }

  Local<Integer> length = Integer::New(DecodeBytes(args[0], ParseEncoding(args[1], UTF8)));

  return scope.Close(length);
}

// DLOpen is node.dlopen(). Used to load 'module.node' dynamically shared
// objects.
Handle<Value> DLOpen(const v8::Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2) return Undefined();

  String::Utf8Value filename(args[0]->ToString()); // Cast
  Local<Object> target = args[1]->ToObject(); // Cast

  // Actually call dlopen().
  // FIXME: This is a blocking function and should be called asynchronously!
  // This function should be moved to file.cc and use libeio to make this
  // system call.
  void *handle = dlopen(*filename, RTLD_LAZY);

  // Handle errors.
  if (handle == NULL) {
    Local<Value> exception = Exception::Error(String::New(dlerror()));
    return ThrowException(exception);
  }

  // Get the init() function from the dynamically shared object.
  void *init_handle = dlsym(handle, "init");
  // Error out if not found.
  if (init_handle == NULL) {
    Local<Value> exception =
      Exception::Error(String::New("No 'init' symbol found in module."));
    return ThrowException(exception);
  }
  extInit init = (extInit)(init_handle); // Cast

  // Execute the C++ module
  init(target);

  return Undefined();
}

// Memory Usage implementation for various platforms:

#ifdef __APPLE__
#define HAVE_GETMEM 1
/* Researched by Tim Becker and Michael Knight
 * http://blog.kuriositaet.de/?p=257
 */

#include <mach/task.h>
#include <mach/mach_init.h>

int getmem(size_t *rss, size_t *vsize) {
  task_t task = MACH_PORT_NULL;
  struct task_basic_info t_info;
  mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

  int r = task_info(mach_task_self(),
                    TASK_BASIC_INFO,
                    (task_info_t)&t_info,
                    &t_info_count);

  if (r != KERN_SUCCESS) return -1;

  *rss = t_info.resident_size;
  *vsize  = t_info.virtual_size;

  return 0;
}
#endif  // __APPLE__

#ifdef __linux__
# define HAVE_GETMEM 1
# include <sys/param.h> /* for MAXPATHLEN */
# include <sys/user.h> /* for PAGE_SIZE */

int getmem(size_t *rss, size_t *vsize) {
  FILE *f = fopen("/proc/self/stat", "r");
  if (!f) return -1;

  int itmp;
  char ctmp;
  char buffer[MAXPATHLEN];

  /* PID */
  if (fscanf(f, "%d ", &itmp) == 0) goto error;
  /* Exec file */
  if (fscanf (f, "%s ", &buffer[0]) == 0) goto error;
  /* State */
  if (fscanf (f, "%c ", &ctmp) == 0) goto error;
  /* Parent process */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* Process group */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* Session id */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* TTY */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* TTY owner process group */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* Flags */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Minor faults (no memory page) */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Minor faults, children */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Major faults (memory page faults) */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Major faults, children */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* utime */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* stime */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* utime, children */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* stime, children */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* jiffies remaining in current time slice */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* 'nice' value */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;
  /* jiffies until next timeout */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* jiffies until next SIGALRM */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* start time (jiffies since system boot) */
  if (fscanf (f, "%d ", &itmp) == 0) goto error;

  /* Virtual memory size */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  *vsize = (size_t) itmp;

  /* Resident set size */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  *rss = (size_t) itmp * PAGE_SIZE;

  /* rlim */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Start of text */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* End of text */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;
  /* Start of stack */
  if (fscanf (f, "%u ", &itmp) == 0) goto error;

  fclose (f);

  return 0;

error:
  fclose (f);
  return -1;
}
#endif  // __linux__

v8::Handle<v8::Value> MemoryUsage(const v8::Arguments& args) {
  HandleScope scope;

#ifndef HAVE_GETMEM
  return ThrowException(Exception::Error(String::New("Not support on your platform. (Talk to Ryan.)")));
#else
  size_t rss, vsize;

  int r = getmem(&rss, &vsize);

  if (r != 0) {
    return ThrowException(Exception::Error(String::New(strerror(errno))));
  }

  Local<Object> info = Object::New();

  if (rss_symbol.IsEmpty()) {
    rss_symbol = NODE_PSYMBOL("rss");
    vsize_symbol = NODE_PSYMBOL("vsize");
    heap_total_symbol = NODE_PSYMBOL("heapTotal");
    heap_used_symbol = NODE_PSYMBOL("heapUsed");
  }

  info->Set(rss_symbol, Integer::NewFromUnsigned(rss));
  info->Set(vsize_symbol, Integer::NewFromUnsigned(vsize));

  // V8 memory usage
  HeapStatistics v8_heap_stats;
  V8::GetHeapStatistics(&v8_heap_stats);
  info->Set(heap_total_symbol,
            Integer::NewFromUnsigned(v8_heap_stats.total_heap_size()));
  info->Set(heap_used_symbol,
            Integer::NewFromUnsigned(v8_heap_stats.used_heap_size()));

  return scope.Close(info);
#endif
}


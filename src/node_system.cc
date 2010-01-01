// Copyright 2009 Kris Kowal <kris@cixar.com> MIT License

#include <node_system.h>

#include <v8.h>
#include <node.h>
#include <node_version.h>

using namespace v8;
using namespace node;

void System::Initialize(Handle<Object> target, int argc, char **argv, char **environ) {

  // version
  target->Set(String::NewSymbol("version"), String::New(NODE_VERSION));
  // installPrefix
  target->Set(String::NewSymbol("installPrefix"), String::New(NODE_PREFIX));

  // platform
#define xstr(s) str(s)
#define str(s) #s
  target->Set(String::NewSymbol("platform"), String::New(xstr(PLATFORM)));

  // args
  int i, j;
  Local<Array> arguments = Array::New(argc + 1);
  arguments->Set(Integer::New(0), String::New(argv[0]));
  for (j = 1, i = 1; i < argc; j++, i++) {
    Local<String> arg = String::New(argv[i]);
    arguments->Set(Integer::New(j), arg);
  }
  // assign it
  target->Set(String::NewSymbol("args"), arguments);
  target->Set(String::NewSymbol("ARGV"), arguments);

  // env
  Local<Object> env = Object::New();
  for (i = 0; environ[i]; i++) {
    // skip entries without a '=' character
    for (j = 0; environ[i][j] && environ[i][j] != '='; j++) { ; }
    // create the v8 objects
    Local<String> field = String::New(environ[i], j);
    Local<String> value = Local<String>();
    if (environ[i][j] == '=') {
      value = String::New(environ[i]+j+1);
    }
    // assign them
    env->Set(field, value);
  }
  // assign ENV
  target->Set(String::NewSymbol("env"), env);
  target->Set(String::NewSymbol("ENV"), env);

  // compile
  NODE_SET_METHOD(target, "compile", System::Compile);
}

Handle<Value> System::Compile(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2) {
    return ThrowException(Exception::TypeError(
          String::New("needs two arguments.")));
  }

  Local<String> source = args[0]->ToString();
  Local<String> filename = args[1]->ToString();

  Handle<Script> script = Script::Compile(source, filename);
  if (script.IsEmpty()) return Undefined();

  Handle<Value> result = script->Run();
  if (result.IsEmpty()) return Undefined();

  return scope.Close(result);
}


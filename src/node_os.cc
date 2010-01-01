// Copyright 2009 Kris Kowal <kris@cixar.com> MIT License
#include <node_os.h>

#include <string.h> // strerror
#include <errno.h>
#include <node_constants.h>

using namespace v8;
using namespace node;

void Os::Initialize(Handle<Object> target) {
  NODE_SET_METHOD(target, "kill", Kill);
  NODE_SET_METHOD(target, "exit", Exit);
}

v8::Handle<v8::Value> Os::Kill(const v8::Arguments& args) {
  HandleScope scope;
  
  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return ThrowException(Exception::Error(String::New("Bad argument.")));
  }
  
  pid_t pid = args[0]->IntegerValue();

  int sig = SIGTERM;

  if (args.Length() >= 2) {
    if (args[1]->IsNumber()) {
      sig = args[1]->Int32Value();
    } else if (args[1]->IsString()) {
      Local<String> signame = args[1]->ToString();

      Local<Value> sig_v = Constants::constants->Get(signame);
      if (!sig_v->IsNumber()) {
        return ThrowException(Exception::Error(String::New("Unknown signal")));
      }
      sig = sig_v->Int32Value();
    }
  }

  int r = kill(pid, sig);

  if (r != 0) {
    return ThrowException(Exception::Error(String::New(strerror(errno))));
  }

  return Undefined();
}

v8::Handle<v8::Value> Os::Exit(const v8::Arguments& args) {
  int r = 0;
  if (args.Length() > 0)
    r = args[0]->IntegerValue();
  fflush(stderr);
  exit(r);
  return Undefined();
}


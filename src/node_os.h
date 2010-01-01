// Copyright 2009 Kris Kowal <kris@cixar.com> MIT License
#ifndef SRC_NODE_OS_H_
#define SRC_NODE_OS_H_

#include <v8.h>
#include <node.h>

namespace node {
  class Os {
   public:
    static void Initialize(v8::Handle<v8::Object> target);
    static v8::Handle<v8::Value> Exit(const v8::Arguments& args);
    static v8::Handle<v8::Value> Kill(const v8::Arguments& args);
  };
}

#endif // SRC_NODE_OS_H_

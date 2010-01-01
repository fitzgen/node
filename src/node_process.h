// Copyright 2009 Kris Kowal <kris@cixar.com> MIT License
#ifndef SRC_NODE_PROCESS_H_
#define SRC_NODE_PROCESS_H_

#include <v8.h>
#include <node.h>

namespace node {
  class Process {
   public:
    static void Initialize(v8::Handle<v8::Object> target);
    static v8::Handle<v8::Object> process;
  };
}

#endif // ndef SRC_NODE_PROCESS_H

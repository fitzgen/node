// Copyright 2009 Kris Kowal <kris@cixar.com> MIT License
#ifndef SRC_NODE_SYSTEM_H_
#define SRC_NODE_SYSTEM_H_

#include <v8.h>

namespace node {
  class System {
   public:
    static void Initialize(v8::Handle<v8::Object> target, int argc, char **argv, char **environ);
    static v8::Handle<v8::Value> Compile(const v8::Arguments& args);
  };
}

#endif // SRC_NODE_SYSTEM_H_

// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#ifndef SRC_CONSTANTS_H_
#define SRC_CONSTANTS_H_

#include <node.h>
#include <v8.h>

namespace node {

class Constants {
 public:
  static void Initialize(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Object> constants;
};

}  // namespace node

#endif  // SRC_CONSTANTS_H_

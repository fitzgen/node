// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#ifndef SRC_FILE_H_
#define SRC_FILE_H_

#include <node.h>
#include <node_events.h>
#include <node_encodings.h>
#include <v8.h>

namespace node {

/* Are you missing your favorite POSIX function? It might be very easy to
 * add a wrapper. Take a look in deps/libeio/eio.h at the list of wrapper
 * functions.  If your function is in that list, just follow the lead of
 * EIOPromise::Open. You'll need to add two functions, one static function
 * in EIOPromise, and one static function which interprets the javascript
 * args in src/file.cc. Then just a reference to that function in
 * File::Initialize() and you should be good to go.
 * Don't forget to add a test to test/mjsunit.
 */

#define NODE_V8_METHOD_DECLARE(name) \
  static v8::Handle<v8::Value> name(const v8::Arguments& args)

class FileSystem {
 public:
  static void Initialize(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> Chdir(const v8::Arguments& args);
  static v8::Handle<v8::Value> Cwd(const v8::Arguments& args);
  static v8::Handle<v8::Value> Umask(const v8::Arguments& args);
};

class File {
 public:
  static void Initialize(v8::Handle<v8::Object> target);
};

}  // namespace node
#endif  // SRC_FILE_H_

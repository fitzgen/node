// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#ifndef NODE_STAT_H_
#define NODE_STAT_H_

#include <node.h>
#include <node_events.h>
#include <ev.h>

namespace node {

class Stat : EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target);

  static v8::Persistent<v8::FunctionTemplate> stats_constructor_template;
  static v8::Persistent<v8::String> dev_symbol;
  static v8::Persistent<v8::String> ino_symbol;
  static v8::Persistent<v8::String> mode_symbol;
  static v8::Persistent<v8::String> nlink_symbol;
  static v8::Persistent<v8::String> uid_symbol;
  static v8::Persistent<v8::String> gid_symbol;
  static v8::Persistent<v8::String> rdev_symbol;
  static v8::Persistent<v8::String> size_symbol;
  static v8::Persistent<v8::String> blksize_symbol;
  static v8::Persistent<v8::String> blocks_symbol;
  static v8::Persistent<v8::String> atime_symbol;
  static v8::Persistent<v8::String> mtime_symbol;
  static v8::Persistent<v8::String> ctime_symbol;
 protected:
  static v8::Persistent<v8::FunctionTemplate> constructor_template;


  Stat() : EventEmitter() {
    persistent_ = false;
    path_ = NULL;
    ev_init(&watcher_, Stat::Callback);
    watcher_.data = this;
  }

  ~Stat() {
    Stop();
    assert(path_ == NULL);
  }

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> Start(const v8::Arguments& args);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);

 private:
  static void Callback(EV_P_ ev_stat *watcher, int revents);

  void Stop();

  ev_stat watcher_;
  bool persistent_;
  char *path_;
};

}  // namespace node
#endif  // NODE_STAT_H_


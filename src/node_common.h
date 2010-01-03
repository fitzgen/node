// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#ifndef SRC_NODE_COMMON_H_
#define SRC_NODE_COMMON_H_

#include <v8.h>
#include <ev.h>

namespace node {

const char* ToCString(const v8::String::Utf8Value& value);
void ReportException(v8::TryCatch *try_catch);
v8::Handle<v8::Value> ExecuteString(v8::Handle<v8::String> source,
                                    v8::Handle<v8::Value> filename);
v8::Handle<v8::Value> ExecuteString(v8::Handle<v8::String> source,
                                    v8::Handle<v8::Value> filename);
void OnFatalError(const char* location, const char* message);
void FatalException(v8::TryCatch &try_catch);

void EIOCallback(EV_P_ ev_async *watcher, int revents);
void EIOWantPoll(void);

void DebugMessageCallback(EV_P_ ev_async *watcher, int revents);
void DebugMessageDispatch(void);
void ExecuteNativeJS(const char *filename, const char *data);

} // namespace node

#endif // ndef SRC_NODE_COMMON_H_

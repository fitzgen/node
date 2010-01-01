// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#ifndef SRC_NODE_ENCODINGS_H_
#define SRC_NODE_ENCODINGS_H_

#include <v8.h>
#include <sys/types.h> /* ssize_t */

namespace node {
enum encoding {ASCII, UTF8, BINARY};
enum encoding ParseEncoding(v8::Handle<v8::Value> encoding_v,
                            enum encoding _default = BINARY);
v8::Local<v8::Value> Encode(const void *buf, size_t len,
                            enum encoding encoding = BINARY);

// Returns -1 if the handle was not valid for decoding
ssize_t DecodeBytes(v8::Handle<v8::Value>,
                    enum encoding encoding = BINARY);

// returns bytes written.
ssize_t DecodeWrite(char *buf,
                    size_t buflen,
                    v8::Handle<v8::Value>,
                    enum encoding encoding = BINARY);

}

#endif // SRC_NODE_ENCODINGS_H_

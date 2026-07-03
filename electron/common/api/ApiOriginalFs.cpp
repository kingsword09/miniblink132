// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "electron/nodeblink.h"
#include "common/NodeRegisterHelp.h"
#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_version.h"
#include <v8.h>
#include <string>

namespace {

void initializeCommonOriginalFsApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    v8::Isolate* isolate = context->GetIsolate();
    v8::Local<v8::Value> processValue;
    if (!context->Global()->Get(context, v8::String::NewFromUtf8(isolate, "process").ToLocalChecked()).ToLocal(&processValue) || !processValue->IsObject())
        return;

    v8::Local<v8::Object> process = processValue.As<v8::Object>();
    v8::Local<v8::Value> getBuiltinValue;
    if (!process->Get(context, v8::String::NewFromUtf8(isolate, "getBuiltinModule").ToLocalChecked()).ToLocal(&getBuiltinValue) || !getBuiltinValue->IsFunction())
        return;

    v8::Local<v8::Value> argv[] = {
        v8::String::NewFromUtf8(isolate, "fs").ToLocalChecked(),
    };
    v8::Local<v8::Value> fsValue;
    if (!getBuiltinValue.As<v8::Function>()->Call(context, process, 1, argv).ToLocal(&fsValue) || !fsValue->IsObject())
        return;

    if (unused->IsObject()) {
        unused.As<v8::Object>()->Set(context, v8::String::NewFromUtf8(isolate, "exports").ToLocalChecked(), fsValue).ToChecked();
        return;
    }

    v8::Local<v8::Array> names = fsValue.As<v8::Object>()->GetOwnPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < names->Length(); ++i) {
        v8::Local<v8::Value> key;
        v8::Local<v8::Value> value;
        if (names->Get(context, i).ToLocal(&key) && fsValue.As<v8::Object>()->Get(context, key).ToLocal(&value))
            exports->Set(context, key, value).ToChecked();
    }
}

} // namespace

static const char CommonOriginalFsSricpt[] = "module.exports = require('fs');";

static NodeNative nativeCommonOriginalFsNative { "original-fs", CommonOriginalFsSricpt, sizeof(CommonOriginalFsSricpt) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_original_fs, initializeCommonOriginalFsApi, &nativeCommonOriginalFsNative)

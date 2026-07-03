// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>

#include "electron/nodeblink.h"
#include "electron/common/PlatformUtil.h"
#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/gin_helper/wrappable.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"

#include "third_party/libnode/src/node.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_version.h"
#include "third_party/libuv/include/uv.h"

namespace {

bool isViewApiEnabled()
{
    return false;
}

} // namespace

void initializeFeaturesApi(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    v8::Isolate* isolate = context->GetIsolate();
    gin_helper::Dictionary dict(isolate, exports);
    dict.Set("isDesktopCapturerEnabled", false);
    dict.SetMethodT("isViewApiEnabled", &isViewApiEnabled);
}

static const char CommonFeaturesNative[] = "exports = {};";
static NodeNative nativeCommonFeaturesNative { "Features", CommonFeaturesNative, sizeof(CommonFeaturesNative) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_common_features, initializeFeaturesApi, &nativeCommonFeaturesNative)

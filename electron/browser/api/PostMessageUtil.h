#ifndef electron_browser_api_PostMessageUtil_h
#define electron_browser_api_PostMessageUtil_h

#include "v8.h"

namespace mojo {
class Connector;
class Message;
}

namespace atom {

bool isValidWrappable(const v8::Local<v8::Value>& val);
bool postMessageHelper(mojo::Connector* m_connector, const v8::FunctionCallbackInfo<v8::Value>& info);
bool onAcceptHelper(v8::Local<v8::Object> wrap, mojo::Message* mojoMessage);

}

#endif // electron_browser_api_PostMessageUtil_h


#include "electron/common/NodeRegisterHelp.h"
#include "electron/common/IdLiveDetect.h"
#include "electron/common/NodeBinding.h"
#include "electron/common/NodeThread.h"
#include "electron/common/AtomVersion.h"
#include "electron/common/V8Util.h"
#include "electron/common/api/EventEmitter.h"
#include "electron/common/api/EventEmitterCaller.h"
#include "electron/common/gin_helper/dictionary.h"
#include "electron/common/gin_helper/object_template_builder.h"
#include "electron/common/gin_helper/public/gin_embedders.h"
#include "electron/common/gin_helper/public/wrapper_info.h"
#include "third_party/libnode/src/node_binding.h"
#include "third_party/libnode/src/node_buffer.h"

#include <string.h>
#include <stdlib.h>

#if defined(OS_MAC)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecItem.h>
#include <Security/SecRandom.h>
#include <array>
#include <vector>
#endif

// electron\shell\browser\api\electron_api_safe_storage.cc
// components/os_crypt/sync/os_crypt_win.cc

namespace OSCrypt {

#if defined(OS_MAC)

namespace {

constexpr size_t kAesKeySize = kCCKeySizeAES256;
constexpr size_t kHmacKeySize = CC_SHA256_DIGEST_LENGTH;
constexpr size_t kKeyMaterialSize = kAesKeySize + kHmacKeySize;
constexpr size_t kIvSize = kCCBlockSizeAES128;
constexpr size_t kMacSize = CC_SHA256_DIGEST_LENGTH;
constexpr char kKeychainService[] = "com.miniblink.electron.safeStorage";
constexpr char kKeychainAccount[] = "safeStorage.v1";

bool makeCFData(const void* data, size_t size, CFDataRef* out)
{
    *out = CFDataCreate(kCFAllocatorDefault, static_cast<const UInt8*>(data), static_cast<CFIndex>(size));
    return *out != nullptr;
}

CFMutableDictionaryRef makeKeychainQuery()
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!query)
        return nullptr;

    CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault, kKeychainService, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault, kKeychainAccount, kCFStringEncodingUTF8);
    if (!service || !account) {
        if (service)
            CFRelease(service);
        if (account)
            CFRelease(account);
        CFRelease(query);
        return nullptr;
    }

    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

bool readKeyFromKeychain(std::array<unsigned char, kKeyMaterialSize>* key)
{
    CFMutableDictionaryRef query = makeKeychainQuery();
    if (!query)
        return false;

    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result)
        return false;

    bool ok = false;
    if (CFGetTypeID(result) == CFDataGetTypeID()) {
        CFDataRef data = static_cast<CFDataRef>(result);
        if (CFDataGetLength(data) == static_cast<CFIndex>(key->size())) {
            memcpy(key->data(), CFDataGetBytePtr(data), key->size());
            ok = true;
        }
    }

    CFRelease(result);
    return ok;
}

bool writeKeyToKeychain(const std::array<unsigned char, kKeyMaterialSize>& key)
{
    CFMutableDictionaryRef query = makeKeychainQuery();
    if (!query)
        return false;

    CFDataRef secretData = nullptr;
    if (!makeCFData(key.data(), key.size(), &secretData)) {
        CFRelease(query);
        return false;
    }

    CFDictionarySetValue(query, kSecValueData, secretData);
    OSStatus status = SecItemAdd(query, nullptr);
    CFRelease(secretData);
    CFRelease(query);
    if (status == errSecSuccess)
        return true;
    if (status != errSecDuplicateItem)
        return false;

    CFMutableDictionaryRef updateQuery = makeKeychainQuery();
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDataRef updatedData = nullptr;
    if (!updateQuery || !attributes || !makeCFData(key.data(), key.size(), &updatedData)) {
        if (updateQuery)
            CFRelease(updateQuery);
        if (attributes)
            CFRelease(attributes);
        if (updatedData)
            CFRelease(updatedData);
        return false;
    }

    CFDictionarySetValue(attributes, kSecValueData, updatedData);
    status = SecItemUpdate(updateQuery, attributes);
    CFRelease(updatedData);
    CFRelease(attributes);
    CFRelease(updateQuery);
    return status == errSecSuccess;
}

bool getOrCreateKey(std::array<unsigned char, kKeyMaterialSize>* key)
{
    const char* testKey = getenv("MINIBLINK_SAFE_STORAGE_TEST_KEY");
    if (testKey && strcmp(testKey, "1") == 0) {
        for (size_t i = 0; i < key->size(); ++i)
            (*key)[i] = static_cast<unsigned char>((i * 37 + 19) & 0xff);
        return true;
    }

    if (readKeyFromKeychain(key))
        return true;

    if (SecRandomCopyBytes(kSecRandomDefault, key->size(), key->data()) != errSecSuccess)
        return false;

    if (writeKeyToKeychain(*key))
        return true;

    return readKeyFromKeychain(key);
}

bool randomBytes(unsigned char* data, size_t size)
{
    return SecRandomCopyBytes(kSecRandomDefault, size, data) == errSecSuccess;
}

void hmacSha256(const unsigned char* key, size_t keySize, const std::string& data, unsigned char* digest)
{
    CCHmac(kCCHmacAlgSHA256, key, keySize, data.data(), data.size(), digest);
}

bool secureEquals(const unsigned char* lhs, const unsigned char* rhs, size_t size)
{
    unsigned char diff = 0;
    for (size_t i = 0; i < size; ++i)
        diff |= lhs[i] ^ rhs[i];
    return diff == 0;
}

} // namespace

bool IsEncryptionAvailable()
{
    std::array<unsigned char, kKeyMaterialSize> key;
    return getOrCreateKey(&key);
}

bool EncryptStringWithPrefix(const std::string& plaintext, const char* prefix, std::string* ciphertext)
{
    std::array<unsigned char, kKeyMaterialSize> key;
    if (!getOrCreateKey(&key))
        return false;

    std::array<unsigned char, kIvSize> iv;
    if (!randomBytes(iv.data(), iv.size()))
        return false;

    std::vector<unsigned char> encrypted(plaintext.size() + kCCBlockSizeAES128);
    size_t encryptedSize = 0;
    CCCryptorStatus status = CCCrypt(kCCEncrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key.data(),
        kAesKeySize,
        iv.data(),
        plaintext.data(),
        plaintext.size(),
        encrypted.data(),
        encrypted.size(),
        &encryptedSize);
    if (status != kCCSuccess)
        return false;

    ciphertext->assign(prefix, strlen(prefix));
    ciphertext->append(reinterpret_cast<const char*>(iv.data()), iv.size());
    ciphertext->append(reinterpret_cast<const char*>(encrypted.data()), encryptedSize);

    std::array<unsigned char, kMacSize> mac;
    hmacSha256(key.data() + kAesKeySize, kHmacKeySize, *ciphertext, mac.data());
    ciphertext->append(reinterpret_cast<const char*>(mac.data()), mac.size());
    return true;
}

bool DecryptString(const std::string& ciphertext, std::string* plaintext)
{
    std::array<unsigned char, kKeyMaterialSize> key;
    if (!getOrCreateKey(&key))
        return false;

    constexpr size_t prefixSize = 3;
    if (ciphertext.size() < prefixSize + kIvSize + kMacSize)
        return false;

    const size_t payloadSize = ciphertext.size() - kMacSize;
    std::array<unsigned char, kMacSize> expectedMac;
    std::string payload(ciphertext.data(), payloadSize);
    hmacSha256(key.data() + kAesKeySize, kHmacKeySize, payload, expectedMac.data());
    const unsigned char* actualMac = reinterpret_cast<const unsigned char*>(ciphertext.data() + payloadSize);
    if (!secureEquals(expectedMac.data(), actualMac, expectedMac.size()))
        return false;

    const unsigned char* iv = reinterpret_cast<const unsigned char*>(ciphertext.data() + prefixSize);
    const char* encryptedData = ciphertext.data() + prefixSize + kIvSize;
    size_t encryptedSize = ciphertext.size() - prefixSize - kIvSize - kMacSize;
    std::vector<unsigned char> decrypted(encryptedSize + kCCBlockSizeAES128);
    size_t decryptedSize = 0;
    CCCryptorStatus status = CCCrypt(kCCDecrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key.data(),
        kAesKeySize,
        iv,
        encryptedData,
        encryptedSize,
        decrypted.data(),
        decrypted.size(),
        &decryptedSize);
    if (status != kCCSuccess)
        return false;

    plaintext->assign(reinterpret_cast<const char*>(decrypted.data()), decryptedSize);
    return true;
}

#else

const char* kEncryptionKey = "AtomEncryptionKey";

bool IsEncryptionAvailable()
{
    return true;
}

static std::string stringXOR(const std::string& content, const std::string& key)
{
    std::string str = content;
    for (unsigned int i = 0; i < str.length(); i++) {
        str[i] ^= key[i % key.length()];
    }

    return str;
}

bool EncryptString(const std::string& plaintext, std::string* ciphertext)
{
    *ciphertext = stringXOR(plaintext, kEncryptionKey);
    return true;
}

bool EncryptStringWithPrefix(const std::string& plaintext, const char* prefix, std::string* ciphertext)
{
    ciphertext->assign(prefix, strlen(prefix));
    ciphertext->append(stringXOR(plaintext, kEncryptionKey));
    return true;
}

bool DecryptString(const std::string& ciphertext, std::string* plaintext)
{
    constexpr size_t prefixSize = 3;
    if (ciphertext.size() < prefixSize)
        return false;
    *plaintext = stringXOR(ciphertext.substr(prefixSize), kEncryptionKey);
    return true;
}

#endif

} // OSCrypt

namespace atom {

const char* kEncryptionVersionPrefixV10 = "v10";
const char* kEncryptionVersionPrefixV11 = "v11";
bool use_password_v10 = false;

static void throwSafeStorageError(v8::Isolate* isolate, const char* message)
{
    isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

static void setUsePasswordV10(bool use)
{
    use_password_v10 = use;
}

static bool isEncryptionAvailable()
{
    return OSCrypt::IsEncryptionAvailable();
}

static v8::Local<v8::Value> encryptString(v8::Isolate* isolate, const std::string& plaintext)
{
    if (!isEncryptionAvailable()) {
        throwSafeStorageError(isolate, "Error while encrypting the text provided to safeStorage.encryptString. Encryption is not available.");
        return v8::Local<v8::Value>();
    }

    const char* prefix = use_password_v10 ? kEncryptionVersionPrefixV10 : kEncryptionVersionPrefixV11;
    std::string ciphertext;
    bool encrypted = OSCrypt::EncryptStringWithPrefix(plaintext, prefix, &ciphertext);

    if (!encrypted) {
        throwSafeStorageError(isolate, "Error while encrypting the text provided to safeStorage.encryptString.");
        return v8::Local<v8::Value>();
    }

    return node::Buffer::Copy(isolate, ciphertext.c_str(), ciphertext.size()).ToLocalChecked();
}

static std::string decryptString(v8::Isolate* isolate, v8::Local<v8::Value> buffer)
{
    if (!isEncryptionAvailable()) {
        throwSafeStorageError(isolate, "Error while decrypting the ciphertext provided to safeStorage.decryptString. Decryption is not available.");
        return "";
    }

    if (!node::Buffer::HasInstance(buffer)) {
        throwSafeStorageError(isolate, "Expected the first argument of decryptString() to be a buffer");
        return "";
    }

    // ensures an error is thrown in Mac or Linux on
    // decryption failure, rather than failing silently
    const char* data = node::Buffer::Data(buffer);
    auto size = node::Buffer::Length(buffer);
    std::string ciphertext(data, size);
    if (ciphertext.empty())
        return "";

    if (ciphertext.find(kEncryptionVersionPrefixV10) != 0 && ciphertext.find(kEncryptionVersionPrefixV11) != 0) {
        throwSafeStorageError(isolate, "Error while decrypting the ciphertext provided to safeStorage.decryptString. Ciphertext does not appear to be encrypted.");
        return "";
    }

    std::string plaintext;
    bool decrypted = OSCrypt::DecryptString(ciphertext, &plaintext);
    if (!decrypted) {
        throwSafeStorageError(isolate, "Error while decrypting the ciphertext provided to safeStorage.decryptString.");
        return "";
    }
    return plaintext;
}

static void initializeApiSafeStorage(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused, v8::Local<v8::Context> context, void* priv)
{
    v8::Isolate* isolate = context->GetIsolate();
    gin_helper::Dictionary dict(isolate, exports);
    dict.SetMethodT("decryptString", &decryptString);
    dict.SetMethodT("encryptString", &encryptString);
    dict.SetMethodT("isEncryptionAvailable", &isEncryptionAvailable);
    dict.SetMethodT("setUsePlainTextEncryption", &setUsePasswordV10);
}

static const char SafeStorageSricpt[] = "exports = {};";

static NodeNative nativeSafeStorageNative { "ApiSafeStorage", SafeStorageSricpt, sizeof(SafeStorageSricpt) - 1 };

NODE_MODULE_CONTEXT_AWARE_BUILTIN_SCRIPT_MANUAL(electron_browser_safe_storage, initializeApiSafeStorage, &nativeSafeStorageNative)

} // atom

#include "src/objects/trusted-object.h"

namespace v8 {
namespace internal {

// Definition https://source.chromium.org/chromium/chromium/src/+/main:v8/src/objects/trusted-object.tq?l=5&c=1
class TorqueGeneratedTrustedObjectAsserts {
  static constexpr int kStartOfWeakFieldsOffset = HeapObject::kHeaderSize;
  static constexpr int kEndOfWeakFieldsOffset = HeapObject::kHeaderSize;
  static constexpr int kStartOfStrongFieldsOffset = HeapObject::kHeaderSize;
  static constexpr int kEndOfStrongFieldsOffset = HeapObject::kHeaderSize;
  static constexpr int kHeaderSize = HeapObject::kHeaderSize;

};

// Definition https://source.chromium.org/chromium/chromium/src/+/main:v8/src/objects/trusted-object.tq?l=9&c=1
class TorqueGeneratedExposedTrustedObjectAsserts {
  // https://source.chromium.org/chromium/chromium/src/+/main:v8/src/objects/trusted-object.tq?l=12&c=26
  static constexpr int kSelfIndirectPointerOffset = TrustedObject::kHeaderSize;
  static constexpr int kSelfIndirectPointerOffsetEnd = kSelfIndirectPointerOffset + kTrustedPointerSize - 1;
  static constexpr int kStartOfWeakFieldsOffset = kSelfIndirectPointerOffsetEnd + 1;
  static constexpr int kEndOfWeakFieldsOffset = kSelfIndirectPointerOffsetEnd + 1;
  static constexpr int kStartOfStrongFieldsOffset = kSelfIndirectPointerOffsetEnd + 1;
  static constexpr int kEndOfStrongFieldsOffset = kSelfIndirectPointerOffsetEnd + 1;
  static constexpr int kHeaderSize = kSelfIndirectPointerOffsetEnd + 1;

  static_assert(kSelfIndirectPointerOffset == ExposedTrustedObject::kSelfIndirectPointerOffset,
                "Values of ExposedTrustedObject::kSelfIndirectPointerOffset defined in Torque and C++ do not match");
};

} // namespace internal
} // namespace v8

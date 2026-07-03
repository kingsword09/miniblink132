#include "mojo/public/cpp/bindings/lib/array_internal.h"
#include "mojo/public/cpp/bindings/lib/validation_util.h"
#include "mojo/public/cpp/bindings/message.h"

#include "base/containers/adapters.h"
#include "components/viz/common/features.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/mojo/test/mojo_interface_request_event.h"

namespace mojo {

Message::Message(uint32_t name,
                 uint32_t flags,
                 size_t payload_size,
                 size_t payload_interface_id_count,
                 std::vector<ScopedHandle>* handles)
    : Message(name,
              flags,
              payload_size,
              payload_interface_id_count,
              MOJO_CREATE_MESSAGE_FLAG_NONE,
              handles)
{
}

uint64_t Message::GetTraceId() const
{
    const internal::MessageHeader* message_header = header();
    if (!message_header)
        return 0;
    return (static_cast<uint64_t>(message_header->name) << 32) |
           static_cast<uint64_t>(message_header->trace_nonce);
}

namespace internal {

ArrayDataTraits<bool>::BitRef::~BitRef() = default;

ArrayDataTraits<bool>::BitRef::BitRef(uint8_t* storage, uint8_t mask)
    : storage_(storage), mask_(mask)
{
}

ArrayDataTraits<bool>::BitRef& ArrayDataTraits<bool>::BitRef::operator=(bool value)
{
    if (value)
        *storage_ |= mask_;
    else
        *storage_ &= ~mask_;
    return *this;
}

ArrayDataTraits<bool>::BitRef::operator bool() const
{
    return (*storage_ & mask_) != 0;
}

ValidationContext::ValidationContext(Message* message,
                                     const char* description,
                                     ValidatorType validator_type)
    : ValidationContext(message ? message->payload() : nullptr,
                        message ? message->payload_num_bytes() : 0,
                        message ? message->handles()->size() : 0,
                        0,
                        message,
                        description,
                        0,
                        validator_type)
{
}

void ReportValidationError(ValidationContext* validation_context,
                           ValidationError error,
                           const char* description)
{
    // The local lite Mojo runtime does not provide bad-message reporting hooks.
    // Validation failures still propagate through the generated boolean checks.
    (void)validation_context;
    (void)error;
    (void)description;
}

void ReportValidationErrorForMessage(mojo::Message* message,
                                     ValidationError error,
                                     const char* interface_name,
                                     unsigned int method_ordinal,
                                     bool is_response)
{
    ValidationContext validation_context(message,
                                         is_response ? "response" : "request",
                                         is_response ? ValidationContext::kResponseValidator
                                                     : ValidationContext::kRequestValidator);
    ReportValidationError(&validation_context, error, interface_name);
}

void ReportNonNullableValidationError(ValidationContext* validation_context,
                                      ValidationError error,
                                      int field_index)
{
    ReportValidationError(validation_context, error, "non-nullable field");
}

bool ValidateStructHeaderAndClaimMemory(const void* data,
                                        ValidationContext* validation_context)
{
    if (!data || !IsAligned(data) ||
        !validation_context->IsValidRange(data, sizeof(StructHeader))) {
        ReportValidationError(validation_context, VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
        return false;
    }

    const StructHeader* header = static_cast<const StructHeader*>(data);
    if (header->num_bytes < sizeof(StructHeader) ||
        !validation_context->ClaimMemory(data, header->num_bytes)) {
        ReportValidationError(validation_context, VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
        return false;
    }
    return true;
}

bool ValidateStructHeaderAndVersionSizeAndClaimMemory(
    const void* data,
    base::span<const StructVersionSize> version_sizes,
    ValidationContext* validation_context)
{
    if (!ValidateStructHeaderAndClaimMemory(data, validation_context))
        return false;

    const StructHeader* header = static_cast<const StructHeader*>(data);
    if (version_sizes.empty())
        return true;

    if (header->version <= version_sizes.back().version) {
        for (const auto& version_size : base::Reversed(version_sizes)) {
            if (header->version >= version_size.version) {
                if (header->num_bytes == version_size.num_bytes)
                    return true;
                ReportValidationError(validation_context,
                                      VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
                return false;
            }
        }
    } else if (header->num_bytes < version_sizes.back().num_bytes) {
        ReportValidationError(validation_context,
                              VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
        return false;
    }

    return true;
}

bool ValidateUnversionedStructHeaderAndSizeAndClaimMemory(
    const void* data,
    size_t v0_size,
    ValidationContext* validation_context)
{
    if (!ValidateStructHeaderAndClaimMemory(data, validation_context))
        return false;

    const StructHeader* header = static_cast<const StructHeader*>(data);
    if ((header->version == 0 && header->num_bytes != v0_size) ||
        header->num_bytes < v0_size) {
        ReportValidationError(validation_context,
                              VALIDATION_ERROR_UNEXPECTED_STRUCT_HEADER);
        return false;
    }
    return true;
}

bool ValidateNonInlinedUnionHeaderAndClaimMemory(
    const void* data,
    ValidationContext* validation_context)
{
    if (!data || !IsAligned(data) ||
        !validation_context->ClaimMemory(data, kUnionDataSize) ||
        *static_cast<const uint32_t*>(data) != kUnionDataSize) {
        ReportValidationError(validation_context, VALIDATION_ERROR_ILLEGAL_MEMORY_RANGE);
        return false;
    }
    return true;
}

bool ValidateMessageIsRequestWithoutResponse(const Message* message,
                                             ValidationContext* validation_context)
{
    const bool valid = !message->has_flag(Message::kFlagIsResponse) &&
                       !message->has_flag(Message::kFlagExpectsResponse);
    if (!valid)
        ReportValidationError(validation_context,
                              VALIDATION_ERROR_MESSAGE_HEADER_INVALID_FLAGS);
    return valid;
}

bool ValidateMessageIsRequestExpectingResponse(const Message* message,
                                               ValidationContext* validation_context)
{
    const bool valid = !message->has_flag(Message::kFlagIsResponse) &&
                       message->has_flag(Message::kFlagExpectsResponse);
    if (!valid)
        ReportValidationError(validation_context,
                              VALIDATION_ERROR_MESSAGE_HEADER_INVALID_FLAGS);
    return valid;
}

bool ValidateMessageIsResponse(const Message* message,
                               ValidationContext* validation_context)
{
    const bool valid = message->has_flag(Message::kFlagIsResponse) &&
                       !message->has_flag(Message::kFlagExpectsResponse);
    if (!valid)
        ReportValidationError(validation_context,
                              VALIDATION_ERROR_MESSAGE_HEADER_INVALID_FLAGS);
    return valid;
}

bool ValidateHandleOrInterfaceNonNullable(const AssociatedInterface_Data& input,
                                          int field_index,
                                          ValidationContext* validation_context)
{
    if (input.handle.is_valid())
        return true;
    ReportNonNullableValidationError(validation_context,
                                     VALIDATION_ERROR_UNEXPECTED_INVALID_INTERFACE_ID,
                                     field_index);
    return false;
}

bool ValidateHandleOrInterfaceNonNullable(
    const AssociatedEndpointHandle_Data& input,
    int field_index,
    ValidationContext* validation_context)
{
    if (input.is_valid())
        return true;
    ReportNonNullableValidationError(validation_context,
                                     VALIDATION_ERROR_UNEXPECTED_INVALID_INTERFACE_ID,
                                     field_index);
    return false;
}

bool ValidateHandleOrInterfaceNonNullable(const Interface_Data& input,
                                          int field_index,
                                          ValidationContext* validation_context)
{
    if (input.handle.is_valid())
        return true;
    ReportNonNullableValidationError(validation_context,
                                     VALIDATION_ERROR_UNEXPECTED_INVALID_HANDLE,
                                     field_index);
    return false;
}

bool ValidateHandleOrInterfaceNonNullable(const Handle_Data& input,
                                          int field_index,
                                          ValidationContext* validation_context)
{
    if (input.is_valid())
        return true;
    ReportNonNullableValidationError(validation_context,
                                     VALIDATION_ERROR_UNEXPECTED_INVALID_HANDLE,
                                     field_index);
    return false;
}

bool ValidateHandleOrInterface(const AssociatedInterface_Data& input,
                               ValidationContext* validation_context)
{
    if (validation_context->ClaimAssociatedEndpointHandle(input.handle))
        return true;
    ReportValidationError(validation_context, VALIDATION_ERROR_ILLEGAL_INTERFACE_ID);
    return false;
}

bool ValidateHandleOrInterface(const AssociatedEndpointHandle_Data& input,
                               ValidationContext* validation_context)
{
    if (validation_context->ClaimAssociatedEndpointHandle(input))
        return true;
    ReportValidationError(validation_context, VALIDATION_ERROR_ILLEGAL_INTERFACE_ID);
    return false;
}

bool ValidateHandleOrInterface(const Interface_Data& input,
                               ValidationContext* validation_context)
{
    if (validation_context->ClaimHandle(input.handle))
        return true;
    ReportValidationError(validation_context, VALIDATION_ERROR_ILLEGAL_HANDLE);
    return false;
}

bool ValidateHandleOrInterface(const Handle_Data& input,
                               ValidationContext* validation_context)
{
    if (validation_context->ClaimHandle(input))
        return true;
    ReportValidationError(validation_context, VALIDATION_ERROR_ILLEGAL_HANDLE);
    return false;
}

}  // namespace internal
}  // namespace mojo

namespace blink {

MojoInterfaceRequestEvent::MojoInterfaceRequestEvent(MojoHandle* handle)
    : Event(event_type_names::kInterfacerequest, Bubbles::kNo, Cancelable::kNo),
      handle_(handle)
{
}

}  // namespace blink

namespace features {

BASE_FEATURE(kTemporalSkipOverlaysWithRootCopyOutputRequests,
             "TemporalSkipOverlaysWithRootCopyOutputRequests",
             base::FEATURE_ENABLED_BY_DEFAULT);

bool IsCVDisplayLinkBeginFrameSourceEnabled()
{
    return false;
}

}  // namespace features

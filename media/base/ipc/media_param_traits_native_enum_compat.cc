// Copyright 2026 The MiniBlink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <type_traits>

#include "base/pickle.h"
#include "ipc/ipc_param_traits.h"
#include "media/base/audio_codecs.h"
#include "media/base/decoder.h"
#include "media/base/encryption_scheme.h"
#include "media/base/video_codecs.h"
#include "media/base/watch_time_keys.h"

namespace IPC {
namespace {

template <typename Enum>
void WriteEnum(base::Pickle* pickle, Enum value)
{
    pickle->WriteInt(static_cast<int>(value));
}

template <typename Enum>
bool ReadEnum(const base::Pickle*, base::PickleIterator* iter, Enum* value, Enum min_value, Enum max_value)
{
    int raw_value = 0;
    if (!iter->ReadInt(&raw_value)) {
        return false;
    }

    using Underlying = std::underlying_type_t<Enum>;
    const auto raw = static_cast<Underlying>(raw_value);
    if (raw < static_cast<Underlying>(min_value) || raw > static_cast<Underlying>(max_value)) {
        return false;
    }

    *value = static_cast<Enum>(raw_value);
    return true;
}

void LogEnum(std::string* log)
{
    log->append("<media enum>");
}

} // namespace

#define DEFINE_MEDIA_ENUM_PARAM_TRAITS(enum_name, min_value, max_value)                                  \
    template <> struct ParamTraits<enum_name> {                                                          \
        using param_type = enum_name;                                                                    \
        static void Write(base::Pickle* pickle, const param_type& value);                                \
        static bool Read(const base::Pickle* pickle, base::PickleIterator* iter, param_type* value);     \
        static void Log(const param_type&, std::string* log);                                            \
    };                                                                                                  \
    void ParamTraits<enum_name>::Write(base::Pickle* pickle, const param_type& value)                    \
    {                                                                                                   \
        WriteEnum(pickle, value);                                                                       \
    }                                                                                                   \
    bool ParamTraits<enum_name>::Read(const base::Pickle* pickle, base::PickleIterator* iter, param_type* value) \
    {                                                                                                   \
        return ReadEnum(pickle, iter, value, min_value, max_value);                                      \
    }                                                                                                   \
    void ParamTraits<enum_name>::Log(const param_type&, std::string* log)                                \
    {                                                                                                   \
        LogEnum(log);                                                                                   \
    }

DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::AudioCodec, media::AudioCodec(), media::AudioCodec::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::VideoCodec, media::VideoCodec(), media::VideoCodec::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::WatchTimeKey, media::WatchTimeKey(), media::WatchTimeKey::kWatchTimeKeyMax)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::AudioDecoderType, media::AudioDecoderType(), media::AudioDecoderType::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::EncryptionScheme, media::EncryptionScheme(), media::EncryptionScheme::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::VideoDecoderType, media::VideoDecoderType(), media::VideoDecoderType::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::AudioCodecProfile, media::AudioCodecProfile(), media::AudioCodecProfile::kMaxValue)
DEFINE_MEDIA_ENUM_PARAM_TRAITS(media::VideoCodecProfile, media::VIDEO_CODEC_PROFILE_MIN, media::VIDEO_CODEC_PROFILE_MAX)

#undef DEFINE_MEDIA_ENUM_PARAM_TRAITS

} // namespace IPC

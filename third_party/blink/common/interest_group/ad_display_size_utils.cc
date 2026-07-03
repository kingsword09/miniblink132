// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/common/interest_group/ad_display_size_utils.h"

#include <cctype>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace blink {

namespace {

blink::AdSize::LengthUnit ConvertUnitStringToUnitEnum(std::string_view input)
{
    if (input == "px") {
        return blink::AdSize::LengthUnit::kPixels;
    }

    if (input == "sw") {
        return blink::AdSize::LengthUnit::kScreenWidth;
    }

    if (input == "sh") {
        return blink::AdSize::LengthUnit::kScreenHeight;
    }

    return blink::AdSize::LengthUnit::kInvalid;
}

bool IsAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

void TrimAsciiWhitespace(std::string_view* input)
{
    while (!input->empty() && std::isspace(static_cast<unsigned char>(input->front()))) {
        input->remove_prefix(1);
    }
    while (!input->empty() && std::isspace(static_cast<unsigned char>(input->back()))) {
        input->remove_suffix(1);
    }
}

bool ParseAdSizeParts(std::string_view input, std::string_view* value, std::string_view* unit)
{
    TrimAsciiWhitespace(&input);
    if (input.empty()) {
        return false;
    }

    size_t pos = 0;
    if (input[pos] == '0') {
        ++pos;
    } else if (input[pos] >= '1' && input[pos] <= '9') {
        do {
            ++pos;
        } while (pos < input.size() && IsAsciiDigit(input[pos]));
    } else {
        return false;
    }

    if (pos < input.size() && input[pos] == '.') {
        ++pos;
        const size_t decimal_start = pos;
        while (pos < input.size() && IsAsciiDigit(input[pos])) {
            ++pos;
        }
        if (pos == decimal_start) {
            return false;
        }
    }

    *value = input.substr(0, pos);
    *unit = input.substr(pos);
    return unit->empty() || *unit == "px" || *unit == "sw" || *unit == "sh";
}

} // namespace

std::string ConvertAdDimensionToString(double value, AdSize::LengthUnit units)
{
    return base::NumberToString(value) + ConvertAdSizeUnitToString(units);
}

std::string ConvertAdSizeUnitToString(const blink::AdSize::LengthUnit& unit)
{
    switch (unit) {
    case blink::AdSize::LengthUnit::kPixels:
        return "px";
    case blink::AdSize::LengthUnit::kScreenWidth:
        return "sw";
    case blink::AdSize::LengthUnit::kScreenHeight:
        return "sh";
    case blink::AdSize::LengthUnit::kInvalid:
        return "";
    }
}

std::string ConvertAdSizeToString(const blink::AdSize& ad_size)
{
    DCHECK(IsValidAdSize(ad_size));
    return base::StrCat(
        { ConvertAdDimensionToString(ad_size.width, ad_size.width_units), ",", ConvertAdDimensionToString(ad_size.height, ad_size.height_units) });
}

std::tuple<double, blink::AdSize::LengthUnit> ParseAdSizeString(std::string_view input)
{
    std::string_view value;
    std::string_view unit;
    if (!ParseAdSizeParts(input, &value, &unit)) {
        // This return value will fail the interest group size validator.
        return { 0.0, blink::AdSize::LengthUnit::kInvalid };
    }

    double length_val = 0.0;
    if (!base::StringToDouble(std::string(value), &length_val)) {
        return { 0.0, blink::AdSize::LengthUnit::kInvalid };
    }

    // If the input consists of pure numbers without an unit, it will be parsed as
    // pixels.
    blink::AdSize::LengthUnit length_units = unit.empty() ? blink::AdSize::LengthUnit::kPixels : ConvertUnitStringToUnitEnum(unit);

    return { length_val, length_units };
}

bool IsValidAdSize(const blink::AdSize& size)
{
    // Disallow non-positive and non-finite values.
    if (size.width <= 0 || size.height <= 0 || !std::isfinite(size.width) || !std::isfinite(size.height)) {
        return false;
    }

    // Disallow invalid units.
    if (size.width_units == blink::AdSize::LengthUnit::kInvalid || size.height_units == blink::AdSize::LengthUnit::kInvalid) {
        return false;
    }

    return true;
}

} // namespace blink

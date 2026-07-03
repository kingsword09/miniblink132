// Copyright 2026 The miniblink Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/ui/ClipboardUtil.h"

#include <cstdlib>

#include "base/strings/string_util.h"
#include "windows.h"

namespace content {

unsigned int ClipboardUtil::getHtmlFormatType()
{
    static unsigned int html_format = ::RegisterClipboardFormat(L"HTML Format");
    return html_format;
}

void ClipboardUtil::cfHtmlExtractMetadata(const std::string& cf_html,
                                          std::string* base_url,
                                          size_t* html_start,
                                          size_t* fragment_start,
                                          size_t* fragment_end)
{
    if (base_url) {
        static const std::string source_url("SourceURL:");
        size_t line_start = cf_html.find(source_url);
        if (line_start != std::string::npos) {
            size_t source_start = line_start + source_url.length();
            size_t source_end = cf_html.find("\n", line_start);
            if (source_end != std::string::npos) {
                *base_url = cf_html.substr(source_start, source_end - source_start);
                base::TrimWhitespaceASCII(*base_url, base::TRIM_ALL, base_url);
            }
        }
    }

    std::string cf_html_lower = base::ToLowerASCII(cf_html);
    size_t markup_start = cf_html_lower.find("<html", 0);
    if (html_start)
        *html_start = markup_start;

    size_t tag_start = cf_html.find("<!--StartFragment", markup_start);
    if (tag_start == std::string::npos) {
        static const std::string start_fragment("StartFragment:");
        size_t start = cf_html.find(start_fragment);
        if (start != std::string::npos && fragment_start) {
            *fragment_start = static_cast<size_t>(
                std::atoi(cf_html.c_str() + start + start_fragment.length()));
        }

        static const std::string end_fragment("EndFragment:");
        size_t end = cf_html.find(end_fragment);
        if (end != std::string::npos && fragment_end) {
            *fragment_end = static_cast<size_t>(
                std::atoi(cf_html.c_str() + end + end_fragment.length()));
        }
        return;
    }

    if (fragment_start)
        *fragment_start = cf_html.find('>', tag_start) + 1;
    if (fragment_end) {
        size_t tag_end = cf_html.rfind("<!--EndFragment", std::string::npos);
        *fragment_end = cf_html.rfind('<', tag_end);
    }
}

}  // namespace content

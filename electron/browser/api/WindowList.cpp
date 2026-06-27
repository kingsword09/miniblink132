// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "WindowList.h"

#include <algorithm>

namespace atom {

// static
WindowList* WindowList::m_instance = nullptr;

namespace {

bool g_isClosingAllWindows = false;
bool g_windowCloseCancelled = false;

} // namespace

// static
WindowList* WindowList::getInstance()
{
    if (!m_instance)
        m_instance = new WindowList;
    return m_instance;
}

// static
void WindowList::addWindow(WindowInterface* window)
{
    if (!window)
        return;

    // Push |window| on the appropriate list instance.
    WindowVector& windows = getInstance()->m_windows;
    windows.push_back(window);
}

// static
void WindowList::removeWindow(WindowInterface* window)
{
    WindowVector& windows = getInstance()->m_windows;
    windows.erase(std::remove(windows.begin(), windows.end(), window), windows.end());
}

WindowInterface* WindowList::find(int id) const
{
    for (WindowVector::const_iterator it = m_windows.begin(); it != m_windows.end(); ++it) {
        if ((*it)->getId() == id)
            return *it;
    }
    return nullptr;
}

// static
void WindowList::WindowCloseCancelled(WindowInterface* window)
{
    if (g_isClosingAllWindows)
        g_windowCloseCancelled = true;
}

// static
bool WindowList::closeAllWindows()
{
    WindowVector windows = getInstance()->m_windows;
    g_isClosingAllWindows = true;
    g_windowCloseCancelled = false;
    for (WindowInterface* window : windows) {
        if (!window || window->isClosed())
            continue;
        window->close();
    }
    bool cancelled = g_windowCloseCancelled;
    g_windowCloseCancelled = false;
    g_isClosingAllWindows = false;
    return !cancelled;
}

WindowList::WindowList()
{
}

WindowList::~WindowList()
{
}

} // namespace atom

namespace {

class TestWindow : public atom::WindowInterface {
public:
    explicit TestWindow(bool cancelClose)
        : cancelClose_(cancelClose)
    {
    }

    bool isClosed() override { return closed_; }

    void close() override
    {
        ++closeCount_;
        if (cancelClose_ && !allowClose_) {
            atom::WindowList::WindowCloseCancelled(this);
            return;
        }
        closed_ = true;
    }

    v8::Local<v8::Object> getWrapper() override { return v8::Local<v8::Object>(); }
    int getId() const override { return id_; }
    atom::WebContents* getWebContents() const override { return nullptr; }
    HWND getHWND() const override { return nullptr; }

    bool cancelClose_ = false;
    bool allowClose_ = false;
    bool closed_ = false;
    int closeCount_ = 0;
    int id_ = 0;
};

} // namespace

extern "C" bool electronWindowListCloseAllWindowsSmokeForTesting()
{
    atom::WindowList* list = atom::WindowList::getInstance();
    if (!list || !list->empty())
        return false;

    TestWindow first(false);
    TestWindow second(true);
    atom::WindowList::addWindow(&first);
    atom::WindowList::addWindow(&second);

    bool firstClose = atom::WindowList::closeAllWindows();
    bool cancelObserved = !firstClose
        && first.closed_
        && !second.closed_
        && first.closeCount_ == 1
        && second.closeCount_ == 1
        && list->size() == 2;

    second.allowClose_ = true;
    bool secondClose = atom::WindowList::closeAllWindows();
    bool retryObserved = secondClose
        && first.closeCount_ == 1
        && second.closeCount_ == 2
        && second.closed_;

    atom::WindowList::removeWindow(&first);
    atom::WindowList::removeWindow(&second);

    return cancelObserved && retryObserved && list->empty();
}

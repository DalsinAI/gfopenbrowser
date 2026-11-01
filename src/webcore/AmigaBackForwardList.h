/*
 * GFOpenBrowser: the page's back/forward list.
 *
 * Adapted from WebKitLegacy's BackForwardList, under this licence:
 *
 * Copyright (C) 2006, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (C) 2009 Google, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <WebCore/BackForwardClient.h>
#include <WebCore/FrameIdentifier.h>
#include <WebCore/HistoryItem.h>
#include <wtf/HashSet.h>
#include <wtf/Vector.h>

namespace GFOpenBrowser {

class AmigaBackForwardList final : public WebCore::BackForwardClient {
public:
    static Ref<AmigaBackForwardList> create() { return adoptRef(*new AmigaBackForwardList); }

    void addItem(Ref<WebCore::HistoryItem>&&) final;
    void setChildItem(WebCore::BackForwardFrameItemIdentifier, Ref<WebCore::HistoryItem>&&) final { }
    void goToItem(WebCore::HistoryItem&) final;

    Vector<Ref<WebCore::HistoryItem>> allItems(WebCore::FrameIdentifier) final { return m_entries; }
    RefPtr<WebCore::HistoryItem> itemAtIndex(int, WebCore::FrameIdentifier) final;

    unsigned backListCount() const final;
    unsigned forwardListCount() const final;
    bool containsItem(const WebCore::HistoryItem&) const final;

    void close() final;

    RefPtr<WebCore::HistoryItem> backItem() const;
    RefPtr<WebCore::HistoryItem> currentItem() const;
    RefPtr<WebCore::HistoryItem> forwardItem() const;

private:
    AmigaBackForwardList() = default;

    static constexpr unsigned capacity = 50;
    static constexpr unsigned noCurrentItemIndex = UINT_MAX;

    Vector<Ref<WebCore::HistoryItem>> m_entries;
    HashSet<RefPtr<WebCore::HistoryItem>> m_entryHash;
    unsigned m_current { noCurrentItemIndex };
};

} // namespace GFOpenBrowser

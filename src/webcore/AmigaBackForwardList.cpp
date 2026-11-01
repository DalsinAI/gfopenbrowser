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
#include "config.h"
#include "AmigaBackForwardList.h"

#include <WebCore/BackForwardCache.h>

namespace GFOpenBrowser {

using namespace WebCore;

void AmigaBackForwardList::addItem(Ref<HistoryItem>&& newItem)
{
    // Drop everything ahead of the current item.
    if (m_current != noCurrentItemIndex) {
        unsigned targetSize = m_current + 1;
        while (m_entries.size() > targetSize) {
            Ref<HistoryItem> item = m_entries.takeLast();
            m_entryHash.remove(item.ptr());
            BackForwardCache::singleton().remove(item);
        }
    }

    // Drop the oldest item when the list is full.
    if (m_entries.size() == capacity && m_current) {
        Ref<HistoryItem> item = WTF::move(m_entries[0]);
        m_entries.removeAt(0);
        m_entryHash.remove(item.ptr());
        BackForwardCache::singleton().remove(item);
        --m_current;
    }

    m_entryHash.add(newItem.ptr());
    m_entries.insert(m_current + 1, WTF::move(newItem));
    ++m_current;
}

void AmigaBackForwardList::goToItem(HistoryItem& item)
{
    for (unsigned index = 0; index < m_entries.size(); ++index) {
        if (m_entries[index].ptr() == &item) {
            m_current = index;
            return;
        }
    }
}

RefPtr<HistoryItem> AmigaBackForwardList::backItem() const
{
    if (m_current && m_current != noCurrentItemIndex)
        return m_entries[m_current - 1].copyRef();
    return nullptr;
}

RefPtr<HistoryItem> AmigaBackForwardList::currentItem() const
{
    if (m_current != noCurrentItemIndex)
        return m_entries[m_current].copyRef();
    return nullptr;
}

RefPtr<HistoryItem> AmigaBackForwardList::forwardItem() const
{
    if (m_entries.size() && m_current < m_entries.size() - 1)
        return m_entries[m_current + 1].copyRef();
    return nullptr;
}

unsigned AmigaBackForwardList::backListCount() const
{
    return m_current == noCurrentItemIndex ? 0 : m_current;
}

unsigned AmigaBackForwardList::forwardListCount() const
{
    return m_current == noCurrentItemIndex ? 0 : m_entries.size() - m_current - 1;
}

RefPtr<HistoryItem> AmigaBackForwardList::itemAtIndex(int index, FrameIdentifier)
{
    // Range checks without arithmetic on index, which could overflow.
    if (m_current == noCurrentItemIndex)
        return nullptr;
    if (index < -static_cast<int>(m_current))
        return nullptr;
    if (index > static_cast<int>(forwardListCount()))
        return nullptr;
    return m_entries[index + m_current].copyRef();
}

bool AmigaBackForwardList::containsItem(const HistoryItem& entry) const
{
    return m_entryHash.contains(const_cast<HistoryItem*>(&entry));
}

void AmigaBackForwardList::close()
{
    m_entries.clear();
    m_entryHash.clear();
    m_current = noCurrentItemIndex;
}

} // namespace GFOpenBrowser

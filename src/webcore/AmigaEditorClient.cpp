/*
 * GFOpenBrowser: WebCore's editor client. It lets form fields and editable
 * content take typing, and turns editing keys into WebCore's commands.
 *
 * Our code is MIT, Copyright (c) 2026 AmiCradle Team. The key tables and the
 * keyboard handling come from WebKit's WPE port, under this licence:
 *
 * Copyright (C) 2014 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"
#include "AmigaEditorClient.h"

#include <WebCore/DocumentPage.h>
#include <WebCore/DocumentView.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/Document.h>
#include <WebCore/Editor.h>
#include <WebCore/EditorClient.h>
#include <WebCore/EventNames.h>
#include <WebCore/KeyboardEvent.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/Node.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/TextCheckerClient.h>
#include <WebCore/UndoStep.h>
#include <WebCore/WindowsKeyboardCodes.h>
#include <array>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>

namespace GFOpenBrowser {

using namespace WebCore;

static const unsigned CtrlKey  = 1 << 0;
static const unsigned AltKey   = 1 << 1;
static const unsigned ShiftKey = 1 << 2;
static const unsigned MetaKey  = 1 << 3;

struct KeyDownEntry {
    unsigned virtualKey;
    unsigned modifiers;
    const char* name;
};

struct KeyPressEntry {
    unsigned charCode;
    unsigned modifiers;
    const char* name;
};

static constexpr auto keyDownEntries = WTF::toArray<KeyDownEntry>({
    { VK_LEFT,   0,                  "MoveLeft"                                },
    { VK_LEFT,   ShiftKey,           "MoveLeftAndModifySelection"              },
    { VK_LEFT,   CtrlKey,            "MoveWordLeft"                            },
    { VK_LEFT,   CtrlKey | ShiftKey, "MoveWordLeftAndModifySelection"          },
    { VK_RIGHT,  0,                  "MoveRight"                               },
    { VK_RIGHT,  ShiftKey,           "MoveRightAndModifySelection"             },
    { VK_RIGHT,  CtrlKey,            "MoveWordRight"                           },
    { VK_RIGHT,  CtrlKey | ShiftKey, "MoveWordRightAndModifySelection"         },
    { VK_UP,     0,                  "MoveUp"                                  },
    { VK_UP,     ShiftKey,           "MoveUpAndModifySelection"                },
    { VK_PRIOR,  ShiftKey,           "MovePageUpAndModifySelection"            },
    { VK_DOWN,   0,                  "MoveDown"                                },
    { VK_DOWN,   ShiftKey,           "MoveDownAndModifySelection"              },
    { VK_NEXT,   ShiftKey,           "MovePageDownAndModifySelection"          },
    { VK_PRIOR,  0,                  "MovePageUp"                              },
    { VK_NEXT,   0,                  "MovePageDown"                            },
    { VK_HOME,   0,                  "MoveToBeginningOfLine"                   },
    { VK_HOME,   ShiftKey,           "MoveToBeginningOfLineAndModifySelection" },
    { VK_HOME,   CtrlKey,            "MoveToBeginningOfDocument"               },
    { VK_END,    0,                  "MoveToEndOfLine"                         },
    { VK_END,    ShiftKey,           "MoveToEndOfLineAndModifySelection"       },
    { VK_END,    CtrlKey,            "MoveToEndOfDocument"                     },
    { VK_BACK,   0,                  "DeleteBackward"                          },
    { VK_BACK,   ShiftKey,           "DeleteBackward"                          },
    { VK_DELETE, 0,                  "DeleteForward"                           },
    { VK_BACK,   CtrlKey,            "DeleteWordBackward"                      },
    { VK_DELETE, CtrlKey,            "DeleteWordForward"                       },
    { VK_TAB,    0,                  "InsertTab"                               },
    { VK_TAB,    ShiftKey,           "InsertBacktab"                           },
    { VK_RETURN, 0,                  "InsertNewline"                           },
    { VK_RETURN, ShiftKey,           "InsertLineBreak"                         },
    // The Amiga's shortcuts use the right Amiga key, which arrives as Meta.
    { 'C',       MetaKey,            "Copy"                                    },
    { 'V',       MetaKey,            "Paste"                                   },
    { 'X',       MetaKey,            "Cut"                                     },
    { 'A',       MetaKey,            "SelectAll"                               },
    { 'Z',       MetaKey,            "Undo"                                    },
    { 'Z',       MetaKey | ShiftKey, "Redo"                                    },
    { 'C',       CtrlKey,            "Copy"                                    },
    { 'V',       CtrlKey,            "Paste"                                   },
    { 'X',       CtrlKey,            "Cut"                                     },
    { 'A',       CtrlKey,            "SelectAll"                               },
    { 'Z',       CtrlKey,            "Undo"                                    },
    { 'Y',       CtrlKey,            "Redo"                                    },
});

static constexpr auto keyPressEntries = WTF::toArray<KeyPressEntry>({
    { '\t',   0,                  "InsertTab"       },
    { '\t',   ShiftKey,           "InsertBacktab"   },
    { '\r',   0,                  "InsertNewline"   },
    { '\r',   ShiftKey,           "InsertLineBreak" },
});

static const char* interpretKeyEvent(const KeyboardEvent& event)
{
    static NeverDestroyed<HashMap<int, const char*>> keyDownCommandsMap;
    static NeverDestroyed<HashMap<int, const char*>> keyPressCommandsMap;

    if (keyDownCommandsMap.get().isEmpty()) {
        for (const auto& entry : keyDownEntries)
            keyDownCommandsMap.get().set(entry.modifiers << 16 | entry.virtualKey, entry.name);
        for (const auto& entry : keyPressEntries)
            keyPressCommandsMap.get().set(entry.modifiers << 16 | entry.charCode, entry.name);
    }

    unsigned modifiers = 0;
    if (event.shiftKey())
        modifiers |= ShiftKey;
    if (event.altKey())
        modifiers |= AltKey;
    if (event.ctrlKey())
        modifiers |= CtrlKey;
    if (event.metaKey())
        modifiers |= MetaKey;

    if (event.type() == eventNames().keydownEvent) {
        int mapKey = modifiers << 16 | event.keyCode();
        return mapKey ? keyDownCommandsMap.get().get(mapKey) : nullptr;
    }

    int mapKey = modifiers << 16 | event.charCode();
    return mapKey ? keyPressCommandsMap.get().get(mapKey) : nullptr;
}

static void handleKeyPress(LocalFrame& frame, KeyboardEvent& event, const PlatformKeyboardEvent& platformEvent)
{
    if (!frame.editor().canEdit())
        return;

    auto commandName = String::fromLatin1(interpretKeyEvent(event));
    if (!commandName.isEmpty()) {
        frame.editor().command(commandName).execute();
        event.setDefaultHandled();
        return;
    }

    // Control characters and keys held with a modifier insert nothing.
    if (event.charCode() < ' ')
        return;
    if (platformEvent.controlKey() || platformEvent.metaKey())
        return;

    if (frame.editor().insertText(platformEvent.text(), &event))
        event.setDefaultHandled();
}

static void handleKeyDown(LocalFrame& frame, KeyboardEvent& event, const PlatformKeyboardEvent&)
{
    auto commandName = String::fromLatin1(interpretKeyEvent(event));
    if (commandName.isEmpty())
        return;

    // Text goes in with the keypress; Tab may move the focus instead.
    Editor::Command command = frame.editor().command(commandName);
    if (command.isTextInsertion())
        return;

    if (command.execute())
        event.setDefaultHandled();
}

class AmigaTextCheckerClient final : public TextCheckerClient {
    bool shouldEraseMarkersAfterChangeSelection(TextCheckingType) const final { return true; }
    void ignoreWordInSpellDocument(const String&) final { }
    void learnWord(const String&) final { }
    void checkSpellingOfString(StringView, int*, int*) final { }
    void checkGrammarOfString(StringView, Vector<GrammarDetail>&, int*, int*) final { }
#if USE(UNIFIED_TEXT_CHECKING)
    Vector<TextCheckingResult> checkTextOfParagraph(StringView, OptionSet<TextCheckingType>, const VisibleSelection&) final { return { }; }
#endif
    void getGuessesForWord(const String&, const String&, const VisibleSelection&, Vector<String>&) final { }
    void requestCheckingOfString(TextCheckingRequest&, const VisibleSelection&) final { }
    void requestExtendedCheckingOfString(TextCheckingRequest&, const VisibleSelection&) final { }
};

class AmigaEditorClient final : public EditorClient {
    WTF_MAKE_TZONE_ALLOCATED(AmigaEditorClient);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(AmigaEditorClient);
private:
    bool shouldDeleteRange(const std::optional<SimpleRange>&) final { return true; }
    bool smartInsertDeleteEnabled() final { return false; }
    bool isSelectTrailingWhitespaceEnabled() const final { return false; }
    bool isContinuousSpellCheckingEnabled() final { return false; }
    void toggleContinuousSpellChecking() final { }
    bool isGrammarCheckingEnabled() final { return false; }
    void toggleGrammarChecking() final { }
    int spellCheckerDocumentTag() final { return -1; }

    bool shouldBeginEditing(const SimpleRange&) final { return true; }
    bool shouldEndEditing(const SimpleRange&) final { return true; }
    bool shouldInsertNode(Node&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldInsertText(const String&, const std::optional<SimpleRange>&, EditorInsertAction) final { return true; }
    bool shouldChangeSelectedRange(const std::optional<SimpleRange>&, const std::optional<SimpleRange>&, Affinity, bool) final { return true; }

    bool shouldApplyStyle(const StyleProperties&, const std::optional<SimpleRange>&) final { return true; }
    void didApplyStyle() final { }
    bool shouldMoveRangeAfterDelete(const SimpleRange&, const SimpleRange&) final { return true; }

    void didBeginEditing() final { }
    void respondToChangedContents() final { }
    void respondToChangedSelection(LocalFrame*) final { }
    void updateEditorStateAfterLayoutIfEditabilityChanged() final { }
    void discardedComposition(const Document&) final { }
    void canceledComposition() final { }
    void didUpdateComposition() final { }
    void didEndEditing() final { }
    void didEndUserTriggeredSelectionChanges() final { }
    void willWriteSelectionToPasteboard(const std::optional<SimpleRange>&) final { }
    void didWriteSelectionToPasteboard() final { }
    void getClientPasteboardData(const std::optional<SimpleRange>&, Vector<std::pair<String, RefPtr<SharedBuffer>>>&) final { }
    void requestCandidatesForSelection(const VisibleSelection&) final { }
    void handleAcceptedCandidateWithSoftSpaces(const TextCheckingResult&) final { }

    // Undo is not kept yet; the steps are dropped.
    void registerUndoStep(UndoStep&) final { }
    void registerRedoStep(UndoStep&) final { }
    void clearUndoRedoOperations() final { }

    DOMPasteAccessResponse requestDOMPasteAccess(DOMPasteAccessCategory, FrameIdentifier, const String&) final { return DOMPasteAccessResponse::GrantedForGesture; }

    bool canCopyCut(LocalFrame*, bool defaultValue) const final { return defaultValue; }
    bool canPaste(LocalFrame*, bool defaultValue) const final { return defaultValue; }
    bool canUndo() const final { return false; }
    bool canRedo() const final { return false; }

    void undo() final { }
    void redo() final { }

    void handleKeyboardEvent(KeyboardEvent& event) final
    {
        RefPtr node = dynamicDowncast<Node>(event.target());
        if (!node)
            return;
        RefPtr frame = node->document().frame();
        if (!frame)
            return;
        auto* platformEvent = event.underlyingPlatformEvent();
        if (!platformEvent)
            return;
        if (event.type() == eventNames().keypressEvent)
            handleKeyPress(*frame, event, *platformEvent);
        else if (event.type() == eventNames().keydownEvent)
            handleKeyDown(*frame, event, *platformEvent);
    }
    void handleInputMethodKeydown(KeyboardEvent&) final { }

    void textFieldDidBeginEditing(Element&) final { }
    void textFieldDidEndEditing(Element&) final { }
    void textDidChangeInTextField(Element&) final { }
    bool doTextFieldCommandFromEvent(Element&, KeyboardEvent*) final { return false; }
    void textWillBeDeletedInTextField(Element&) final { }
    void textDidChangeInTextArea(Element&) final { }
    void overflowScrollPositionChanged() final { }
    void subFrameScrollPositionChanged() final { }

    bool performTwoStepDrop(DocumentFragment&, const SimpleRange&, bool) final { return false; }

    TextCheckerClient* textChecker() final { return &m_textCheckerClient; }

    void updateSpellingUIWithGrammarString(const String&, const GrammarDetail&) final { }
    void updateSpellingUIWithMisspelledWord(const String&) final { }
    void showSpellingUI(bool) final { }
    bool spellingUIIsShowing() final { return false; }

    void setInputMethodState(Element*) final { }

    AmigaTextCheckerClient m_textCheckerClient;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(AmigaEditorClient);

UniqueRef<EditorClient> createEditorClient()
{
    return makeUniqueRef<AmigaEditorClient>();
}

} // namespace GFOpenBrowser

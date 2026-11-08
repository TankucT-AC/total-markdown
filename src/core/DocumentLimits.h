// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_DOCUMENTLIMITS_H
#define TOTALMARKDOWN_CORE_DOCUMENTLIMITS_H

#include <QtGlobal>

/*
 * How much work the application is willing to do for a document of a given
 * size. Rendering markdown is CPU-bound and runs on the GUI thread, so a
 * multi-megabyte file would otherwise freeze the window on every keystroke.
 *
 * The policy is a pure function of the byte size: one place to tune, easy to
 * unit-test, and easy to explain to the user when the editor degrades.
 */
class DocumentLimits
{
public:
    enum class Tier {
        Interactive, // live preview, highlighting, nothing held back
        Deferred, // the preview arrives once typing pauses
        Manual, // the preview is rendered on request only
    };

    // The policy for a document of this size in bytes (UTF-8).
    static DocumentLimits forSize(qint64 sizeBytes);

    qint64 sizeBytes() const
    {
        return m_sizeBytes;
    }
    Tier tier() const
    {
        return m_tier;
    }

    // True whenever the application holds something back, so the window can
    // say so instead of leaving the user wondering why the preview is stale.
    bool degraded() const
    {
        return m_tier != Tier::Interactive;
    }

    bool previewEnabled() const
    {
        return m_previewEnabled;
    }
    bool editorHighlightEnabled() const
    {
        return m_editorHighlightEnabled;
    }

    // Debounce the preview should use, in milliseconds.
    int renderIntervalMs() const
    {
        return m_renderIntervalMs;
    }

    // Budgets for fenced-code syntax highlighting, in QString characters.
    // A single pathological block is what makes highlighting quadratic, so the
    // per-block cap matters more than the total.
    int codeHighlightMaxBlockChars() const
    {
        return m_codeHighlightMaxBlockChars;
    }
    int codeHighlightMaxTotalChars() const
    {
        return m_codeHighlightMaxTotalChars;
    }

    // Above this, live preview gives way to "render when the typing pauses".
    static constexpr qint64 livePreviewLimitBytes = 1024 * 1024;
    // Above this, the preview stays off until the user asks for it.
    static constexpr qint64 previewLimitBytes = 5 * 1024 * 1024;
    static constexpr int defaultRenderIntervalMs = 40;
    static constexpr int deferredRenderIntervalMs = 900;

private:
    // The defaults describe the Interactive tier, so a value-constructed
    // instance means "no restrictions".
    qint64 m_sizeBytes = 0;
    Tier m_tier = Tier::Interactive;
    bool m_previewEnabled = true;
    bool m_editorHighlightEnabled = true;
    int m_renderIntervalMs = defaultRenderIntervalMs;
    int m_codeHighlightMaxBlockChars = 128 * 1024;
    int m_codeHighlightMaxTotalChars = 1024 * 1024;
};

#endif // TOTALMARKDOWN_CORE_DOCUMENTLIMITS_H

// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "core/MarkdownBuilder.h"
#include "core/Theme.h"
#include "viewer/CodeHighlighter.h"

#include <QSet>
#include <QTextBlock>
#include <QTextDocument>

class CodeHighlightTest : public QObject
{
    Q_OBJECT

private slots:
    void codeBlockIsSplitIntoStyledFragments();
    void switchingThemeChangesCodeColors();
    void unknownLanguageIsLeftAlone();
    void plainTextIsNotTouched();
    void oversizedBlockIsSkipped();
    void zeroTotalBudgetSkipsEverything();
    void generousLimitsMatchUnlimited();

private:
    static QTextDocument *build(const QString &markdown);
    static QSet<QRgb> paletteOf(QTextDocument *document);
};

QTextDocument *CodeHighlightTest::build(const QString &markdown)
{
    RenderStyle style;
    style.theme = ThemeDefinition::light();
    auto *document = new QTextDocument;
    MarkdownBuilder::build(document, markdown, style);
    return document;
}

QSet<QRgb> CodeHighlightTest::paletteOf(QTextDocument *document)
{
    QSet<QRgb> colors;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it)
            colors.insert(it.fragment().charFormat().foreground().color().rgb());
    }
    return colors;
}

void CodeHighlightTest::codeBlockIsSplitIntoStyledFragments()
{
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("```cpp\n// comment\nint x = 0;\n```\n")));
    const QSet<QRgb> before = paletteOf(document.get());

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.highlight(document.get());

    const QSet<QRgb> after = paletteOf(document.get());
    QVERIFY(after.size() > before.size()); // keywords/comments got their own colors
}

void CodeHighlightTest::switchingThemeChangesCodeColors()
{
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("```python\n# comment\nx = 1\n```\n")));

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.highlight(document.get());
    const QSet<QRgb> light = paletteOf(document.get());
    QVERIFY(light.size() > 1);

    highlighter.setThemeDefinition(ThemeDefinition::dark());
    highlighter.highlight(document.get());
    const QSet<QRgb> dark = paletteOf(document.get());
    QVERIFY(dark.size() > 1);

    QVERIFY(light != dark);
}

void CodeHighlightTest::unknownLanguageIsLeftAlone()
{
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("```definitelynotalanguage\nfoo bar\n```\n")));
    const QSet<QRgb> before = paletteOf(document.get());

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.highlight(document.get());

    QCOMPARE(paletteOf(document.get()), before);
}

void CodeHighlightTest::plainTextIsNotTouched()
{
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("# Heading\n\nsome prose\n")));
    const QSet<QRgb> before = paletteOf(document.get());

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.highlight(document.get());

    QCOMPARE(paletteOf(document.get()), before);
}

void CodeHighlightTest::oversizedBlockIsSkipped()
{
    // A minified file is one enormous line; the per-block cap is what keeps it
    // from stalling the pass.
    QString single;
    while (single.size() < 4096)
        single += QStringLiteral("int value = 1; ");
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("```cpp\n") + single + QStringLiteral("\n```\n")));
    const QSet<QRgb> before = paletteOf(document.get());

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.setLimits(1024, -1); // far below the length of that line
    highlighter.highlight(document.get());
    QCOMPARE(paletteOf(document.get()), before);

    // Proof that the cap, and not the language, is what left it alone.
    CodeHighlighter loose;
    loose.setThemeDefinition(ThemeDefinition::light());
    loose.highlight(document.get());
    QVERIFY(paletteOf(document.get()).size() > before.size());
}

void CodeHighlightTest::zeroTotalBudgetSkipsEverything()
{
    std::unique_ptr<QTextDocument> document(build(QStringLiteral("```cpp\nint a = 1;\n```\n")));
    const QSet<QRgb> before = paletteOf(document.get());

    CodeHighlighter highlighter;
    highlighter.setThemeDefinition(ThemeDefinition::light());
    highlighter.setLimits(-1, 0);
    highlighter.highlight(document.get());

    QCOMPARE(paletteOf(document.get()), before);
}

void CodeHighlightTest::generousLimitsMatchUnlimited()
{
    const QString markdown = QStringLiteral("```python\n# note\nx = 1\n```\n");

    std::unique_ptr<QTextDocument> unlimited(build(markdown));
    CodeHighlighter loose;
    loose.setThemeDefinition(ThemeDefinition::light());
    loose.highlight(unlimited.get());

    std::unique_ptr<QTextDocument> capped(build(markdown));
    CodeHighlighter bounded;
    bounded.setThemeDefinition(ThemeDefinition::light());
    bounded.setLimits(4096, 64 * 1024);
    bounded.highlight(capped.get());

    QCOMPARE(paletteOf(capped.get()), paletteOf(unlimited.get()));
}

QTEST_MAIN(CodeHighlightTest)
#include "test_code.moc"
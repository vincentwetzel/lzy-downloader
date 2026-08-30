#pragma once

#include <QStringList>
#include <QRegularExpression>

/**
 * Retains the newest diagnostic lines within both line and character limits.
 *
 * Worker diagnostics are useful for recovery and error messages, but external
 * tools can produce unbounded output. The helper deliberately keeps the tail
 * so the final failure is not displaced by progress or warning noise.
 */
class DiagnosticTail
{
public:
    explicit DiagnosticTail(qsizetype maxLines = 100, qsizetype maxCharacters = 64 * 1024)
        : m_maxLines(qMax<qsizetype>(0, maxLines))
        , m_maxCharacters(qMax<qsizetype>(0, maxCharacters))
    {
    }

    void append(const QString &line)
    {
        if (line.isEmpty() || m_maxLines == 0 || m_maxCharacters == 0) {
            return;
        }

        m_lines.append(line.size() > m_maxCharacters ? line.right(m_maxCharacters) : line);
        trim();
    }

    void appendText(const QString &text)
    {
        const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            append(line);
        }
    }

    void clear() { m_lines.clear(); }
    [[nodiscard]] bool isEmpty() const { return m_lines.isEmpty(); }
    [[nodiscard]] qsizetype size() const { return m_lines.size(); }
    [[nodiscard]] QStringList lines() const { return m_lines; }
    [[nodiscard]] QStringList mid(qsizetype position, qsizetype length = -1) const { return m_lines.mid(position, length); }
    [[nodiscard]] QString join(const QString &separator) const { return m_lines.join(separator); }
    [[nodiscard]] QStringList::const_iterator begin() const { return m_lines.cbegin(); }
    [[nodiscard]] QStringList::const_iterator end() const { return m_lines.cend(); }

private:
    void trim()
    {
        while (m_lines.size() > m_maxLines) {
            m_lines.removeFirst();
        }

        qsizetype characters = 0;
        for (const QString &line : m_lines) {
            characters += line.size();
        }
        while (characters > m_maxCharacters && !m_lines.isEmpty()) {
            characters -= m_lines.constFirst().size();
            m_lines.removeFirst();
        }
    }

    qsizetype m_maxLines;
    qsizetype m_maxCharacters;
    QStringList m_lines;
};

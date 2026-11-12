#include "csvutils.h"

#include <QFile>

namespace CsvUtils {

int Table::column(const QString &key) const
{
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].trimmed().compare(key, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

QString Table::value(int row, const QString &key) const
{
    const int col = column(key);
    if (row < 0 || row >= rows.size() || col < 0 || col >= rows[row].size())
        return QString();
    return rows[row][col];
}

bool readFile(const QString &path, Table *table, QString *error)
{
    if (!table) {
        if (error) *error = QStringLiteral("CSV 表格指针无效");
        return false;
    }
    table->headers.clear();
    table->rows.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取 CSV：") + path;
        return false;
    }
    QString src = QString::fromUtf8(file.readAll());
    if (!src.isEmpty() && src.front() == QChar(0xFEFF))
        src.remove(0, 1);
    QVector<QStringList> parsed;
    QString field;
    QStringList row;
    bool quoted = false;
    for (int i = 0; i < src.size(); ++i) {
        const QChar c = src[i];
        if (c == QLatin1Char('"')) {
            if (quoted && i + 1 < src.size() && src[i + 1] == QLatin1Char('"')) {
                field += QLatin1Char('"');
                ++i;
            } else if (!quoted && !field.isEmpty()) {
                field += c;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted && (c == QLatin1Char(',') || c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            row.append(field);
            field.clear();
            if (c != QLatin1Char(',')) {
                if (!(row.size() == 1 && row[0].trimmed().isEmpty()))
                    parsed.append(row);
                row.clear();
                if (c == QLatin1Char('\r') && i + 1 < src.size() && src[i+1] == QLatin1Char('\n'))
                    ++i;
            }
        } else {
            field += c;
        }
    }
    if (quoted) {
        if (error) *error = QStringLiteral("CSV 引号未闭合：") + path;
        return false;
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row.append(field);
        parsed.append(row);
    }
    if (parsed.isEmpty()) {
        if (error) *error = QStringLiteral("CSV 为空：") + path;
        return false;
    }
    table->headers = parsed[0];
    for (int i = 1; i < parsed.size(); ++i) {
        if (parsed[i].size() > table->headers.size()) {
            if (error) *error = QStringLiteral("CSV 列数不匹配：") + path;
            return false;
        }
        QStringList values = parsed[i];
        while (values.size() < table->headers.size()) values.append(QString());
        table->rows.append(values);
    }
    return true;
}

QString escapeField(const QString &input)
{
    QString result = input;
    result.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + result + QStringLiteral("\"");
}

} // namespace CsvUtils

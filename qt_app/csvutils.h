#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace CsvUtils {

struct Table {
    QStringList headers;
    QVector<QStringList> rows;
    int column(const QString &key) const;
    QString value(int row, const QString &key) const;
};

bool readFile(const QString &path, Table *table, QString *error = nullptr);
QString escapeField(const QString &input);

} // namespace CsvUtils

#include "workspace.h"
#include "resource_metrics.h"
#include <cmath>
#include <limits>

namespace podlord {
ResourceFilter::ResourceFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortRole(Qt::UserRole + 6);
}
Result<QList<ResourceFilter::Token>> ResourceFilter::compile(const QString& text) {
    if (text.toUtf8().size() > 65536)
        return Failure{StoreError::InvalidInput, "The filter exceeds the 64 KiB expression boundary."};
    QList<Token> tokens;
    qsizetype position = 0;
    while (position < text.size()) {
        if (text[position].isSpace()) { ++position; continue; }
        const auto start = position;
        Token token;
        const auto delimiter = text[position];
        if (delimiter == '"' || delimiter == '/') {
            ++position; bool closed = false;
            while (position < text.size()) {
                const auto character = text[position++];
                if (character == delimiter) { closed = true; break; }
                if (character == '\\' && position < text.size()) {
                    const auto escaped = text[position++];
                    if (delimiter == '/' && escaped != '/') token.value += '\\';
                    token.value += escaped;
                } else token.value += character;
            }
            if (!closed) return Failure{StoreError::InvalidInput, "Close the quoted value or /regular expression/ before searching."};
            token.kind = delimiter == '"' ? Token::Exact : Token::Regex;
            if (token.kind == Token::Regex) {
                token.pattern = QRegularExpression("(*LIMIT_MATCH=100000)(*LIMIT_DEPTH=1000)" + token.value,
                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
                if (!token.pattern.isValid())
                    return Failure{StoreError::InvalidInput, "Invalid regular expression. Correct it or reset the search."};
            }
        } else {
            while (position < text.size() && !text[position].isSpace()) ++position;
            token.value = text.mid(start, position - start);
            for (const auto& operation : QStringList{">=", "=>", "<=", "=<", ">", "<", "="}) {
                if (token.value.startsWith(operation) && token.value.size() > operation.size()) {
                    token.operation = operation; token.value.remove(0, operation.size()); break;
                }
            }
            bool integer = false;
            token.number = token.value.toInt(&integer);
            if (integer) token.kind = Token::Number;
            else if (token.operation == "=") token.kind = Token::Exact;
            else if (token.operation.isEmpty() && token.value.startsWith('~') && token.value.size() > 1) {
                token.kind = Token::Prefix; token.value.remove(0, 1);
            } else if (token.operation.isEmpty() && token.value.endsWith('~') && token.value.size() > 1) {
                token.kind = Token::Suffix; token.value.chop(1);
            }
        }
        token.raw = text.mid(start, position - start);
        tokens.append(std::move(token));
    }
    return tokens;
}
QStringList ResourceFilter::exactValues(const QString& expression) {
    QStringList values;
    const auto compiled = compile(expression);
    if (const auto* tokens = std::get_if<QList<Token>>(&compiled))
        for (const auto& token : *tokens) if (token.kind == Token::Exact) values.append(token.value);
    return values;
}
Result<QList<ResourceFilter::Token>> ResourceFilter::compileQuantity(const QString& text, bool cpu) {
    const auto parsed = compile(text);
    if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
    const auto input = std::get<QList<Token>>(parsed);
    QList<Token> result;
    const auto normalize = [cpu](QString value) {
        static const QRegularExpression whitespace("\\s+");
        value.remove(whitespace);
        static const QList<QPair<QString, QString>> cpuUnits{{"mCPU", "m"}, {"millicores", "m"}, {"millicore", "m"},
            {"cores", ""}, {"core", ""}, {"m", "m"}, {"c", ""}};
        static const QList<QPair<QString, QString>> byteUnits{{"EiB", "Ei"}, {"PiB", "Pi"}, {"TiB", "Ti"}, {"GiB", "Gi"}, {"MiB", "Mi"}, {"KiB", "Ki"},
            {"Ei", "Ei"}, {"Pi", "Pi"}, {"Ti", "Ti"}, {"Gi", "Gi"}, {"Mi", "Mi"}, {"Ki", "Ki"},
            {"EB", "E"}, {"PB", "P"}, {"TB", "T"}, {"GB", "G"}, {"MB", "M"}, {"KB", "k"},
            {"E", "E"}, {"P", "P"}, {"T", "T"}, {"G", "G"}, {"M", "M"}, {"K", "k"}, {"B", ""}};
        for (const auto& unit : cpu ? cpuUnits : byteUnits) {
            if (!value.endsWith(unit.first, Qt::CaseInsensitive)) continue;
            value.chop(unit.first.size()); value += unit.second; break;
        }
        return value;
    };
    const QStringList operators{">", "<", ">=", "<=", "=>", "=<", "="};
    for (qsizetype index = 0; index < input.size(); ++index) {
        auto token = input[index];
        if (operators.contains(token.raw)) {
            if (++index >= input.size() || !input[index].operation.isEmpty())
                return Failure{StoreError::InvalidInput, "A quantity comparison needs one value, for example >=500m or <128Mi."};
            const auto operation = token.raw; token = input[index]; token.operation = operation;
        }
        // Quoted values retain exact display matching, including stale/incomplete annotations.
        if (token.raw.startsWith('"') && token.operation.isEmpty()) continue;
        if (token.kind == Token::Regex || token.kind == Token::Prefix || token.kind == Token::Suffix) {
            if (!token.operation.isEmpty()) return Failure{StoreError::InvalidInput, "A numeric comparison needs a quantity, not a text pattern."};
            continue;
        }
        if (index + 1 < input.size() && input[index + 1].kind == Token::Contains && input[index + 1].operation.isEmpty()
            && !input[index + 1].value.isEmpty() && input[index + 1].value[0].isLetter()
            && metricQuantity("1" + normalize(input[index + 1].value), cpu)) token.value += input[++index].value;
        const auto value = metricQuantity(normalize(token.value), cpu);
        if (!value) {
            const auto& input = token.value;
            const bool numeric = !input.isEmpty() && (input[0].isDigit() || input[0] == '.' ||
                ((input[0] == '+' || input[0] == '-') && input.size() > 1 && (input[1].isDigit() || input[1] == '.')));
            if (numeric || token.kind == Token::Number || (!token.operation.isEmpty() && token.kind != Token::Exact))
                return Failure{StoreError::InvalidInput, "Invalid quantity. Use a nonnegative finite value, such as 500m CPU or 64Mi memory/storage."};
            continue;
        }
        token.kind = Token::Quantity; token.number = *value;
        result.append(std::move(token));
    }
    // The reference applies numeric groups when present, otherwise the shared display-text grammar.
    return result.isEmpty() ? input : result;
}
namespace {
std::optional<double> durationSeconds(const QString& value) {
    if (value.compare("now", Qt::CaseInsensitive) == 0) return 0;
    static const QRegularExpression part("([0-9]+)(ms|secs?|mins?|hrs?|days?|weeks?|s|m|h|d|w)?", QRegularExpression::CaseInsensitiveOption);
    static const QMap<QString, qint64> units{{"", 1000}, {"ms", 1}, {"s", 1000}, {"sec", 1000}, {"secs", 1000},
        {"m", 60000}, {"min", 60000}, {"mins", 60000}, {"h", 3600000}, {"hr", 3600000}, {"hrs", 3600000},
        {"d", 86400000}, {"day", 86400000}, {"days", 86400000}, {"w", 604800000}, {"week", 604800000}, {"weeks", 604800000}};
    qsizetype position = 0; qint64 total = 0;
    while (position < value.size()) {
        const auto match = part.match(value, position, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) return {};
        bool valid = false; const auto number = match.captured(1).toLongLong(&valid);
        const auto factor = units.value(match.captured(2).toLower());
        if (!valid || number > (std::numeric_limits<qint64>::max() - total) / factor) return {};
        total += number * factor; position = match.capturedEnd();
    }
    return value.isEmpty() ? std::nullopt : std::optional<double>(double(total) / 1000);
}
}
Result<QList<ResourceFilter::Token>> ResourceFilter::compileDuration(const QString& text) {
    const auto parsed = compile(text);
    if (const auto* failure = std::get_if<Failure>(&parsed)) return *failure;
    const auto input = std::get<QList<Token>>(parsed); QList<Token> result;
    const QStringList operators{">", "<", ">=", "<=", "=>", "=<", "="};
    for (qsizetype index = 0; index < input.size(); ++index) {
        auto token = input[index];
        if (operators.contains(token.raw)) {
            if (++index >= input.size() || !input[index].operation.isEmpty())
                return Failure{StoreError::InvalidInput, "An age comparison needs a duration, for example >=5m or <1h."};
            const auto operation = token.raw; token = input[index]; token.operation = operation;
        }
        if (token.kind == Token::Regex || token.kind == Token::Prefix || token.kind == Token::Suffix || token.raw.startsWith('"')) {
            if (!token.operation.isEmpty()) return Failure{StoreError::InvalidInput, "An age comparison needs a duration, not a text pattern."};
            continue;
        }
        if (index + 1 < input.size() && input[index + 1].operation.isEmpty() && !input[index + 1].raw.isEmpty()
            && input[index + 1].raw[0].isLetter() && durationSeconds("1" + input[index + 1].raw)) token.value += input[++index].raw;
        const auto duration = durationSeconds(token.value);
        if (!duration) {
            const auto& value = token.value;
            const bool numeric = !value.isEmpty() && (value[0].isDigit() || value[0] == '.' ||
                ((value[0] == '-' || value[0] == '+') && value.size() > 1 && value[1].isDigit()));
            if (numeric || (!token.operation.isEmpty() && token.kind != Token::Exact))
                return Failure{StoreError::InvalidInput, "Invalid age duration. Use nonnegative whole seconds or ms, s, m, h, d, w; compound durations such as 1h30m are supported."};
            continue;
        }
        token.kind = Token::Quantity; token.number = *duration; result.append(std::move(token));
    }
    return result.isEmpty() ? input : result;
}
Result<QString> ResourceFilter::selectValue(const QString& expression, const QString& value, bool selected) {
    const auto compiled = compile(expression);
    if (const auto* failure = std::get_if<Failure>(&compiled)) return *failure;
    QStringList result;
    bool found = false;
    for (const auto& token : std::get<QList<Token>>(compiled)) {
        if (token.kind == Token::Exact && token.value.compare(value, Qt::CaseInsensitive) == 0) {
            found = true;
            if (selected) result.append(token.raw);
        } else result.append(token.raw);
    }
    if (selected && !found) {
        auto escaped = value; escaped.replace('\\', "\\\\").replace('"', "\\\"");
        result.append("\"" + escaped + "\"");
    }
    return result.join(' ');
}
bool ResourceFilter::filter(const QString& text, const QMap<QString, QString>& fields, const QString& mode) {
    if (compiled_ && text_ == text && fields_ == fields && mode_ == mode) {
        if (fields.contains("createdAt") && error_.isEmpty()) { refreshFilter(); rowCount(); }
        return error_.isEmpty();
    }
    text_ = text; fields_ = fields; mode_ = mode; compiled_ = true; tokens_.clear(); fieldTokens_.clear(); error_.clear();
    if (!QStringList{"", "problems", "activity"}.contains(mode)) error_ = "Unsupported resource filter mode.";
    const auto global = compile(text);
    if (const auto* failure = std::get_if<Failure>(&global)) error_ = failure->message;
    else tokens_ = std::get<QList<Token>>(global);
    for (auto field = fields.cbegin(); field != fields.cend() && error_.isEmpty(); ++field) {
        int column = -1;
        if (sourceModel()) for (int index = 0; index < sourceModel()->columnCount(); ++index)
            if (sourceModel()->headerData(index, Qt::Horizontal, Qt::UserRole).toString() == field.key()) { column = index; break; }
        if (column < 0) { error_ = "Unsupported filter field. Reset the field filters."; break; }
        const bool quantity = field.key() == "cpu" || field.key() == "memory" || field.key() == "storage";
        const auto parsed = quantity ? compileQuantity(field.value(), field.key() == "cpu")
            : field.key() == "createdAt" ? compileDuration(field.value()) : compile(field.value());
        if (const auto* failure = std::get_if<Failure>(&parsed)) error_ = failure->message;
        else if (!std::get<QList<Token>>(parsed).isEmpty()) fieldTokens_.insert(column, std::get<QList<Token>>(parsed));
    }
    refreshFilter();
    // Evaluate before reporting acceptance, including the bounded regex result.
    rowCount();
    if (!error_.isEmpty()) refreshFilter();
    emit filterStateChanged();
    return error_.isEmpty();
}
bool ResourceFilter::refreshFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange(); endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    return error_.isEmpty();
}
bool ResourceFilter::matches(const QString& value, const QList<Token>& tokens) const {
    for (const auto& token : tokens) {
        switch (token.kind) {
        case Token::Quantity: break;
        case Token::Contains: if (value.contains(token.value, Qt::CaseInsensitive)) return true; break;
        case Token::Exact: if (value.compare(token.value, Qt::CaseInsensitive) == 0) return true; break;
        case Token::Prefix: if (value.startsWith(token.value, Qt::CaseInsensitive)) return true; break;
        case Token::Suffix: if (value.endsWith(token.value, Qt::CaseInsensitive)) return true; break;
        case Token::Number: {
            bool integer = false; const int number = value.toInt(&integer);
            if (!integer) break;
            const auto& operation = token.operation;
            const bool accepted = operation == ">" ? number > token.number : operation == "<" ? number < token.number
                : operation == ">=" || operation == "=>" ? number >= token.number
                : operation == "<=" || operation == "=<" ? number <= token.number : number == token.number;
            if (accepted) return true;
            break;
        }
        case Token::Regex: {
            const auto match = token.pattern.match(value);
            if (!match.isValid()) {
                error_ = "Regular expression exceeded its evaluation budget. Simplify it or reset the search.";
                if (!errorNotificationPending_) {
                    errorNotificationPending_ = true;
                    auto* owner = const_cast<ResourceFilter*>(this);
                    QMetaObject::invokeMethod(owner, [owner] {
                        owner->errorNotificationPending_ = false;
                        owner->refreshFilter(); emit owner->filterStateChanged();
                    }, Qt::QueuedConnection);
                }
                return false;
            }
            if (match.hasMatch()) return true;
            break;
        }
        }
    }
    return false;
}
bool ResourceFilter::filterAcceptsRow(int row, const QModelIndex& parent) const {
    if (!error_.isEmpty() || !sourceModel()) return false;
    const auto identity = sourceModel()->index(row, 0, parent);
    if (mode_ == "problems" && sourceModel()->data(identity, Qt::UserRole + 9).toInt() == 0) return false;
    if (mode_ == "activity" && !sourceModel()->data(identity, Qt::UserRole + 10).toBool()) return false;
    for (auto field = fieldTokens_.cbegin(); field != fieldTokens_.cend(); ++field) {
        const auto index = sourceModel()->index(row, field.key(), parent);
        const bool quantity = field.value().first().kind == Token::Quantity;
        const bool age = sourceModel()->headerData(field.key(), Qt::Horizontal, Qt::UserRole) == "createdAt";
        if (quantity ? !matchesQuantity(sourceModel()->data(index, Qt::UserRole + (age ? 11 : 6)), field.value())
            : !matches(sourceModel()->data(index).toString(), field.value())) return false;
    }
    if (tokens_.isEmpty()) return true;
    for (int column = 0; column < sourceModel()->columnCount(); ++column) {
        const bool accepted = matches(sourceModel()->data(sourceModel()->index(row, column, parent)).toString(), tokens_);
        if (!error_.isEmpty()) return false;
        if (accepted) return true;
    }
    return false;
}
bool ResourceFilter::matchesQuantity(const QVariant& value, const QList<Token>& tokens) const {
    // JSON numbers retain either integer or floating-point QVariant storage; display text is never a measurement.
    if (value.metaType().id() != QMetaType::Double && value.metaType().id() != QMetaType::LongLong) return false;
    const auto measured = value.toDouble();
    if (!std::isfinite(measured) || measured < 0) return false;
    bool hasExact = false, exactAccepted = false;
    for (const auto& token : tokens) {
        const auto& operation = token.operation;
        if (operation.isEmpty() || operation == "=") { hasExact = true; exactAccepted |= measured == token.number; continue; }
        const bool accepted = operation == ">" ? measured > token.number : operation == "<" ? measured < token.number
            : operation == ">=" || operation == "=>" ? measured >= token.number : measured <= token.number;
        if (!accepted) return false;
    }
    return !hasExact || exactAccepted;
}
bool ResourceFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto missing = [](const QVariant& value) {
        return !value.isValid() || value.isNull() || (value.metaType().id() == QMetaType::QString && value.toString().isEmpty())
            || (value.metaType().id() == QMetaType::QDateTime && !value.toDateTime().isValid());
    };
    const bool a = missing(sourceModel()->data(left, sortRole())), b = missing(sourceModel()->data(right, sortRole()));
    if (a != b) return sortOrder() == Qt::AscendingOrder ? !a : a;
    if (!a) {
        if (QSortFilterProxyModel::lessThan(left, right)) return true;
        if (QSortFilterProxyModel::lessThan(right, left)) return false;
    }
    const auto x = sourceModel()->data(left, Qt::UserRole).toString(), y = sourceModel()->data(right, Qt::UserRole).toString();
    return sortOrder() == Qt::AscendingOrder ? x < y : x > y;
}
}

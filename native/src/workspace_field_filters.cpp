#include "workspace.h"
#include "resource_metrics.h"
#include <QClipboard>
#include <QGuiApplication>

namespace podlord {
QVariantList Workspace::filterFields() const {
    QVariantList result;
    static const QStringList supported{"name", "kind", "namespace", "status", "node", "image", "cluster", "owner", "issue", "cpu", "memory", "storage", "ready", "restarts", "createdAt", "uid"};
    for (const auto& field : supported)
        for (int column = 0; column < rows_.columnCount(); ++column)
            if (rows_.headerData(column, Qt::Horizontal, Qt::UserRole).toString() == field)
                result.append(QVariantMap{{"id", field}, {"name", rows_.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString()}});
    return result;
}
QVariantMap Workspace::resourceFieldFilters() const {
    QVariantMap result;
    const auto fields = navigation_.value(active_).fields;
    for (auto field = fields.cbegin(); field != fields.cend(); ++field) result.insert(field.key(), field.value());
    return result;
}
bool Workspace::applyResourceFilters() {
    const auto& nav = navigation_[active_];
    table_.filter(nav.filter, nav.fields, nav.mode);
    dashboardDirty_ = true;
    publishDashboard();
    queueViewSave("resource");
    emit fieldFiltersChanged(); emit resourcePresentationChanged(); emit changed();
    return true;
}
bool Workspace::filterField(const QString& field, const QString& expression) {
    if (active_.isEmpty() || busy_) return false;
    bool supported = false;
    for (const auto& value : filterFields()) if (value.toMap().value("id").toString() == field) supported = true;
    if (!supported) return false;
    auto& nav = navigation_[active_];
    if (nav.fields.value(field) == expression) return true;
    if (expression.isEmpty()) nav.fields.remove(field); else nav.fields.insert(field, expression);
    return applyResourceFilters();
}
bool Workspace::resetResourceFilters() {
    if (active_.isEmpty() || busy_) return false;
    auto& nav = navigation_[active_];
    if (nav.filter.isEmpty() && nav.fields.isEmpty() && nav.mode.isEmpty()) return true;
    nav.filter.clear(); nav.fields.clear(); nav.mode.clear();
    return applyResourceFilters();
}
bool Workspace::prepareFilterPicker(const QString& field) {
    if (active_.isEmpty() || busy_) return false;
    int column = -1;
    for (const auto& value : filterFields()) if (value.toMap().value("id").toString() == field)
        for (int index = 0; index < rows_.columnCount(); ++index)
            if (rows_.headerData(index, Qt::Horizontal, Qt::UserRole).toString() == field) column = index;
    if (column < 0) return false;
    QStringList values = ResourceFilter::exactValues(navigation_.value(active_).fields.value(field));
    for (int row = 0; row < rows_.rowCount(); ++row) {
        const auto index = rows_.index(row, column);
        const bool quantity = field == "cpu" || field == "memory" || field == "storage";
        const auto measurement = quantity ? rows_.data(index, Qt::UserRole + 6) : QVariant{};
        if (quantity && !measurement.isValid()) continue;
        const auto value = quantity ? formatMetricQuantity(measurement.toDouble(), field == "cpu") : rows_.data(index).toString();
        if (!value.isEmpty() && value != "-") values.append(value);
    }
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) { return left < right; });
    filterPickerSession_ = active_; filterPickerField_ = field; filterPickerValues_ = values;
    emit filterPickerChanged();
    return true;
}
bool Workspace::filterValueSelected(const QString& value) const {
    if (filterPickerSession_ != active_ || !filterPickerValues_.contains(value)) return false;
    const auto selected = ResourceFilter::exactValues(navigation_.value(active_).fields.value(filterPickerField_));
    for (const auto& current : selected) if (current.compare(value, Qt::CaseInsensitive) == 0) return true;
    return false;
}
bool Workspace::selectFilterValue(const QString& value, bool selected) {
    if (filterPickerSession_ != active_ || !filterPickerValues_.contains(value)) return false;
    const auto expression = ResourceFilter::selectValue(navigation_.value(active_).fields.value(filterPickerField_), value, selected);
    if (std::holds_alternative<Failure>(expression)) return false;
    return filterField(filterPickerField_, std::get<QString>(expression));
}
bool Workspace::copyFilterValue(const QString& value) {
    if (filterPickerSession_ != active_ || !filterPickerValues_.contains(value) || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(value); return true;
}
}

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace podlord {
bool Workspace::setFilterMode(const QString& mode) {
    if (active_.isEmpty() || !QStringList{"", "problems", "activity"}.contains(mode)) return false;
    auto& nav = navigation_[active_];
    if (nav.mode == mode) return true;
    nav.mode = mode;
    return applyResourceFilters();
}
QStringList Workspace::filterPresets() const {
    auto names = presets_.keys(); names.removeAll("default");
    if (presets_.contains("default")) names.prepend("default");
    return names;
}
QString Workspace::selectedFilterPreset() const {
    const auto nav = navigation_.value(active_);
    for (auto preset = presets_.cbegin(); preset != presets_.cend(); ++preset)
        if (preset->filter == nav.filter && preset->fields == nav.fields && preset->mode == nav.mode) return preset.key();
    return {};
}
bool Workspace::reloadFilterPresets() {
    if (presetsBusy_) return false;
    presetsBusy_ = true; emit filterPresetsChanged();
    auto* watcher = new QFutureWatcher<Result<TableViewStates>>(this);
    connect(watcher, &QFutureWatcher<Result<TableViewStates>>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); presetsBusy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) { presetsReady_ = false; presetsError_ = failure->message; }
        else { presets_ = std::get<TableViewStates>(result); presetsReady_ = true; presetsError_.clear(); }
        emit filterPresetsChanged(); emit fieldFiltersChanged(); emit changed();
        if (viewClosePending_ && !viewSaving_ && pendingViews_.isEmpty() && !viewStateFailed()) emit windowCloseApproved();
    });
    presetsFuture_ = QtConcurrent::run([profile = profile_, schemas = tableSchemas()] { return ViewStateStore(profile, schemas).loadPresets(); });
    watcher->setFuture(presetsFuture_); return true;
}
bool Workspace::persistFilterPresets(const TableViewStates& desired) {
    if (presetsBusy_ || !presetsReady_) return false;
    presetsBusy_ = true; emit filterPresetsChanged();
    auto* watcher = new QFutureWatcher<Result<TableViewStates>>(this);
    connect(watcher, &QFutureWatcher<Result<TableViewStates>>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); presetsBusy_ = false;
        if (const auto* failure = std::get_if<Failure>(&result)) presetsError_ = failure->message;
        else { presets_ = std::get<TableViewStates>(result); presetsError_.clear(); }
        emit filterPresetsChanged(); emit fieldFiltersChanged(); emit changed();
        if (viewClosePending_ && !viewSaving_ && pendingViews_.isEmpty() && !viewStateFailed()) emit windowCloseApproved();
    });
    presetsFuture_ = QtConcurrent::run([profile = profile_, schemas = tableSchemas(), desired, expected = presets_] {
        return ViewStateStore(profile, schemas).savePresets(desired, expected);
    });
    watcher->setFuture(presetsFuture_); return true;
}
bool Workspace::loadFilterPreset(const QString& name) {
    if (active_.isEmpty() || !presetsReady_ || !presets_.contains(name)) return false;
    const auto value = presets_.value(name); auto& nav = navigation_[active_];
    nav.filter = value.filter; nav.fields = value.fields; nav.mode = value.mode;
    return applyResourceFilters();
}
bool Workspace::saveFilterPreset(const QString& requested) {
    const auto name = requested.trimmed();
    if (active_.isEmpty() || name.isEmpty() || name.size() > 128 || name.compare("default", Qt::CaseInsensitive) == 0 || !table_.error().isEmpty()) {
        presetsError_ = "Use a non-default name (1-128 characters) and a valid resource filter."; emit filterPresetsChanged(); return false;
    }
    for (const auto& existing : presets_.keys()) if (existing != name && existing.compare(name, Qt::CaseInsensitive) == 0) {
        presetsError_ = "A filter preset already uses that name."; emit filterPresetsChanged(); return false;
    }
    auto desired = presets_; const auto nav = navigation_.value(active_);
    desired[name] = {nav.filter, {}, false, nav.fields, nav.mode};
    return persistFilterPresets(desired);
}
bool Workspace::renameFilterPreset(const QString& name, const QString& replacement) {
    const auto next = replacement.trimmed();
    if (!presets_.contains(name) || name == "default" || next.isEmpty() || next.size() > 128 || next.compare("default", Qt::CaseInsensitive) == 0) return false;
    if (next == name) return true;
    for (const auto& existing : presets_.keys()) if (existing != name && existing.compare(next, Qt::CaseInsensitive) == 0) return false;
    auto desired = presets_; const auto value = desired.take(name); desired[next] = value;
    return persistFilterPresets(desired);
}
bool Workspace::deleteFilterPreset(const QString& name) {
    if (name == "default" || !presets_.contains(name)) return false;
    auto desired = presets_; desired.remove(name); return persistFilterPresets(desired);
}
}

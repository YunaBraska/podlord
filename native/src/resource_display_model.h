#pragma once
#include <QSortFilterProxyModel>

namespace podlord {
/** Display-only cap over the complete, already filtered and sorted cache. */
class ResourceDisplayModel final : public QSortFilterProxyModel {
public:
    explicit ResourceDisplayModel(QAbstractItemModel* source) {
        setSourceModel(source);
        // Eligibility depends on row position after sorting and removals.
        connect(source, &QAbstractItemModel::layoutChanged, this, [this] { refreshRows(); });
        connect(source, &QAbstractItemModel::rowsInserted, this, [this] { refreshRows(); });
        connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { refreshRows(); });
    }
    bool setLimit(int value) {
        if (value < 1 || value > 5000) return false;
        if (limit_ == value) return true;
        limit_ = value; refreshRows(); return true;
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override { return !parent.isValid() && row < limit_; }
private:
    void refreshRows() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange(); endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }
    int limit_ = 256;
};
}

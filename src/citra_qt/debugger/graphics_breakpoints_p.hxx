// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include <QAbstractListModel>

#include "video_core/debug_utils/debug_utils.h"

class BreakPointModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum {
        Role_IsEnabled = Qt::UserRole,
    };

    BreakPointModel(std::shared_ptr<Pica::DebugContext> context, QObject* parent);

    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);

public slots:
    void OnBreakPointHit(Pica::DebugContext::Event event);
    void OnResumed();

private:
    std::weak_ptr<Pica::DebugContext> context_weak;
    bool at_breakpoint;
    Pica::DebugContext::Event active_breakpoint;
};

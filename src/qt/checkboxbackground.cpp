// Copyright (c) 2018-2019 SwiftCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QStyleOption>
#include <QPainter>
#include <iostream>
#include "checkboxbackground.h"

QCheckBoxBackground::QCheckBoxBackground(QWidget *parent)
    : QWidget(parent)
{
    hLayout = new QHBoxLayout(this);
    checkBox = new QCheckBox(this);
    hLayout->addWidget(checkBox);
    connect(checkBox, SIGNAL(stateChanged(int)), this, SLOT(handleStateChanged(int)));
}

QCheckBoxBackground::~QCheckBoxBackground()
{
    disconnect(SIGNAL(stateChanged(int)));
    if (hLayout)
        delete hLayout;
    if (checkBox)
        delete checkBox;
}

void QCheckBoxBackground::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    QPainter p(this);

    opt.init(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void QCheckBoxBackground::setObjectName(const QString &name)
{
    checkBox->setObjectName(name);
}

void QCheckBoxBackground::setAttribute(Qt::WidgetAttribute attribute, bool on)
{
    checkBox->setAttribute(attribute, on);
}

void QCheckBoxBackground::setAlignment(Qt::Alignment alignment)
{
    hLayout->setAlignment(alignment);
}

void QCheckBoxBackground::setContentsMargins(int left, int top, int right, int bottom)
{
    hLayout->setContentsMargins(left, top, right, bottom);
}

void QCheckBoxBackground::stateChanged(int state)
{
    checkBox->stateChanged(state);
}

void QCheckBoxBackground::handleStateChanged(int state)
{
    emit stateChanged(state);
}

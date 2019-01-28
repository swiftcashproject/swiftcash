// Copyright (c) 2018-2019 SwiftCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QWidget>
#include <QHBoxLayout>
#include <QCheckBox>

class QCheckBoxBackground : public QWidget
{
    Q_OBJECT;
public:
    QCheckBoxBackground(QWidget *parent = 0);
    ~QCheckBoxBackground();
    void paintEvent(QPaintEvent *event);
    void setObjectName(const QString &name);
    void setAttribute(Qt::WidgetAttribute attribute, bool on = true);
    void setAlignment(Qt::Alignment alignment);
    void setContentsMargins(int left, int top, int right, int bottom);

signals:
    void stateChanged(int state);

public slots:
    void handleStateChanged(int state);

private:
    QHBoxLayout *hLayout;
    QCheckBox *checkBox;
};

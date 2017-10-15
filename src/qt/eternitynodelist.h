#ifndef ETERNITYNODELIST_H
#define ETERNITYNODELIST_H

#include "eternitynode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_ETERNITYNODELIST_UPDATE_SECONDS                 60
#define ETERNITYNODELIST_UPDATE_SECONDS                    15
#define ETERNITYNODELIST_FILTER_COOLDOWN_SECONDS            3

namespace Ui {
    class EternitynodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Eternitynode Manager page widget */
class EternitynodeList : public QWidget
{
    Q_OBJECT

public:
    explicit EternitynodeList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~EternitynodeList();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu *contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyEternitynodeInfo(QString strAlias, QString strAddr, eternitynode_info_t& infoMn);
    void updateMyNodeList(bool fForce = false);
    void updateNodeList();

Q_SIGNALS:

private:
    QTimer *timer;
    Ui::EternitynodeList *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;

    // Protects tableWidgetEternitynodes
    CCriticalSection cs_mnlist;

    // Protects tableWidgetMyEternitynodes
    CCriticalSection cs_mymnlist;

    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint &);
    void on_filterLineEdit_textChanged(const QString &strFilterIn);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyEternitynodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // ETERNITYNODELIST_H

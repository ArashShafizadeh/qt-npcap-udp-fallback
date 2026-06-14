#pragma once

#include "reassembly/framereassembler.h"

#include <QMainWindow>
#include <QUdpSocket>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void toggleReceiver();
    void drainDatagrams();
    void sendOrderedScenario();
    void sendOutOfOrderScenario();
    void sendDuplicateScenario();
    void sendInterleavedScenario();
    void sendMissingBatchScenario();
    void sendTimeoutScenario();
    void updateStatistics();
    void appendLog(const QString &message);

private:
    QByteArray makePayload(const QString &label, int bytes) const;
    void sendDatagrams(const QList<QByteArray> &datagrams, int intervalMs = 5);
    quint8 nextStreamId();
    void buildUi();

    QUdpSocket m_receiver;
    QUdpSocket m_sender;
    qfr::FrameReassembler m_reassembler;
    bool m_listening = false;
    quint8 m_streamCounter = 0;

    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

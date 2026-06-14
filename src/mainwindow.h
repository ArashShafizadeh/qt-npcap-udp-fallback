#pragma once

#include "hybridudpreceiver.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void populateAddresses();
    void toggleReceiver();
    void sendLocalTestDatagram();
    void appendDatagram(const ParsedUdpDatagram &datagram,
                        HybridUdpReceiver::Source source,
                        const QString &sourceDetail);
    void updateStatistics(const HybridUdpReceiver::Statistics &statistics);
    void appendLog(const QString &message);
    void setControlsRunning(bool running);

    HybridUdpReceiver receiver;

    QComboBox *addressCombo = nullptr;
    QSpinBox *firstPortSpin = nullptr;
    QSpinBox *lastPortSpin = nullptr;
    QCheckBox *npcapCheck = nullptr;
    QPushButton *startStopButton = nullptr;
    QPushButton *testButton = nullptr;
    QLineEdit *testPayloadEdit = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *qtUdpCountLabel = nullptr;
    QLabel *npcapCountLabel = nullptr;
    QLabel *deliveredCountLabel = nullptr;
    QLabel *duplicateCountLabel = nullptr;
    QLabel *byteCountLabel = nullptr;
    QPlainTextEdit *logEdit = nullptr;
};

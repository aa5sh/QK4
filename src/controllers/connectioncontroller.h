#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QThread>
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

class NetworkMetrics;
class RadioState;

class ConnectionController : public QObject {
    Q_OBJECT

public:
    explicit ConnectionController(RadioState *radioState, QObject *parent = nullptr);
    ~ConnectionController();

    // Connection lifecycle
    void connectToRadio(const RadioEntry &radio);
    void disconnectFromRadio();

    // Send CAT command to K4 (thread-safe — uses QueuedConnection internally)
    void sendCAT(const QString &command);

    // State queries
    bool isConnected() const;
    TcpClient::ConnectionState connectionState() const;
    const RadioEntry &currentRadio() const { return m_currentRadio; }
    void setDisplayFps(int fps) { m_currentRadio.displayFps = fps; }

    // Access to owned objects (for wiring by MainWindow)
    TcpClient *tcpClient() const { return m_tcpClient; }
    NetworkMetrics *networkMetrics() const { return m_networkMetrics; }

signals:
    void radioReady();                                             // Auth succeeded, K4 is live
    void connectionLost();                                         // Disconnected (clean or error)
    void connectionError(const QString &error);                    // Connection error
    void authFailed();                                             // Authentication failed
    void connectionStateChanged(TcpClient::ConnectionState state); // Any state transition

private slots:
    void onStateChanged(TcpClient::ConnectionState state);

private:
    TcpClient *m_tcpClient;
    QThread *m_ioThread = nullptr;
    NetworkMetrics *m_networkMetrics;
    RadioState *m_radioState; // not owned
    RadioEntry m_currentRadio;
};

#endif // CONNECTIONCONTROLLER_H

#pragma once

namespace TlsBackend {

// Makes Qt's OpenSSL backend the active one. Must run before the first QSslSocket is constructed.
void selectOpenSsl();

// `QK4 --tls-self-check`: reports which TLS backend and OpenSSL library are in use and whether a
// TLS 1.2+ PSK cipher suite is available. Opens no window and no connection. Returns 0 only when
// OpenSSL is active and PSK is available, 1 otherwise.
int runSelfCheck();

} // namespace TlsBackend

// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_CONNECTION_H_
#define MUMBLE_CONNECTION_H_

#include "HostAddress.h"
#include "Logger.h"
#include "MumbleProtocol.h"
#include "Timer.h"

#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#	include "win.h"
#endif

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtNetwork/QSslSocket>

#ifdef Q_OS_WIN
#	include <ws2tcpip.h>
#endif

namespace google {
namespace protobuf {
	class Message;
}
} // namespace google

class Connection : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(Connection)

	void applySocketOptions();

protected:
	QSslSocket *qtsSocket;
	Mumble::Protocol::TCPMessageType m_type;
	int iPacketLength;
	unsigned int m_droppedVoicePackets = 0;
	Timer m_lastVoiceDrop;
#ifdef Q_OS_WIN
	static HANDLE hQoS;
	DWORD dwFlow;
#endif
protected slots:
	void socketRead();
	void socketError(QAbstractSocket::SocketError);
	void socketDisconnected();
	void socketSslErrors(const QList< QSslError > &errors);
public slots:
	void proceedAnyway();
signals:
	void encrypted();
	void connectionClosed(QAbstractSocket::SocketError, const QString &reason);
	void message(Mumble::Protocol::TCPMessageType type, const QByteArray &);
	void handleSslErrors(const QList< QSslError > &);

public:
	/// Tunnelled voice is dropped while more than this is queued, so a congested link doesn't build up stale audio.
	static constexpr qint64 VOICE_DROP_BACKLOG_BYTES = 4096;
	/// A drop episode ends once voice has been sent with no drop for this long. While the tunnel is slower than the
	/// voice stream the backlog hovers around the limit, so single sends between drops do not end it.
	static constexpr std::chrono::seconds VOICE_DROP_QUIET_PERIOD{ 1 };
	/// Caps the unsent data the kernel buffers, so that a backlog stays in Qt where sendVoiceMessage() sees it.
	static constexpr int SEND_LOW_WATERMARK_BYTES = 4096;

	Connection(QObject *parent, QSslSocket *qtsSocket);
	~Connection();

	template< typename... Args > void warn(spdlog::format_string_t< Args... > fmt, Args &&... args) const {
		mumble::log::warn("{}:{} -> {}", peerAddress().toStdString(), peerPort(),
						  spdlog::fmt_lib::format(fmt, std::forward< Args >(args)...));
	}

	static bool messageToNetwork(const ::google::protobuf::Message &msg, Mumble::Protocol::TCPMessageType msgType,
								 QByteArray &cache);
	void sendMessage(const QByteArray &qbaMsg);
	/// Sends a tunnelled voice packet unless the send backlog is too large. Returns whether it was sent.
	bool sendVoiceMessage(const QByteArray &qbaMsg);
	void disconnectSocket(bool force = false);
	void forceFlush();

	/// Returns the peer's immediate certificate.
	QSslCertificate peerCertificate() const;
	/// Returns the peer's chain of digital certificates, starting with the peer's immediate certificate
	/// and ending with the CA's certificate.
	QList< QSslCertificate > peerCertificateChain() const;
	QSslCipher sessionCipher() const;
	QSsl::SslProtocol sessionProtocol() const;
	QString sessionProtocolString() const;
	HostAddress peerAddress() const;
	quint16 peerPort() const;
	/// Look up the local address of this Connection.
	HostAddress localAddress() const;
	/// Look up the local port of this Connection.
	quint16 localPort() const;
	bool bDisconnectedEmitted;

	void setToS();
#ifdef Q_OS_WIN
	static void setQoS(HANDLE hParentQoS);
#endif
};

#endif

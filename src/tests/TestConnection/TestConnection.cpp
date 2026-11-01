// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtNetwork>
#include <QtTest>

#include <spdlog/sinks/ringbuffer_sink.h>

#include "Connection.h"
#include "Logger.h"
#include "SSL.h"
#include "SelfSignedCertificate.h"

#ifdef Q_OS_WIN
#	include <winsock2.h>
#else
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <sys/socket.h>
#endif

namespace {
int tcpOption(QAbstractSocket *socket, int option) {
	int value = 0;
#ifdef Q_OS_WIN
	int length = sizeof(value);
#else
	socklen_t length = sizeof(value);
#endif
	getsockopt(static_cast< int >(socket->socketDescriptor()), IPPROTO_TCP, option, reinterpret_cast< char * >(&value),
			   &length);
	return value;
}

// Wraps accepted descriptors in QSslSocket the way the server does, starting TLS only when given a certificate.
class Server : public QTcpServer {
public:
	QList< QSslSocket * > accepted;
	QSslCertificate certificate;
	QSslKey key;

protected:
	void incomingConnection(qintptr descriptor) override {
		auto *socket = new QSslSocket(this);
		socket->setSocketDescriptor(descriptor);
		if (!certificate.isNull()) {
			socket->setLocalCertificate(certificate);
			socket->setPrivateKey(key);
			socket->startServerEncryption();
		}
		accepted << socket;
	}
};

qint64 backlog(QSslSocket *socket) {
	return socket->bytesToWrite() + socket->encryptedBytesToWrite();
}
} // namespace

class TestConnection : public QObject {
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanupTestCase();
	void appliesSocketOptionsToClientSocket();
	void appliesNoDelayToAcceptedSocket();
	void dropsVoiceWhileBacklogged();
	void dropsVoiceWhileEncryptedBacklogged();
	void logsDropEpisodeOnce();
};

void TestConnection::initTestCase() {
	MumbleSSL::initialize();
	mumble::log::init();
}

void TestConnection::cleanupTestCase() {
	MumbleSSL::destroy();
}

void TestConnection::appliesSocketOptionsToClientSocket() {
	Server server;
	QVERIFY(server.listen(QHostAddress::LocalHost));

	auto *socket = new QSslSocket();
	Connection connection(nullptr, socket); // built before connecting, like ServerHandler does
	socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
	QVERIFY(socket->waitForConnected(5000));
	QCoreApplication::processEvents();

	QVERIFY(tcpOption(socket, TCP_NODELAY) != 0);
#ifdef TCP_NOTSENT_LOWAT
	QCOMPARE(tcpOption(socket, TCP_NOTSENT_LOWAT), Connection::SEND_LOW_WATERMARK_BYTES);
#endif
}

void TestConnection::appliesNoDelayToAcceptedSocket() {
	Server server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	QTcpSocket client;
	client.connectToHost(QHostAddress::LocalHost, server.serverPort());
	QVERIFY(client.waitForConnected(5000));
	QTRY_COMPARE(server.accepted.size(), 1);

	Connection connection(nullptr, server.accepted.first());
	QVERIFY(tcpOption(server.accepted.first(), TCP_NODELAY) != 0);
}

// No event loop runs between the writes, so Qt keeps everything in its own buffer and the backlog is exact.
void TestConnection::dropsVoiceWhileBacklogged() {
	Server server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	auto *socket = new QSslSocket();
	Connection connection(nullptr, socket);
	socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
	QVERIFY(socket->waitForConnected(5000));

	const QByteArray voice(100, 'v');
	QVERIFY(connection.sendVoiceMessage(voice));

	connection.sendMessage(QByteArray(Connection::VOICE_DROP_BACKLOG_BYTES, 'c'));
	const qint64 queued = backlog(socket);
	QVERIFY(!connection.sendVoiceMessage(voice));
	QCOMPARE(backlog(socket), queued);

	connection.sendMessage(QByteArray(10, 'c')); // control messages are still queued
	QCOMPARE(backlog(socket), queued + 10);

	QTRY_COMPARE(backlog(socket), qint64(0));
	QVERIFY(connection.sendVoiceMessage(voice));
}

// With TLS, flushing encrypts the plaintext, so the backlog sits in the encrypted buffer.
void TestConnection::dropsVoiceWhileEncryptedBacklogged() {
	Server server;
	QVERIFY(SelfSignedCertificate::generateMurmurV2Certificate(server.certificate, server.key));
	QVERIFY(server.listen(QHostAddress::LocalHost));
	auto *socket = new QSslSocket();
	socket->setPeerVerifyMode(QSslSocket::VerifyNone);
	Connection connection(nullptr, socket);
	socket->connectToHostEncrypted(QHostAddress(QHostAddress::LocalHost).toString(), server.serverPort());
	QTRY_COMPARE(server.accepted.size(), 1);
	QSslSocket *peer = server.accepted.first();
	QTRY_VERIFY(socket->isEncrypted() && peer->isEncrypted());

	// The peer stops reading, so once the kernel buffers are full the flushed data stays in Qt
	peer->setReadBufferSize(1);
	const QByteArray chunk(1024 * 1024, 'c');
	for (int i = 0; i < 256 && socket->encryptedBytesToWrite() <= Connection::VOICE_DROP_BACKLOG_BYTES; ++i) {
		connection.sendMessage(chunk);
		connection.forceFlush();
	}
	QVERIFY(socket->encryptedBytesToWrite() > Connection::VOICE_DROP_BACKLOG_BYTES);
	QVERIFY(socket->bytesToWrite() <= Connection::VOICE_DROP_BACKLOG_BYTES);

	const QByteArray voice(100, 'v');
	const qint64 queued = backlog(socket);
	QVERIFY(!connection.sendVoiceMessage(voice));
	QCOMPARE(backlog(socket), queued);

	connection.sendMessage(QByteArray(10, 'c')); // control messages are still queued
	QCOMPARE(backlog(socket), queued + 10);

	peer->setReadBufferSize(0);
	QTRY_COMPARE_WITH_TIMEOUT(backlog(socket), qint64(0), 30000);
	QVERIFY(connection.sendVoiceMessage(voice));
}

// While the tunnel is congested, the backlog drains between drops. A send in such a gap must not end the episode, or
// every crossing of the limit would log twice.
void TestConnection::logsDropEpisodeOnce() {
	auto sink = std::make_shared< spdlog::sinks::ringbuffer_sink_mt >(16);
	mumble::log::addSink(sink);

	Server server;
	QVERIFY(server.listen(QHostAddress::LocalHost));
	auto *socket = new QSslSocket();
	Connection connection(nullptr, socket);
	socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
	QVERIFY(socket->waitForConnected(5000));

	const QByteArray voice(100, 'v');
	for (int i = 0; i < 2; ++i) {
		connection.sendMessage(QByteArray(Connection::VOICE_DROP_BACKLOG_BYTES + 1, 'c'));
		QVERIFY(!connection.sendVoiceMessage(voice));
		QTRY_COMPARE(backlog(socket), qint64(0));
		QVERIFY(connection.sendVoiceMessage(voice));
	}
	QTest::qWait(static_cast< int >(
		std::chrono::duration_cast< std::chrono::milliseconds >(Connection::VOICE_DROP_QUIET_PERIOD).count() + 100));
	QVERIFY(connection.sendVoiceMessage(voice));

	mumble::log::removeSink(sink);
	int started = 0, ended = 0;
	for (const std::string &line : sink->last_formatted()) {
		started += line.find("Dropping tunnelled voice") != std::string::npos;
		ended += line.find("again after dropping 2 packets") != std::string::npos;
	}
	QCOMPARE(started, 1);
	QCOMPARE(ended, 1);
}

QTEST_MAIN(TestConnection)
#include "TestConnection.moc"

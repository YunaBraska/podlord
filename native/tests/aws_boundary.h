#pragma once
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSslKey>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>
#include <QUrlQuery>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>

namespace podlord::test {
inline void awsBoundaryRequire(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// This TLS proxy replaces only AWS's external boundary. Production URL construction,
// transport, credential ownership, token signing and browser handoff remain real.
class AwsBoundary final : public QTcpServer {
public:
    explicit AwsBoundary(QString mode) : mode(std::move(mode)), originalTls(QSslConfiguration::defaultConfiguration()), originalProxy(QNetworkProxy::applicationProxy()) {
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* raw = nullptr;
        awsBoundaryRequire(context && EVP_PKEY_keygen_init(context.get()) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) == 1
            && EVP_PKEY_keygen(context.get(), &raw) == 1, "Cannot generate external AWS fixture key.");
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
        std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
        awsBoundaryRequire(certificate && X509_set_version(certificate.get(), 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1
            && X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) && X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 86400)
            && X509_set_pubkey(certificate.get(), key.get()) == 1, "Cannot create AWS fixture certificate.");
        auto* name = X509_get_subject_name(certificate.get());
        awsBoundaryRequire(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Podlord local AWS boundary"), -1, -1, 0) == 1
            && X509_set_issuer_name(certificate.get(), name) == 1, "Cannot name AWS fixture certificate.");
        for (const auto& extension : {qMakePair(NID_basic_constraints, "critical,CA:TRUE"), qMakePair(NID_subject_alt_name,
            "DNS:oidc.eu-west-1.amazonaws.com,DNS:portal.sso.eu-west-1.amazonaws.com,DNS:oidc.cn-north-1.amazonaws.com.cn,DNS:portal.sso.cn-north-1.amazonaws.com.cn,DNS:sts.eu-central-1.amazonaws.com,DNS:sts.cn-north-1.amazonaws.com.cn")}) {
            auto* value = X509V3_EXT_conf_nid(nullptr, nullptr, extension.first, extension.second);
            awsBoundaryRequire(value && X509_add_ext(certificate.get(), value, -1) == 1, "Cannot extend AWS fixture certificate.");
            X509_EXTENSION_free(value);
        }
        awsBoundaryRequire(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0, "Cannot sign AWS fixture certificate.");
        std::unique_ptr<BIO, decltype(&BIO_free)> pemKey(BIO_new(BIO_s_mem()), BIO_free), pemCert(BIO_new(BIO_s_mem()), BIO_free);
        awsBoundaryRequire(pemKey && pemCert && PEM_write_bio_PrivateKey(pemKey.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1
            && PEM_write_bio_X509(pemCert.get(), certificate.get()) == 1, "Cannot encode AWS fixture TLS material.");
        BUF_MEM* data = nullptr;
        BIO_get_mem_ptr(pemKey.get(), &data); privateKey = QSslKey(QByteArray(data->data, data->length), QSsl::Rsa);
        BIO_get_mem_ptr(pemCert.get(), &data); ca = QSslCertificate(QByteArray(data->data, data->length));
        awsBoundaryRequire(!privateKey.isNull() && !ca.isNull() && listen(QHostAddress::LocalHost, 0), "Cannot listen for external AWS requests.");
        auto tls = originalTls;
        if (this->mode != "tls_untrusted") tls.addCaCertificate(ca);
        QSslConfiguration::setDefaultConfiguration(tls);
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", serverPort()));
        clock.start();
    }
    ~AwsBoundary() override {
        for (auto* socket : findChildren<QSslSocket*>()) socket->abort();
        close();
        QSslConfiguration::setDefaultConfiguration(originalTls);
        QNetworkProxy::setApplicationProxy(originalProxy);
    }
    QString mode;
    int registrations = 0, devices = 0, tokens = 0, credentials = 0;
    int roles = 0;
    QList<QMap<QString, QString>> roleForms;
    QList<QByteArray> roleHeaders;
    int connections = 0, socketError = 0;
    qint64 deviceAt = 0;
    QList<qint64> pollTimes;
    bool valid = true;
private:
    QSslConfiguration originalTls;
    QNetworkProxy originalProxy;
    QSslKey privateKey;
    QSslCertificate ca;
    QElapsedTimer clock;
    void incomingConnection(qintptr descriptor) override {
        ++connections;
        auto* socket = new QSslSocket(this);
        connect(socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) { socketError = int(error); });
        if (!socket->setSocketDescriptor(descriptor)) { valid = false; socket->deleteLater(); return; }
        socket->setLocalCertificate(ca); socket->setPrivateKey(privateKey); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto bytes = std::make_shared<QByteArray>();
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QSslSocket::readyRead, this, [this, socket, bytes] {
            bytes->append(socket->readAll());
            const auto split = bytes->indexOf("\r\n\r\n");
            if (split < 0) return;
            if (!socket->isEncrypted()) {
                const auto target = bytes->split(' ').value(1);
                if (!bytes->startsWith("CONNECT ") || (target != "oidc.eu-west-1.amazonaws.com:443" && target != "portal.sso.eu-west-1.amazonaws.com:443"
                    && target != "oidc.cn-north-1.amazonaws.com.cn:443" && target != "portal.sso.cn-north-1.amazonaws.com.cn:443"
                    && target != "sts.eu-central-1.amazonaws.com:443" && target != "sts.cn-north-1.amazonaws.com.cn:443")) {
                    valid = false; socket->abort(); return;
                }
                bytes->clear();
                socket->write("HTTP/1.1 200 Connection Established\r\n\r\n"); socket->flush(); socket->startServerEncryption(); return;
            }
            const auto header = bytes->left(split);
            qsizetype length = 0;
            for (const auto& line : header.split('\n')) if (line.trimmed().toLower().startsWith("content-length:")) length = line.trimmed().mid(15).trimmed().toLongLong();
            if (bytes->size() < split + 4 + length) return;
            const auto method = header.split(' ').value(0), path = header.split(' ').value(1);
            const auto request = QJsonDocument::fromJson(bytes->mid(split + 4, length)).object();
            QJsonObject response;
            QByteArray xml;
            QString stage;
            int status = 200;
            if (path == "/") {
                stage = "role"; ++roles;
                const QUrlQuery form(QString::fromLatin1(bytes->mid(split + 4, length)));
                QMap<QString, QString> values;
                for (const auto& item : form.queryItems(QUrl::FullyDecoded)) values.insert(item.first, item.second);
                roleForms.append(values); roleHeaders.append(header);
                valid = valid && method == "POST" && values.value("Action") == "AssumeRole" && values.value("Version") == "2011-06-15"
                    && !values.value("RoleArn").isEmpty() && !values.value("RoleSessionName").isEmpty()
                    && header.contains("AWS4-HMAC-SHA256 Credential=") && header.toLower().contains("content-type: application/x-www-form-urlencoded")
                    && !header.contains("private-signing-secret") && !bytes->mid(split + 4, length).contains("private-signing-secret");
                xml = "<AssumeRoleResponse xmlns=\"https://sts.amazonaws.com/doc/2011-06-15/\"><AssumeRoleResult><Credentials>"
                    "<AccessKeyId>ROLEKEY" + QByteArray::number(roles) + "</AccessKeyId><SecretAccessKey>private-role-secret</SecretAccessKey>"
                    "<SessionToken>private-role-token-" + QByteArray::number(roles) + "</SessionToken><Expiration>"
                    + QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODate).toLatin1()
                    + "</Expiration></Credentials></AssumeRoleResult></AssumeRoleResponse>";
                if (mode == "role_invalid_xml") xml = "private-error-must-not-leak";
            } else if (path == "/client/register") {
                stage = "register"; ++registrations;
                valid = valid && method == "POST" && request.value("clientName") == "Podlord" && request.value("clientType") == "public"
                    && request.value("scopes").toArray() == QJsonArray{"sso:account:access"}
                    && request.value("grantTypes").toArray() == QJsonArray{"urn:ietf:params:oauth:grant-type:device_code"};
                response = {{"clientId", "public-client"}, {"clientSecret", "private-client-secret"}, {"clientSecretExpiresAt", QDateTime::currentSecsSinceEpoch() + 86400}};
                if (mode == "register_empty_id") response["clientId"] = "";
                if (mode == "register_bad_secret") response["clientSecret"] = "bad secret";
                if (mode == "register_expired") response["clientSecretExpiresAt"] = 1;
            } else if (path == "/device_authorization") {
                stage = "device"; ++devices;
                deviceAt = clock.elapsed();
                valid = valid && method == "POST" && request.value("clientId") == "public-client" && request.value("clientSecret") == "private-client-secret"
                    && request.value("startUrl") == "https://company.awsapps.com/start";
                response = {{"deviceCode", "private-device-code"}, {"userCode", "ABCD-EFGH"}, {"verificationUri", "https://device.sso.example/verify"},
                    {"verificationUriComplete", "https://device.sso.example/verify?user_code=ABCD-EFGH"}, {"expiresIn", 300}, {"interval", 0}};
                if (mode == "device_no_complete") response.remove("verificationUriComplete");
                if (mode == "device_empty_code") response["deviceCode"] = "";
                if (mode == "device_empty_user") response["userCode"] = "";
                if (mode == "device_http") response["verificationUri"] = "http://device.sso.example/verify";
                if (mode == "device_bad_complete") response["verificationUriComplete"] = "https://wrong.example/verify";
                if (mode == "device_bad_uri") response["verificationUri"] = "https://user:secret@device.sso.example/verify";
                if (mode == "device_bad_fragment") response["verificationUri"] = "https://device.sso.example/verify#fragment";
                if (mode == "device_zero") response["expiresIn"] = 0;
                if (mode == "device_long") response["expiresIn"] = 3601;
                if (mode == "device_expired") response["expiresIn"] = 1;
                if (mode == "device_negative_interval") response["interval"] = -1;
                if (mode == "device_long_interval") response["interval"] = 601;
            } else if (path == "/token") {
                stage = "token"; ++tokens;
                pollTimes.append(clock.elapsed());
                valid = valid && method == "POST" && request.value("clientId") == "public-client" && request.value("clientSecret") == "private-client-secret"
                    && request.value("deviceCode") == "private-device-code" && request.value("grantType") == "urn:ietf:params:oauth:grant-type:device_code";
                response = {{"accessToken", "private-access-token"}, {"expiresIn", 3600}};
                if (tokens == 1 && (mode == "pending" || mode == "slow_down" || mode == "pending_type" || mode == "slow_type")) {
                    status = 400;
                    response = mode == "pending_type" || mode == "slow_type"
                        ? QJsonObject{{"__type", mode == "pending_type" ? "aws#AuthorizationPendingException" : "aws#SlowDownException"}}
                        : QJsonObject{{"error", mode == "pending" ? "authorization_pending" : "slow_down"}};
                }
                if (mode == "denied") { status = 400; response = {{"error", "access_denied"}, {"message", "private-error-must-not-leak"}}; }
                if (mode == "token_empty") response["accessToken"] = "";
                if (mode == "token_space") response["accessToken"] = "bad token";
                if (mode == "token_short") response["expiresIn"] = 30;
                if (mode == "token_long") response["expiresIn"] = 86401;
            } else if (path.startsWith("/federation/credentials?")) {
                stage = "credentials"; ++credentials;
                const QUrlQuery query(QUrl::fromEncoded(path));
                valid = valid && method == "GET" && query.queryItemValue("account_id") == "123456789012"
                    && query.queryItemValue("role_name") == "Developer" && header.toLower().contains("x-amz-sso_bearer_token: private-access-token");
                QJsonObject fields{{"accessKeyId", "SSOKEY"}, {"secretAccessKey", "private-signing-secret"}, {"sessionToken", "private-session-token"},
                    {"expiration", QDateTime::currentMSecsSinceEpoch() + (mode == "short_role" ? 120000 : 3600000)}};
                if (mode == "credentials_empty_key") fields["accessKeyId"] = "";
                if (mode == "credentials_bad_secret") fields["secretAccessKey"] = "bad secret";
                if (mode == "credentials_empty_session") fields["sessionToken"] = "";
                if (mode == "credentials_expired") fields["expiration"] = 1;
                if (mode == "credentials_long") fields["expiration"] = QDateTime::currentMSecsSinceEpoch() + qint64(172800000);
                response = {{"roleCredentials", fields}};
                if (mode == "credentials_missing") response.remove("roleCredentials");
            } else { valid = false; status = 404; }
            if (!valid) std::fprintf(stderr, "External AWS contract mismatch at %s, method %s.\n", qPrintable(stage), method.constData());
            if (mode == stage + "_pending" || mode == stage + "_timeout") return;
            if (mode == stage + "_denied" || (stage == "role" && mode == "mfa_denied")) status = 403;
            if (mode == stage + "_redirect") status = 302;
            if (mode == stage + "_server") status = 503;
            QByteArray body = stage == "role" ? xml : QJsonDocument(response).toJson(QJsonDocument::Compact);
            if (mode == stage + "_bad_json") body = "private-error-must-not-leak";
            if (mode == stage + "_array") body = "[]";
            if (mode == stage + "_oversized") body = QByteArray(65537, 'x');
            socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost(); bytes->clear();
        });
    }
};
} // namespace podlord::test

#include "native_eks.h"
#include <QCoreApplication>
#include <QUrlQuery>
#include <cstdio>

using namespace podlord;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString scenario = QString::fromUtf8(argv[1]);
    const auto now = QDateTime::fromString("2026-11-05T12:00:00Z", Qt::ISODate);
    if (scenario.startsWith("xml_")) {
        QByteArray xml = "<AssumeRoleResponse xmlns=\"https://sts.amazonaws.com/doc/2011-06-15/\"><AssumeRoleResult><Credentials>"
            "<AccessKeyId>ROLEKEY</AccessKeyId><SecretAccessKey>secret-key</SecretAccessKey><SessionToken>session+token/=</SessionToken>"
            "<Expiration>2026-11-05T13:00:00Z</Expiration></Credentials></AssumeRoleResult></AssumeRoleResponse>";
        if (scenario == "xml_unicode") xml.replace("ROLEKEY", "ROLE\xE2\x82\xAC");
        if (scenario == "xml_expired") xml.replace("T13:00:00Z", "T12:00:00Z");
        if (scenario == "xml_far_future") xml.replace("2026-11-05T13", "2026-11-07T13");
        if (scenario == "xml_no_timezone") xml.replace("T13:00:00Z", "T13:00:00");
        if (scenario == "xml_missing") xml.replace("<AccessKeyId>ROLEKEY</AccessKeyId>", "");
        if (scenario == "xml_duplicate") xml.replace("</Credentials>", "<AccessKeyId>SECOND</AccessKeyId></Credentials>");
        if (scenario == "xml_nested") xml.replace("ROLEKEY", "<nested>ROLEKEY</nested>");
        if (scenario == "xml_wrong_namespace") xml.replace("https://sts.amazonaws.com/doc/2011-06-15/", "https://invalid.example/");
        if (scenario == "xml_wrong_root") xml.replace("AssumeRoleResponse", "OtherResponse");
        if (scenario == "xml_malformed") xml.chop(4);
        if (scenario == "xml_oversized") xml = QByteArray(65537, 'x');
        if (scenario == "xml_dtd") xml.prepend("<!DOCTYPE AssumeRoleResponse [<!ENTITY key 'ROLEKEY'>]>");
        if (scenario == "xml_entity") { xml.replace("ROLEKEY", "&key;"); xml.prepend("<!DOCTYPE AssumeRoleResponse [<!ENTITY key 'ROLEKEY'>]>"); }
        if (scenario == "xml_empty_token") xml.replace("session+token/=", "");
        if (scenario == "xml_control") xml.replace("ROLEKEY", "ROLE\nKEY");
        if (scenario == "xml_long_key") xml.replace("ROLEKEY", QByteArray(129, 'K'));
        if (scenario == "xml_empty") xml.clear();
        if (scenario == "xml_no_result") xml = "<AssumeRoleResponse xmlns=\"https://sts.amazonaws.com/doc/2011-06-15/\"/>";
        if (scenario == "xml_two_results") xml.replace("</AssumeRoleResponse>", "<AssumeRoleResult/></AssumeRoleResponse>");
        if (scenario == "xml_two_credentials") xml.replace("</AssumeRoleResult>", "<Credentials/></AssumeRoleResult>");
        if (scenario == "xml_unknown_field") xml.replace("</Credentials>", "<Unknown>x</Unknown></Credentials>");
        if (scenario == "xml_depth") xml.replace("</AssumeRoleResult>", "<a><b><c><d><e><f><g><h><i/></h></g></f></e></d></c></b></a></AssumeRoleResult>");
        if (scenario == "xml_unicode_secret") xml.replace("secret-key", "secret-\xE2\x82\xAC");
        if (scenario == "xml_unicode_token") xml.replace("session+token/=", "session-\xE2\x82\xAC");
        if (scenario == "xml_empty_key") xml.replace("ROLEKEY", "");
        if (scenario == "xml_empty_secret") xml.replace("secret-key", "");
        if (scenario == "xml_long_secret") xml.replace("secret-key", QByteArray(129, 's'));
        if (scenario == "xml_long_token") xml.replace("session+token/=", QByteArray(8193, 't'));
        if (scenario == "xml_invalid_expiry") xml.replace("2026-11-05T13:00:00Z", "invalidZ");
        if (scenario == "xml_invalid_time") return std::holds_alternative<Failure>(nativeAwsRoleCredentials(xml, {})) ? 0 : 1;
        const auto result = nativeAwsRoleCredentials(xml, now);
        if (scenario != "xml_valid") return std::holds_alternative<Failure>(result) ? 0 : 1;
        const auto* value = std::get_if<AwsTemporaryCredentials>(&result);
        return value && value->accessKey == "ROLEKEY" && value->secretKey == "secret-key"
            && value->sessionToken == "session+token/=" && value->expiresAt == now.addSecs(3600) ? 0 : 1;
    }
    EksLogin login;
    login.region = "eu-central-1"; login.cluster = "example";
    login.accessKey = "AKIDEXAMPLE"; login.secretKey = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    AwsRole role{"arn:aws:iam::123456789012:role/Target", "podlord-test", {}, {}, 3600};
    QString code;
    bool invalid = false;
    if (scenario == "sign_mfa" || scenario == "sign_bad_mfa" || scenario == "sign_unicode_mfa") {
        role.mfaSerial = "arn:aws:iam::123456789012:mfa/user"; code = "123456";
        if (scenario == "sign_bad_mfa") { code = "12345"; invalid = true; }
        if (scenario == "sign_unicode_mfa") { code = QString(6, QChar(0xFF11)); invalid = true; }
    }
    if (scenario == "sign_unrequested_mfa") { code = "123456"; invalid = true; }
    if (scenario == "sign_bad_role") { role.arn = "https://attacker.example/role"; invalid = true; }
    if (scenario == "sign_bad_session") { role.sessionName = "bad session"; invalid = true; }
    if (scenario == "sign_bad_duration") { role.durationSeconds = 899; invalid = true; }
    if (scenario == "sign_expired_source") { login.credentialExpiresAt = now; invalid = true; }
    if (scenario == "sign_missing_key") { login.accessKey.clear(); invalid = true; }
    if (scenario == "sign_session_token") login.sessionToken = "temporary+token/=";
    if (scenario == "sign_china") login.region = "cn-north-1";
    if (scenario == "sign_external") role.externalId = "external/id+value";
    if (scenario.startsWith("sign_invalid_")) {
        invalid = true;
        const auto field = scenario.mid(13);
        if (field == "region") login.region = "invalid";
        if (field == "long_region") login.region = QString(65, 'r');
        if (field == "secret") login.secretKey.clear();
        if (field == "key_control") login.accessKey = "bad key";
        if (field == "key_length") login.accessKey = QByteArray(129, 'k');
        if (field == "secret_control") login.secretKey = "bad secret";
        if (field == "secret_length") login.secretKey = QByteArray(129, 's');
        if (field == "token_control") login.sessionToken = "bad token";
        if (field == "token_length") login.sessionToken = QByteArray(8193, 't');
        if (field == "empty_session") role.sessionName.clear();
        if (field == "long_arn") role.arn = "arn:aws:iam::123456789012:role/" + QString(2048, 'a');
        if (field == "external") role.externalId = "bad external";
        if (field == "serial") role.mfaSerial = "bad serial";
        if (field == "long_duration") role.durationSeconds = 43201;
    }
    if (scenario == "sign_invalid_time") return std::holds_alternative<Failure>(nativeAwsRoleRequest(login, role, {}, {})) ? 0 : 1;
    const auto result = nativeAwsRoleRequest(login, role, code, now);
    if (invalid) return std::holds_alternative<Failure>(result) ? 0 : 1;
    const auto* request = std::get_if<AwsRoleRequest>(&result);
    if (!request) return 1;
    if (scenario == "sign_valid" && !request->headers.value("authorization").endsWith("Signature=af7c3e57dad563c0b4b92b6726454f40eee9e324e49171d5ccda039bb6a6a317")) {
        std::fprintf(stderr, "AWS STS reference signature differs.\n"); return 1;
    }
    const QUrlQuery body(QString::fromLatin1(request->body));
    const bool passed = request->url == QUrl(scenario == "sign_china" ? "https://sts.cn-north-1.amazonaws.com.cn/" : "https://sts.eu-central-1.amazonaws.com/")
        && body.queryItemValue("Action") == "AssumeRole" && body.queryItemValue("Version") == "2011-06-15"
        && body.queryItemValue("RoleArn", QUrl::FullyDecoded) == role.arn && body.queryItemValue("RoleSessionName") == role.sessionName
        && body.queryItemValue("DurationSeconds") == "3600" && request->headers.value("authorization").startsWith("AWS4-HMAC-SHA256 ")
        && !request->url.hasQuery() && !request->body.contains(login.secretKey)
        && (code.isEmpty() || body.queryItemValue("TokenCode") == code)
        && (role.externalId.isEmpty() || body.queryItemValue("ExternalId", QUrl::FullyDecoded) == role.externalId)
        && (login.sessionToken.isEmpty() || request->headers.value("x-amz-security-token") == login.sessionToken);
    if (!passed) std::fprintf(stderr, "AWS STS public request contract failed.\n");
    return passed ? 0 : 1;
}

#include "kubeconfig_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool accepted, const char* message) {
    if (!accepted) throw std::runtime_error(message);
}
void write(const QString& path, const QByteArray& bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create fixture directory.");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(bytes)==bytes.size(), "Cannot write source fixture.");
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read source fixture.");
    return file.readAll();
}
bool run(const QString& scenario) {
    using namespace podlord;
    require(QStringList{"content","local","repeat","invalid","empty","limit","byte_limit","at_limit","missing","removed","mismatch","snapshot_identity","context_identity","tampered","symlink"}.contains(scenario), "Unknown owned-source editor scenario.");
    QTemporaryDir temporary;
    require(temporary.isValid(), "Private fixture directory unavailable.");
    const auto profile=temporary.filePath("profile"), originalPath=temporary.filePath("original-config");
    const QByteArray original("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster: {server: 'https://127.0.0.1:9'}\nusers:\n- name: reader\n  user: {token: owned-editor-private-token}\ncontexts:\n- name: alpha\n  context: {cluster: local, user: reader}\n- name: beta\n  context: {cluster: local, user: reader}\n");
    write(originalPath,original);
    const KubeconfigStore store(profile);
    if (scenario=="local") {
        require(std::holds_alternative<SourceSnapshot>(store.importFile(originalPath)), "Local source ingress failed.");
    } else {
        const QString origin="content://podlord.test.owned-editor/document/config";
        const auto digest=[](const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); };
        const auto hash=digest(original), id=digest(origin.toUtf8())+"-"+hash;
        // Protected owned-envelope input, matching document-import storage. No provider is contacted.
        const QJsonObject record{{"version",1},{"sourcePath",origin},{"contentHash",hash},
            {"yamlBase64",QString::fromLatin1(original.toBase64())},{"importedAt","2026-01-01T00:00:00.000Z"}};
        const auto path=profile+"/kubeconfigs/"+id+".json";
        write(path,QJsonDocument(record).toJson());
        require(QFile::setPermissions(profile,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)
            && QFile::setPermissions(profile+"/kubeconfigs",QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)
            && QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner), "Cannot protect owned document fixture.");
    }
    const auto loaded=store.list();
    require(std::holds_alternative<SourceCatalog>(loaded), "Owned source catalog unavailable.");
    const auto catalog=std::get<SourceCatalog>(loaded);
    require(catalog.errors.isEmpty() && catalog.sources.size()==1, "Owned source fixture was not validated.");
    const auto source=catalog.sources.first();
    require(source.contexts.size()==2, "Owned source contexts missing.");
    QString snapshot=source.id, context=source.contexts.first().id;
    const auto baseline=store.ownedYaml(snapshot,context);
    require(std::holds_alternative<QString>(baseline) && std::get<QString>(baseline)==QString::fromUtf8(original), "Owned baseline read failed.");
    const SessionStore sessions(profile);
    for (const auto& value : source.contexts)
        require(std::holds_alternative<SessionCatalog>(sessions.create({value.id,{}},value.name)), "Cannot create saved session fixture.");
    const auto saved=sessions.list();
    require(std::holds_alternative<SessionCatalog>(saved), "Saved sessions unavailable.");
    require(std::holds_alternative<SessionCatalog>(sessions.activate(std::get<SessionCatalog>(saved).sessions.first().id)), "Cannot select saved session fixture.");
    QString draft=QString::fromUtf8(original)+"# edited owned copy\n";
    StoreError expected=StoreError::InvalidData;
    const bool rejected=QStringList{"invalid","empty","limit","byte_limit","missing","removed","mismatch","snapshot_identity","context_identity","tampered","symlink"}.contains(scenario);
    constexpr qsizetype limit=16*1024*1024;
    if (scenario=="content") {
        const auto pasted=store.importText(source.sourcePath,draft);
        require(std::holds_alternative<Failure>(pasted) && std::get<Failure>(pasted).code==StoreError::InvalidInput, "Public paste accepted a document URI origin.");
    } else if (scenario=="repeat") draft=QString::fromUtf8(original);
    else if (scenario=="invalid") draft="contexts: [\n# owned-editor-private-token\n";
    else if (scenario=="empty") draft.clear();
    else if (scenario=="limit") draft=QString(limit+1,QLatin1Char('a'));
    else if (scenario=="byte_limit") {
        draft=QString::fromUtf8(original)+"#"+QString(limit/2,QChar(0x00e9))+"\n";
        require(draft.size()<=limit && draft.toUtf8().size()>limit, "UTF-8 byte-limit fixture is invalid.");
    } else if (scenario=="at_limit") {
        draft=QString::fromUtf8(original)+"#";
        draft+=QString(limit-draft.size()-1,QLatin1Char('a'))+"\n";
        require(draft.toUtf8().size()==limit, "Exact import-limit fixture is invalid.");
    } else if (scenario=="missing") {
        require(QFile::remove(source.ownedPath), "Cannot remove owned fixture."); expected=StoreError::ReadFailed;
    } else if (scenario=="removed") {
        QStringList affected;
        for (const auto& value : std::get<SessionCatalog>(saved).sessions)
            if (value.config.contextId==context) affected.append(value.id.toString(QUuid::WithoutBraces));
        require(std::holds_alternative<SessionCatalog>(store.removeContext(context,affected)), "Cannot remove context fixture.");
        expected=StoreError::NotFound;
    } else if (scenario=="mismatch") {
        const auto other=store.importText(temporary.filePath("other-config"),QString::fromUtf8(original));
        require(std::holds_alternative<SourceSnapshot>(other), "Cannot import unrelated source fixture.");
        context=std::get<SourceSnapshot>(other).contexts.first().id; expected=StoreError::NotFound;
    } else if (scenario=="snapshot_identity") { snapshot="../original-config"; expected=StoreError::InvalidInput; }
    else if (scenario=="context_identity") { context="../original-config"; expected=StoreError::InvalidInput; }
    else if (scenario=="tampered") write(source.ownedPath,"{broken}");
    else if (scenario=="symlink") {
        require(QFile::remove(source.ownedPath) && QFile::link(originalPath,source.ownedPath), "Cannot replace owned fixture with symlink.");
    }
    const auto before=sessions.list();
    require(std::holds_alternative<SessionCatalog>(before), "Session baseline unavailable.");
    const QDir ownedDirectory(profile+"/kubeconfigs");
    const auto files=ownedDirectory.entryList({"*.json"},QDir::Files|QDir::System,QDir::Name);
    const bool exists=QFileInfo::exists(source.ownedPath);
    const auto envelope=exists ? read(source.ownedPath) : QByteArray{};
    const auto edited=store.editOwnedYaml(snapshot,context,draft);
    if (rejected) {
        require(std::holds_alternative<Failure>(edited), "Rejected owned-source edit succeeded.");
        const auto error=std::get<Failure>(edited);
        require(error.code==expected && !error.message.isEmpty() && !error.message.contains("owned-editor-private-token"), "Owned-source edit returned an incorrect or sensitive failure.");
        require(ownedDirectory.entryList({"*.json"},QDir::Files|QDir::System,QDir::Name)==files
            && QFileInfo::exists(source.ownedPath)==exists && (!exists || read(source.ownedPath)==envelope), "Rejected edit modified owned snapshots.");
    } else {
        require(std::holds_alternative<SourceSnapshot>(edited), "Validated owned-source edit failed.");
        const auto result=std::get<SourceSnapshot>(edited);
        require(result.sourcePath==source.sourcePath && (result.id==source.id)==(scenario=="repeat"), "Edited snapshot changed origin or has incorrect identity.");
        const auto current=store.ownedYaml(result.id,result.contexts.first().id), previous=store.ownedYaml(source.id,source.contexts.first().id);
        require(std::holds_alternative<QString>(current) && std::get<QString>(current)==draft
            && std::holds_alternative<QString>(previous) && std::get<QString>(previous)==QString::fromUtf8(original), "Edited or prior immutable YAML changed.");
        if (scenario!="repeat") require(read(source.ownedPath)==envelope, "New snapshot rewrote the previous owned record.");
        const auto after=store.list();
        require(std::holds_alternative<SourceCatalog>(after) && std::get<SourceCatalog>(after).errors.isEmpty()
            && std::get<SourceCatalog>(after).sources.size()==(scenario=="repeat" ? 1 : 2), "Edited source lost or duplicated snapshots.");
        if (scenario=="local") {
            const auto pasted=store.importText(QUrl::fromLocalFile(originalPath).toString(),draft);
            require(std::holds_alternative<SourceSnapshot>(pasted) && std::get<SourceSnapshot>(pasted).id==result.id, "Existing local paste behavior changed.");
        }
    }
    const auto after=sessions.list();
    require(std::holds_alternative<SessionCatalog>(after) && std::get<SessionCatalog>(after)==std::get<SessionCatalog>(before), "Owned-source edit changed existing sessions or selection.");
    require(read(originalPath)==original, "Owned-source edit changed the external file.");
    return true;
}
}
int main(int argc,char** argv) {
    const QCoreApplication application(argc,argv);
    if (argc!=2) return 2;
    try { return run(QString::fromLocal8Bit(argv[1])) ? 0 : 1; }
    catch (const std::exception& error) { std::fprintf(stderr,"Owned-source editor %s: %s\n",argv[1],error.what()); return 1; }
}

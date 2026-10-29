#include "map_previews.h"
#include "kubeconfig_store.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>

namespace podlord {
MapPreviews::MapPreviews(QString profile) : profile_(std::move(profile)) {
    connect(&task_, &QFutureWatcher<Snapshot>::finished, this, [this] {
        const auto snapshot = task_.result(); busy_=false;
        if (snapshot.readable) urls_ = snapshot.urls;
        for (auto it=urls_.begin();it!=urls_.end();) {
            if (!contexts_.contains(it.key())) it=urls_.erase(it); else ++it;
        }
        error_ = snapshot.error; ++revision_; emit changed();
        dispatch();
    });
}
MapPreviews::~MapPreviews() {
    task_.waitForFinished();
    if (dirty_) persist(profile_,contexts_,authoritative_,pending_,removed_);
}
void MapPreviews::reconcile(QSet<QString> contexts, bool authoritative) {
    contexts_ = std::move(contexts); authoritative_ = authoritative; dirty_ = true;
    for (auto it=pending_.begin();it!=pending_.end();) {
        if (!contexts_.contains(it.key())) it=pending_.erase(it); else ++it;
    }
    dispatch();
}
bool MapPreviews::save(const QString& context, const QImage& image) {
    if (!contexts_.contains(context) || image.isNull() || image.width()>320 || image.height()>200) return false;
    pending_.insert(context,image); dirty_=true; dispatch(); return true;
}
void MapPreviews::forget(const QString& context) {
    contexts_.remove(context); pending_.remove(context); urls_.remove(context);
    removed_.insert(context); dirty_=true; dispatch();
}
void MapPreviews::dispatch() {
    if (busy_ || !dirty_) return;
    dirty_=false; busy_=true;
    const auto profile=profile_; const auto contexts=contexts_; const bool prune=authoritative_;
    auto images=std::move(pending_); pending_.clear();
    auto removed=std::move(removed_); removed_.clear();
    task_.setFuture(QtConcurrent::run([profile,contexts,prune,images=std::move(images),removed=std::move(removed)] { return persist(profile,contexts,prune,images,removed); }));
}
MapPreviews::Snapshot MapPreviews::persist(const QString& profile,const QSet<QString>& contexts,bool prune,const QMap<QString,QImage>& images,const QSet<QString>& removed) {
    Snapshot result;
    if (const auto failure=profileFailure(profile)) { result.error=failure->message; return result; }
    const auto folder=QDir(profile).filePath("map-previews");
    if (QFileInfo(folder).isSymLink() || (QFileInfo::exists(folder) && !QFileInfo(folder).isDir())) {
        result.error="Map preview storage must be a private directory, not a link or file."; return result;
    }
    QDir directory(folder);
    if (!directory.exists()) {
        if (images.isEmpty()) { result.readable=true; return result; }
        if (!QDir().mkpath(folder) || !QFile::setPermissions(folder,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)) {
            result.error="Cannot create private map preview storage."; return result;
        }
    }
    QMap<QString,QString> files,versions;
    for (const auto& context:removed) {
        const auto path=directory.filePath(QString::fromLatin1(QCryptographicHash::hash(context.toUtf8(),QCryptographicHash::Sha256).toHex())+".png");
        if (QFileInfo::exists(path) && !QFile::remove(path)) result.error="The removed context's map preview could not be deleted.";
    }
    for (const auto& context:contexts)
        files.insert(QString::fromLatin1(QCryptographicHash::hash(context.toUtf8(),QCryptographicHash::Sha256).toHex())+".png",context);
    const QRegularExpression owned("^[0-9a-f]{64}\\.png$");
    auto protectedFiles=files;
    bool canPrune=false;
    if (prune) {
        const auto current=KubeconfigStore(profile).list();
        if (const auto* catalog=std::get_if<SourceCatalog>(&current); catalog && catalog->errors.isEmpty()) {
            canPrune=true;
            for (const auto& source:catalog->sources) for (const auto& context:source.contexts)
                protectedFiles.insert(QString::fromLatin1(QCryptographicHash::hash(context.id.toUtf8(),QCryptographicHash::Sha256).toHex())+".png",context.id);
        }
    }
    if (canPrune) for (const auto& entry:directory.entryInfoList({"*.png"},QDir::Files|QDir::System|QDir::NoDotAndDotDot)) {
        if (!owned.match(entry.fileName()).hasMatch() || protectedFiles.contains(entry.fileName()) || entry.isSymLink()) continue;
        if (!QFile::remove(entry.absoluteFilePath())) result.error="An orphaned map preview could not be removed.";
    }
    constexpr qint64 maximumBytes=512*1024;
    for (auto image=images.cbegin();image!=images.cend();++image) {
        if (!contexts.contains(image.key())) continue;
        const auto name=QString::fromLatin1(QCryptographicHash::hash(image.key().toUtf8(),QCryptographicHash::Sha256).toHex())+".png";
        const auto path=directory.filePath(name);
        if (QFileInfo(path).isSymLink()) { result.error="Map preview files must not be symbolic links."; continue; }
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!image.value().save(&buffer,"PNG") || bytes.size()>maximumBytes) { result.error="Map preview encoding failed; the previous preview was retained."; continue; }
        QLockFile lock(path+".lock"); lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) { result.error="Map preview storage is busy; the previous preview was retained."; continue; }
        QFile previous(path);
        const bool unchanged=previous.size()<=maximumBytes && previous.open(QIODevice::ReadOnly) && previous.readAll()==bytes;
        previous.close();
        if (!unchanged) {
            QSaveFile file(path); file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
                || file.write(bytes)!=bytes.size() || !file.commit()) {
                result.error="Cannot save the map preview; the previous preview was retained."; continue;
            }
        }
        versions.insert(image.key(),QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
    }
    for (auto file=files.cbegin();file!=files.cend();++file) {
        const QFileInfo info(directory.filePath(file.key()));
        if (!info.exists()) continue;
        if (info.isSymLink() || !info.isFile() || info.size()>maximumBytes) { result.error="An invalid map preview was ignored."; continue; }
        QImageReader reader(info.absoluteFilePath());
        const auto size=reader.size();
        if (!size.isValid() || size.width()>320 || size.height()>200) { result.error="An invalid map preview was ignored."; continue; }
        auto url=QUrl::fromLocalFile(info.absoluteFilePath());
        url.setQuery("v="+versions.value(file.value(),QString::number(info.lastModified().toMSecsSinceEpoch())+"-"+QString::number(info.size())));
        result.urls.insert(file.value(),url.toString());
    }
    result.readable=true; return result;
}
}

/*******************************************************************************************************
 DkImageContainer.cpp
 Created on:	21.02.2014

 nomacs is a fast and small image viewer with the capability of synchronizing multiple instances

 Copyright (C) 2011-2014 Markus Diem <markus@nomacs.org>
 Copyright (C) 2011-2014 Stefan Fiel <stefan@nomacs.org>
 Copyright (C) 2011-2014 Florian Kleber <florian@nomacs.org>

 This file is part of nomacs.

 nomacs is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 nomacs is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 *******************************************************************************************************/

#include "DkImageContainer.h"
#include "DkBasicLoader.h"
#include "DkImageStorage.h"
#include "DkMetaData.h"
#include "DkSettings.h"
#include "DkTimer.h"
#include "DkUtils.h"

#pragma warning(push, 0) // no warnings from includes - begin
#include <QDir>
#include <QImage>
#include <QObject>
#include <QRegularExpression>
#include <QtConcurrentRun>

// quazip
#ifdef WITH_QUAZIP
#include <quazip/JlCompress.h>
#endif
#pragma warning(pop) // no warnings from includes - end

#pragma warning(disable : 4251) // TODO: remove

namespace nmc
{
#ifdef WITH_QUAZIP
QString DkZipContainer::mZipMarker = "dIrChAr";
#endif

// DkImageContainer --------------------------------------------------------------------
/**
 * Creates a DkImageContainer.
 * This class is the basic image management class.
 * @param fileInfo the file of the given
 **/
DkImageContainer::DkImageContainer(const QString &filePath)
    : mOriginalFilePath{filePath}
{
    setFilePath(filePath);
    init();
}

DkImageContainer::~DkImageContainer()
{
}

void DkImageContainer::init()
{
    mEdited = false;
    mSelected = false;

    // always keep in mind that a file does not exist
    if (!mEdited && mLoadState != exists_not)
        mLoadState = not_loaded;
}

bool DkImageContainer::operator==(const DkImageContainer &ric) const
{
    return mFilePath == ric.filePath();
}

void DkImageContainer::clear()
{
    if (mLoader)
        mLoader->release();
    if (mFileBuffer)
        mFileBuffer->clear();
    init();
}

void DkImageContainer::undo()
{
    getLoader()->undo();
}

void DkImageContainer::redo()
{
    getLoader()->redo();
}

void DkImageContainer::setHistoryIndex(int idx)
{
    getLoader()->setHistoryIndex(idx);
}

void DkImageContainer::cropImage(const DkRotatingRect &rect, const QColor &col, bool cropToMetadata)
{
    if (!cropToMetadata) {
        QImage cropped = DkImage::cropToImage(image(), rect, col);
        setImage(cropped, QObject::tr("Cropped"));
        getMetaData()->clearXMPRect();
    } else
        getMetaData()->saveRectToXMP(rect, image().size());
}

QFileInfo DkImageContainer::fileInfo() const
{
    return mFileInfo;
}

QString DkImageContainer::filePath() const
{
    return mFilePath;
}

QString DkImageContainer::dirPath() const
{
    if (!mFileInfo.isFile())
        return "";

#ifdef WITH_QUAZIP
    if (mZipData && mZipData->isZip())
        mZipData->getZipFilePath();
#endif

    return mFileInfo.absolutePath();
}

QString DkImageContainer::fileName() const
{
    return mFileInfo.fileName();
}

bool DkImageContainer::isFromZip()
{
#ifdef WITH_QUAZIP
    return getZipData() && getZipData()->isZip();
#else
    return false;
#endif
}

bool DkImageContainer::exists()
{
#ifdef WITH_QUAZIP

    if (isFromZip())
        return true;
#endif

    return QFileInfo(mFilePath).exists();
}

QString DkImageContainer::getTitleAttribute() const
{
    if (!mLoader || mLoader->getNumPages() <= 1)
        return QString();

    QString attr = "[" + QString::number(mLoader->getPageIdx()) + "/" + QString::number(mLoader->getNumPages()) + "]";

    return attr;
}

QSharedPointer<DkBasicLoader> DkImageContainer::getLoader()
{
    if (!mLoader) {
        mLoader = QSharedPointer<DkBasicLoader>(new DkBasicLoader());
    }

    return mLoader;
}

/**
 * @brief Returns the pointer to the current metadata object, see DkBasicLoader::getMetaData().
 *
 * @return QSharedPointer<DkMetaDataT>
 */
QSharedPointer<DkMetaDataT> DkImageContainer::getMetaData()
{
    return getLoader()->getMetaData();
}

QSharedPointer<DkImageContainerT> DkImageContainerT::fromImageContainer(QSharedPointer<DkImageContainer> imgC)
{
    if (!imgC)
        return QSharedPointer<DkImageContainerT>();

    QSharedPointer<DkImageContainerT> imgCT = QSharedPointer<DkImageContainerT>(new DkImageContainerT(imgC->filePath()));

    imgCT->mLoader = imgC->getLoader();
    imgCT->mEdited = imgC->isEdited();
    imgCT->mSelected = imgC->isSelected();
    imgCT->mLoadState = imgC->getLoadState();
    imgCT->mFileBuffer = imgC->getFileBuffer();

    return imgCT;
}

QSharedPointer<QByteArray> DkImageContainer::getFileBuffer()
{
    if (!mFileBuffer) {
        mFileBuffer = QSharedPointer<QByteArray>(new QByteArray());
    }

    return mFileBuffer;
}

float DkImageContainer::getMemoryUsage() const
{
    if (!mLoader)
        return 0;

    float memSize = mFileBuffer ? mFileBuffer->size() / (1024.0f * 1024.0f) : 0;
    memSize += DkImage::getBufferSizeFloat(mLoader->image().size(), mLoader->image().depth());

    return memSize;
}

float DkImageContainer::getFileSize() const
{
    return QFileInfo(mFilePath).size() / (1024.0f * 1024.0f);
}

DkRotatingRect DkImageContainer::cropRect()
{
    QSharedPointer<DkMetaDataT> metaData = getMetaData();

    if (metaData) {
        return metaData->getXMPRect(image().size());
    } else
        qWarning() << "empty crop rect because there are no metadata...";

    return DkRotatingRect();
}

std::function<bool(const QSharedPointer<DkImageContainer> &, const QSharedPointer<DkImageContainer> &)> DkImageContainer::compareFunc()
{
    // select from the assortment of QFileInfo functions; if there isn't one use this one
    // future: exif, custom sorting, etc can all be tied in here, need not be QFileInfo
    std::function<bool(const QFileInfo &, const QFileInfo &FileInfo)> cmp;

    int mode = DkSettingsManager::param().global().sortMode;

    switch ((DkSettings::sortMode)mode) {
    case DkSettings::sort_filename:
        cmp = &DkUtils::compFilename;
        break;
    case DkSettings::sort_date_created:
        cmp = &DkUtils::compDateCreated;
        break;
    case DkSettings::sort_file_size:
        cmp = &DkUtils::compFileSize;
        break;
    case DkSettings::sort_date_modified:
        cmp = &DkUtils::compDateModified;
        break;
    case DkSettings::sort_random:
        cmp = &DkUtils::compRandom;
        break;
    default:
        qWarning() << "[compareFunc] bogus sort mode ignored" << mode;
        cmp = &DkUtils::compFilename;
    }

    return [cmp](const QSharedPointer<DkImageContainer> &lhs, const QSharedPointer<DkImageContainer> &rhs) {
        return cmp(lhs->fileInfo(), rhs->fileInfo());
    };
}

QImage DkImageContainer::image()
{
    if (getLoader()->image().isNull() && getLoadState() == not_loaded)
        loadImage();

    return mLoader->pixmap(); // current pixmap (rotated pixmap after exif rotation)
}

QImage DkImageContainer::pixmap()
{
    return mLoader->pixmap();
}

QImage DkImageContainer::imageScaledToHeight(int height)
{
    // check cash first
    for (const QImage &img : scaledImages) {
        if (img.height() == height)
            return img;
    }

    // cache it
    QImage sImg = image().scaledToHeight(height, Qt::SmoothTransformation);
    scaledImages << sImg;

    // clean up
    if (scaledImages.size() > 10)
        scaledImages.pop_front();

    return sImg;
}

void DkImageContainer::setImage(const QImage &img, const QString &editName)
{
    scaledImages.clear();
    getLoader()->setEditImage(img, editName);
    mEdited = true;
}

void DkImageContainer::setImage(const QImage &img, const QString &editName, const QString &filePath)
{
    scaledImages.clear(); // invalid now

    setFilePath(mFilePath);
    getLoader()->setImage(img, editName, filePath); // set new image
    mEdited = true;
}

void DkImageContainer::setMetaData(QSharedPointer<DkMetaDataT> editedMetaData, const QImage &img, const QString &editName)
{
    // Add edit history entry with explicitly edited metadata (hasMetaData()) and implicitly modified image
    // how about a signal?

    getLoader()->setEditMetaData(editedMetaData, img, editName);
    mEdited = true;
}

void DkImageContainer::setMetaData(QSharedPointer<DkMetaDataT> editedMetaData, const QString &editName)
{
    // Add edit history entry with explicitly edited metadata (hasMetaData()) and implicitly modified image
    // how about a signal?

    getLoader()->setEditMetaData(editedMetaData, editName);
    mEdited = true;
}

void DkImageContainer::setMetaData(const QString &editName)
{
    // Add edit history entry with explicitly edited metadata (hasMetaData()) and implicitly modified image

    getLoader()->setEditMetaData(editName);
    mEdited = true;
}

void DkImageContainer::setFilePath(const QString &filePath)
{
    mFilePath = filePath;
    mFileInfo = QFileInfo(filePath);

#ifdef Q_OS_WIN
    mFileNameStr = DkUtils::qStringToStdWString(fileName());
#endif
}

bool DkImageContainer::hasImage() const
{
    if (!mLoader)
        return false;

    return mLoader->hasImage();
}

bool DkImageContainer::hasMovie() const
{
    QString newSuffix = QFileInfo(filePath()).suffix();
    return newSuffix.contains(QRegularExpression("(apng|avif|gif|jxl|mng|webp)", QRegularExpression::CaseInsensitiveOption)) != 0;
}

bool DkImageContainer::hasSvg() const
{
    QString newSuffix = QFileInfo(filePath()).suffix();
    return newSuffix.contains(QRegularExpression("(svg)", QRegularExpression::CaseInsensitiveOption)) != 0;
}

int DkImageContainer::getLoadState() const
{
    return mLoadState;
}

bool DkImageContainer::loadImage()
{
    if (!QFileInfo(mFileInfo).exists())
        return false;

    if (getFileBuffer()->isEmpty())
        mFileBuffer = loadFileToBuffer(mFilePath);

    mLoader = loadImageIntern(mFilePath, getLoader(), mFileBuffer);

    return mLoader->hasImage();
}

bool DkImageContainer::saveImage(const QString &filePath, int compression /* = -1 */)
{
    return saveImage(filePath, getLoader()->lastImage(), compression);
}

bool DkImageContainer::saveImage(const QString &filePath, const QImage saveImg, int compression /* = -1 */)
{
    QFileInfo saveFile = QFileInfo(saveImageIntern(filePath, getLoader(), saveImg, compression));

    saveFile.refresh();
    qDebug() << "save file: " << saveFile.absoluteFilePath();

    return saveFile.exists() && saveFile.isFile();
}

QSharedPointer<QByteArray> DkImageContainer::loadFileToBuffer(const QString &filePath)
{
    QFileInfo fInfo = QFileInfo(filePath);

    if (fInfo.isSymLink())
        fInfo = QFileInfo(fInfo.symLinkTarget());

#ifdef WITH_QUAZIP
    if (isFromZip())
        return getZipData()->extractImage(getZipData()->getZipFilePath(), getZipData()->getImageFileName());
#endif

    if (fInfo.suffix().contains("psd")) { // for now just psd's are not cached because their file might be way larger than the part we need to read
        return QSharedPointer<QByteArray>(new QByteArray());
    }

    QFile file(fInfo.absoluteFilePath());
    file.open(QIODevice::ReadOnly);

    QSharedPointer<QByteArray> ba(new QByteArray(file.readAll()));
    file.close();

    return ba;
}

QSharedPointer<DkBasicLoader>
DkImageContainer::loadImageIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, const QSharedPointer<QByteArray> fileBuffer)
{
    try {
        loader->loadGeneral(filePath, fileBuffer, true, false);
    } catch (...) {
        qWarning() << "Unhandled exception in loadGeneral()";
    }

    return loader;
}

QString DkImageContainer::saveImageIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, QImage saveImg, int compression)
{
    return loader->save(filePath, saveImg, compression);
}

void DkImageContainer::saveMetaData()
{
    if (!mLoader)
        return;

    saveMetaDataIntern(mFilePath, mLoader, mFileBuffer);
}

void DkImageContainer::saveMetaDataIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, QSharedPointer<QByteArray> fileBuffer)
{
    // TODO this shouldn't be used without notifying the user, see issue #799
    loader->saveMetaData(filePath, fileBuffer);
}

void DkImageContainer::setEdited(bool edited /* = true */)
{
    mEdited = edited;
}

bool DkImageContainer::isEdited() const
{
    return mEdited;
}

bool DkImageContainer::isSelected() const
{
    return mSelected;
}

bool DkImageContainer::setPageIdx(int skipIdx)
{
    return getLoader()->setPageIdx(skipIdx);
}

#ifdef WITH_QUAZIP
QSharedPointer<DkZipContainer> DkImageContainer::getZipData()
{
    if (!mZipData) {
        mZipData = QSharedPointer<DkZipContainer>(new DkZipContainer(mFilePath));
        if (mZipData->isZip()) {
            setFilePath(mZipData->getImageFileName());
            // mFileInfo = mZipData->getZipFilePath();
            qDebug() << "new fileInfoPath: " << mFileInfo.absoluteFilePath();
        }
    }

    return mZipData;
}
#endif
#ifdef Q_OS_WIN
std::wstring DkImageContainer::getFileNameWStr() const
{
    return mFileNameStr;
}
#endif

QString DkImageContainer::originalFilePath() const
{
    return mOriginalFilePath;
}

// DkImageContainerT --------------------------------------------------------------------
DkImageContainerT::DkImageContainerT(const QString &filePath)
    : DkImageContainer(filePath)
{
    // our file watcher
    mFileUpdateTimer.setSingleShot(false);
    mFileUpdateTimer.setInterval(500);

    connect(&mFileUpdateTimer, &QTimer::timeout, this, &DkImageContainerT::checkForFileUpdates, Qt::UniqueConnection);
}

DkImageContainerT::~DkImageContainerT()
{
    mBufferWatcher.blockSignals(true);
    mBufferWatcher.cancel();
    mImageWatcher.blockSignals(true);
    mImageWatcher.cancel();

    // This dtor is where saveMetaData() used to be called, which called the "dangerous" overload of saveMetaData(),
    // which is dangerous because it updates the file. We consider this to be a bug.
    // The other place where the file was silently updated in the background was the release() routine, called on unload.
    // All changes should be explicitly committed, including exif notes.
    // See issue #799. [2022, PSE]

    // we have to wait here
    mSaveMetaDataWatcher.blockSignals(true);
    mSaveImageWatcher.blockSignals(true);
}

void DkImageContainerT::clear()
{
    cancel();

    if (mFetchingImage || mFetchingBuffer)
        return;

    DkImageContainer::clear();
}

void DkImageContainerT::checkForFileUpdates()
{
#ifdef WITH_QUAZIP
    if (isFromZip())
        setFilePath(getZipData()->getZipFilePath());
#endif

    QDateTime modifiedBefore = fileInfo().lastModified();
    mFileInfo.refresh();

    bool changed = false;

    // if image exists_not don't do this
    if (!mFileInfo.exists() && mLoadState == loaded) {
        changed = true;
    }

    if (mWaitForUpdate != update_loading && mFileInfo.lastModified() != modifiedBefore)
        mWaitForUpdate = update_pending;

#ifdef WITH_QUAZIP
    if (isFromZip())
        setFilePath(getZipData()->getImageFileName());
#endif

    if (changed) {
        mFileUpdateTimer.stop();
        if (DkSettingsManager::param().global().askToSaveDeletedFiles) {
            mEdited = changed;
            emit fileLoadedSignal(true);
        }
        return;
    }

    // we use our own file watcher, since the qt watcher
    // uses locks to check for updates. this might
    // be more accurate. however, the locks are pretty nasty
    // if the user e.g. wants to delete the file while watching
    // it in nomacs
    if (mWaitForUpdate == update_pending && mFileInfo.isReadable()) {
        mWaitForUpdate = update_loading;

        // do not update edited files
        if (!isEdited())
            loadImageThreaded(true);
        else
            qInfo() << "I would update now - but the image is edited...";
    }
}

bool DkImageContainerT::loadImageThreaded(bool force)
{
#ifdef WITH_QUAZIP
    // zip archives: get zip file fileInfo for checks
    if (isFromZip())
        setFilePath(getZipData()->getZipFilePath());
#endif

    // check file for updates
    QFileInfo fileInfo = QFileInfo(filePath());
    QDateTime modifiedBefore = fileInfo.lastModified();
    fileInfo.refresh();

    if (force || fileInfo.lastModified() != modifiedBefore || getLoader()->isDirty()) {
        qDebug() << "updating image...";
        clear();
    }

    // null file?
    if (fileInfo.fileName().isEmpty() || !fileInfo.exists()) {
        QString msg = tr("Sorry, the file: %1 does not exist... ").arg(fileName());
        emit showInfoSignal(msg);
        mLoadState = exists_not;
        return false;
    } else if (!fileInfo.permission(QFile::ReadUser)) {
        QString msg = tr("Sorry, you are not allowed to read: %1").arg(fileName());
        emit showInfoSignal(msg);
        mLoadState = exists_not;
        return false;
    }

#ifdef WITH_QUAZIP
    // zip archives: use the image file info from now on
    if (isFromZip())
        setFilePath(getZipData()->getImageFileName());
#endif

    mLoadState = loading;
    fetchFile();
    return true;
}

void DkImageContainerT::fetchFile()
{
    if (mFetchingBuffer && getLoadState() == loading_canceled) {
        mLoadState = loading; // uncancel loading - we had another call
        return;
    }
    if (mFetchingImage)
        mImageWatcher.waitForFinished();
    // I think we missed to return here
    if (mFetchingBuffer)
        return;

    // ignore doubled calls
    if (mFileBuffer && !mFileBuffer->isEmpty()) {
        bufferLoaded();
        return;
    }

    mFetchingBuffer = true; // saves the threaded call
    connect(&mBufferWatcher, &QFutureWatcher<QSharedPointer<QByteArray>>::finished, this, &DkImageContainerT::bufferLoaded, Qt::UniqueConnection);
    mBufferWatcher.setFuture(QtConcurrent::run([&] {
        return loadFileToBuffer(filePath());
    }));
}

void DkImageContainerT::bufferLoaded()
{
    mFetchingBuffer = false;

    if (!mBufferWatcher.isCanceled())
        mFileBuffer = mBufferWatcher.result();

    if (getLoadState() == loading)
        fetchImage();
    else if (getLoadState() == loading_canceled) {
        mLoadState = not_loaded;
        clear();
        return;
    }
}

void DkImageContainerT::fetchImage()
{
    if (mFetchingBuffer)
        mBufferWatcher.waitForFinished();

    if (mFetchingImage) {
        mLoadState = loading;
        return;
    }

    if (getLoader()->hasImage() || /*!fileBuffer || fileBuffer->isEmpty() ||*/ mLoadState == exists_not) {
        loadingFinished();
        return;
    }

    qInfoClean() << "loading " << filePath();
    mFetchingImage = true;

    connect(&mImageWatcher, &QFutureWatcher<QSharedPointer<DkBasicLoader>>::finished, this, &DkImageContainerT::imageLoaded, Qt::UniqueConnection);

    mImageWatcher.setFuture(QtConcurrent::run([&] {
        return loadImageIntern(filePath(), mLoader, mFileBuffer);
    }));
}

void DkImageContainerT::imageLoaded()
{
    mFetchingImage = false;

    if (getLoadState() == loading_canceled) {
        mLoadState = not_loaded;
        clear();
        return;
    }

    // deliver image
    mLoader = mImageWatcher.result();

    loadingFinished();
}

void DkImageContainerT::loadingFinished()
{
    DkTimer dt;

    if (getLoadState() == loading_canceled) {
        mLoadState = not_loaded;
        clear();
        return;
    }

    // fix the update states
    if (mWaitForUpdate != update_idle) {
        if (!getLoader()->hasImage()) {
            mWaitForUpdate = update_pending;
            mLoadState = not_loaded;
            qInfo() << "could not load while updating - is somebody writing to the file?";
            return;
        } else {
            emit showInfoSignal(tr("updated..."));
            mWaitForUpdate = update_idle;
        }
    }

    if (!getLoader()->hasImage()) {
        mFileUpdateTimer.stop();
        mEdited = false;
        QString msg = tr("Sorry, I could not load: %1").arg(fileName());
        emit showInfoSignal(msg);
        emit fileLoadedSignal(false);
        mLoadState = exists_not;
        return;
    }

    // clear file buffer if it exceeds a certain size?! e.g. psd files
    if (mFileBuffer) {
        double bs = mFileBuffer->size() / (1024.0f * 1024.0f);

        // if the file buffer is more than 5MB - we check if we need to delete it
        if (bs > 5 && bs > DkSettingsManager::param().resources().cacheMemory * 0.5f)
            mFileBuffer->clear();
    }

    mLoadState = loaded;
    emit fileLoadedSignal(true);
}

void DkImageContainerT::downloadFile(const QUrl &url)
{
    if (!mFileDownloader) {
        QString savePath = DkSettingsManager::param().global().tmpPath;

        if (!QFileInfo(savePath).exists())
            savePath = QDir::tempPath() + "/nomacs";

        QFileInfo saveFile(savePath, DkUtils::nowString() + " " + DkUtils::fileNameFromUrl(url));

        mFileDownloader = QSharedPointer<FileDownloader>(new FileDownloader(url, saveFile.absoluteFilePath(), this));
        connect(mFileDownloader.data(), &FileDownloader::downloaded, this, &DkImageContainerT::fileDownloaded, Qt::UniqueConnection);
        qDebug() << "trying to download: " << url;
    } else
        mFileDownloader->downloadFile(url);
}

void DkImageContainerT::fileDownloaded(const QString &filePath)
{
    if (!mFileDownloader) {
        qDebug() << "empty fileDownloader, where it should not be";
        emit fileLoadedSignal(false);
        return;
    }

    mFileBuffer = mFileDownloader->downloadedData();

    if (!mFileBuffer || mFileBuffer->isEmpty()) {
        qDebug() << mFileDownloader->getUrl() << " not downloaded...";
        mEdited = false;
        emit showInfoSignal(tr("Sorry, I could not download:\n%1").arg(mFileDownloader->getUrl().toString()));
        emit fileLoadedSignal(false);
        mLoadState = exists_not;
        return;
    }

    mDownloaded = true;

    if (filePath.isEmpty())
        setFilePath(mFileDownloader->getUrl().toString().split("/").last());
    else
        setFilePath(filePath);

    fetchImage();
}

void DkImageContainerT::cancel()
{
    if (mLoadState != loading)
        return;

    mLoadState = loading_canceled;
}

void DkImageContainerT::receiveUpdates(bool connectSignals)
{
    // !selected - do not connect twice
    if (connectSignals && !mSelected) {
        mFileUpdateTimer.start();
    } else if (!connectSignals) {
        mFileUpdateTimer.stop();
    }

    mSelected = connectSignals;
}

void DkImageContainerT::saveMetaDataThreaded(const QString &filePath)
{
    if (!exists() || (getLoader()->getMetaData() && !getLoader()->getMetaData()->isDirty()))
        return;

    mFileUpdateTimer.stop();
    QFuture<void> future = QtConcurrent::run([&, filePath] {
        return saveMetaDataIntern(filePath, getLoader(), getFileBuffer());
    });
}

void DkImageContainerT::saveMetaDataThreaded()
{
    saveMetaDataThreaded(filePath());
}

bool DkImageContainerT::saveImageThreaded(const QString &filePath, int compression /* = -1 */)
{
    return saveImageThreaded(filePath, getLoader()->lastImage(), compression);
}

bool DkImageContainerT::saveImageThreaded(const QString &filePath, const QImage saveImg, int compression /* = -1 */)
{
    mSaveImageWatcher.waitForFinished();

    QFileInfo fInfo = QFileInfo(filePath);

    if (saveImg.isNull()) {
        QString msg = tr("I can't save an empty file, sorry...\n");
        emit errorDialogSignal(msg);
        return false;
    }
    if (!fInfo.absoluteDir().exists()) {
        QString msg = tr("Sorry, the directory: %1  does not exist\n").arg(filePath);
        emit errorDialogSignal(msg);
        return false;
    }
    if (fInfo.exists() && !fInfo.isWritable()) {
        QString msg = tr("Sorry, I can't write to the file: %1").arg(fInfo.fileName());
        emit errorDialogSignal(msg);
        return false;
    }

    qDebug() << "attempting to save: " << filePath;

    mFileUpdateTimer.stop();
    connect(&mSaveImageWatcher, &QFutureWatcher<QString>::finished, this, &DkImageContainerT::savingFinished, Qt::UniqueConnection);

    mSaveImageWatcher.setFuture(QtConcurrent::run([&, filePath, saveImg, compression] {
        return saveImageIntern(filePath, mLoader, saveImg, compression);
    }));

    return true;
}

void DkImageContainerT::savingFinished()
{
    QString savePath = mSaveImageWatcher.result();

    QFileInfo sInfo = QFileInfo(savePath);
    sInfo.refresh();
    qDebug() << "save file: " << savePath;

    if (!sInfo.exists() || !sInfo.isFile())
        emit fileSavedSignal(savePath, false);
    else {
        //// reset thumb - loadImageThreaded should do it anyway
        // thumb = QSharedPointer<DkThumbNailT>(new DkThumbNailT(saveFile, loader->image()));

        if (mFileBuffer)
            mFileBuffer->clear(); // do a complete clear?

        if (DkSettingsManager::param().resources().loadSavedImage == DkSettings::ls_load || filePath().isEmpty() || dirPath() == sInfo.absolutePath()) {
            setFilePath(savePath);

            emit fileSavedSignal(savePath, true, false);
        } else {
            emit fileSavedSignal(savePath);
        }

        mEdited = false;
        mDownloaded = false;
        if (mSelected) {
            loadImageThreaded(true); // force a reload
            mFileUpdateTimer.start();
        }
    }
}

QSharedPointer<QByteArray> DkImageContainerT::loadFileToBuffer(const QString &filePath)
{
    return DkImageContainer::loadFileToBuffer(filePath);
}

QSharedPointer<DkBasicLoader>
DkImageContainerT::loadImageIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, const QSharedPointer<QByteArray> fileBuffer)
{
    return DkImageContainer::loadImageIntern(filePath, loader, fileBuffer);
}

QString DkImageContainerT::saveImageIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, QImage saveImg, int compression)
{
    return DkImageContainer::saveImageIntern(filePath, loader, saveImg, compression);
}

void DkImageContainerT::saveMetaDataIntern(const QString &filePath, QSharedPointer<DkBasicLoader> loader, QSharedPointer<QByteArray> fileBuffer)
{
    return DkImageContainer::saveMetaDataIntern(filePath, loader, fileBuffer);
}

QSharedPointer<DkBasicLoader> DkImageContainerT::getLoader()
{
    if (!mLoader) {
        DkImageContainer::getLoader();
        connect(mLoader.data(), &DkBasicLoader::errorDialogSignal, this, &DkImageContainerT::errorDialogSignal);
    }

    return mLoader;
}

bool DkImageContainerT::isFileDownloaded() const
{
    return mDownloaded;
}

void DkImageContainerT::undo()
{
    DkImageContainer::undo();
    emit imageUpdatedSignal();
}

void DkImageContainerT::redo()
{
    DkImageContainer::redo();
    emit imageUpdatedSignal();
}

void DkImageContainerT::setHistoryIndex(int idx)
{
    DkImageContainer::setHistoryIndex(idx);
    emit imageUpdatedSignal();
}

}

/***************************************************************************
 *   Copyright Simon Persson                                               *
 *   simonpersson1@gmail.com                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "edexecutor.h"
#include "backupplan.h"
#include "bupjob.h"

#include <kio/directorysizejob.h>
#include <KDiskFreeSpaceInfo>
#include <KLocale>
#include <KNotification>

#include <Solid/DeviceNotifier>
#include <Solid/DeviceInterface>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QTimer>

EDExecutor::EDExecutor(BackupPlan *pPlan, QObject *pParent)
   :PlanExecutor(pPlan, pParent), mStorageAccess(NULL), mWantsToRunBackup(false), mWantsToShowFiles(false)
{
	connect(Solid::DeviceNotifier::instance(), SIGNAL(deviceAdded(QString)), SLOT(deviceAdded(QString)));
	connect(Solid::DeviceNotifier::instance(), SIGNAL(deviceRemoved(QString)), SLOT(deviceRemoved(QString)));
}

void EDExecutor::checkStatus() {
	QList<Solid::Device> lDeviceList = Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume);
	foreach (const Solid::Device &lDevice, lDeviceList) {
		deviceAdded(lDevice.udi());
	}
	updateAccessibility();
}

void EDExecutor::deviceAdded(const QString &pUdi) {
	Solid::Device lDevice(pUdi);
	if(!lDevice.isValid() || !lDevice.is<Solid::StorageVolume>()) {
		return;
	}
	Solid::StorageVolume *lVolume = lDevice.as<Solid::StorageVolume>();
	if(mPlan->mExternalUUID == lVolume->uuid())
	{
		mCurrentUdi = pUdi;
		mStorageAccess = lDevice.as<Solid::StorageAccess>();
		enterAvailableState();
	}
}

void EDExecutor::deviceRemoved(const QString &pUdi) {
	if(mCurrentUdi == pUdi) {
		mWantsToRunBackup = false;
		mCurrentUdi.clear();
		mStorageAccess = NULL;
		enterNotAvailableState();
	}
}

void EDExecutor::updateAccessibility() {
	if(mWantsToRunBackup) {
		startBackup(); // run startBackup again now that it has been mounted
	} else if(mWantsToShowFiles) {
		showFilesClicked();
	}
}

void EDExecutor::startBackup() {
	if(!mStorageAccess) {
		exitBackupRunningState(false);
		return;
	}
	if(mStorageAccess->isAccessible()) {
		if(!mStorageAccess->filePath().isEmpty()) {
			mDestinationPath = QDir::cleanPath(mStorageAccess->filePath() + '/' + mPlan->mExternalDestinationPath);
			QDir lDir(mDestinationPath);
			if(!lDir.exists()) {
				lDir.mkdir(mDestinationPath);
			}
			QFileInfo lInfo(mDestinationPath);
			if(lInfo.isWritable()) {
				BupJob *lJob = new BupJob(mPlan, mDestinationPath, this);
				connect(lJob, SIGNAL(result(KJob*)), SLOT(slotBackupDone(KJob*)));
				lJob->start();
				mWantsToRunBackup = false; //reset, only used to retrigger this state-entering if drive wasn't already mounted
			}
		}
	} else { //not mounted yet. trigger mount and come back to this startBackup again later
		mWantsToRunBackup = true;
		connect(mStorageAccess, SIGNAL(accessibilityChanged(bool,QString)), SLOT(updateAccessibility()));
		mStorageAccess->setup(); //try to mount it, fail silently for now.
	}
}

void EDExecutor::slotBackupDone(KJob *pJob) {
	if(pJob->error()) {
		KNotification::event(KNotification::Error, i18n("Problem"), pJob->errorText());
		exitBackupRunningState(false);
	} else {
		mPlan->mLastCompleteBackup = QDateTime::currentDateTimeUtc();
		KDiskFreeSpaceInfo lSpaceInfo = KDiskFreeSpaceInfo::freeSpaceInfo(mDestinationPath);
		if(lSpaceInfo.isValid())
			mPlan->mLastAvailableSpace = (double)lSpaceInfo.available();
		else
			mPlan->mLastAvailableSpace = -1.0; //unknown size

		KIO::DirectorySizeJob *lSizeJob = KIO::directorySize(mDestinationPath);
		connect(lSizeJob, SIGNAL(result(KJob*)), SLOT(slotBackupSizeDone(KJob*)));
		lSizeJob->start();
	}
}

void EDExecutor::slotBackupSizeDone(KJob *pJob) {
	if(pJob->error()) {
		KNotification::event(KNotification::Error, i18n("Problem"), pJob->errorText());
		mPlan->mLastBackupSize = -1.0; //unknown size
	} else {
		KIO::DirectorySizeJob *lSizeJob = qobject_cast<KIO::DirectorySizeJob *>(pJob);
		mPlan->mLastBackupSize = (double)lSizeJob->totalSize();
	}
	mPlan->writeConfig();
	exitBackupRunningState(pJob->error() == 0);
}

void EDExecutor::showFilesClicked() {
	if(!mStorageAccess)
		return;

	if(mStorageAccess->isAccessible()) {
		if(!mStorageAccess->filePath().isEmpty()) {
			mDestinationPath = QDir::cleanPath(mStorageAccess->filePath() + '/' + mPlan->mExternalDestinationPath);
			QDir lDir(mDestinationPath);
			if(lDir.exists()) {
				mWantsToShowFiles = false; //reset, only used to retrigger this state-entering if drive wasn't already mounted
				if(mBupFuseProcess) {
					unmountBupFuse();
				} else {
					mountBupFuse();
				}
			}
		}
	} else { //not mounted yet. trigger mount and come back to this startBackup again later
		mWantsToShowFiles = true;
		connect(mStorageAccess, SIGNAL(accessibilityChanged(bool,QString)), SLOT(updateAccessibility()));
		mStorageAccess->setup(); //try to mount it, fail silently for now.
	}
}
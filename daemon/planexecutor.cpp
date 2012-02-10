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

#include "planexecutor.h"

#include <KLocale>
#include <KNotification>
#include <KRun>
#include <KStandardDirs>
#include <KTempDir>

#include <QAction>
#include <QDir>
#include <QMenu>

PlanExecutor::PlanExecutor(BackupPlan *pPlan, QObject *pParent)
   :QObject(pParent), mState(NOT_AVAILABLE), mPlan(pPlan), mBupFuseProcess(NULL), mQuestion(NULL), mOkToShowBackup(false)
{
	mShowFilesAction = new QAction(i18n("Show Files"), this);
	mShowFilesAction->setCheckable(true);
	mShowFilesAction->setEnabled(false);
	connect(mShowFilesAction, SIGNAL(triggered()), SLOT(showFilesClicked()));

	mRunBackupAction = new QAction(i18n("Take Backup Now"), this);
	mRunBackupAction->setEnabled(false);
	connect(mRunBackupAction, SIGNAL(triggered()), SLOT(enterBackupRunningState()));

	mActionMenu = new QMenu(mPlan->mDescription);
	mActionMenu->setEnabled(false);
	mActionMenu->addAction(mRunBackupAction);
	mActionMenu->addAction(mShowFilesAction);

	mSchedulingTimer = new QTimer(this);
	mSchedulingTimer->setSingleShot(true);
	connect(mSchedulingTimer, SIGNAL(timeout()), SLOT(enterAvailableState()));
}

PlanExecutor::~PlanExecutor() {
}

// dispatcher code for entering one of the available states
void PlanExecutor::enterAvailableState() {
	if(mState == NOT_AVAILABLE) {
		mShowFilesAction->setEnabled(true);
		mRunBackupAction->setEnabled(true);
		mActionMenu->setEnabled(true);
		mState = WAITING_FOR_FIRST_BACKUP; //initial child state of "Available" state
		emit stateChanged();
	}

	bool lShouldBeTakenNow = false;
	bool lShouldBeTakenLater = false;
	int lTimeUntilNextWakeup;
	QString lUserQuestion;
	QDateTime lNow = QDateTime::currentDateTimeUtc();

	switch(mPlan->mScheduleType) {
	case BackupPlan::MANUAL:
		break;
	case BackupPlan::INTERVAL: {
		QDateTime lNextTime = mPlan->nextScheduledTime();
		if(!lNextTime.isValid() || lNextTime < lNow) {
			lShouldBeTakenNow = true;
			if(!mPlan->mLastCompleteBackup.isValid())
				lUserQuestion = i18n("Do you want to take a first backup now?");
			else {
				QString t = KGlobal::locale()->prettyFormatDuration(mPlan->mLastCompleteBackup.secsTo(lNow) * 1000);
				lUserQuestion = i18n("It's been %1 since the last backup was taken, "
				                     "do you want to take a backup now?", t);
			}
		} else {
			lShouldBeTakenLater = true;
			lTimeUntilNextWakeup = lNow.msecsTo(lNextTime);
		}
		break;
	}
	case BackupPlan::USAGE:
		if(!mPlan->mLastCompleteBackup.isValid()) {
			lShouldBeTakenNow = true;
			lUserQuestion = i18n("Do you want to take a first backup now?");
		} else if(mPlan->mAccumulatedUsageTime > (quint32)mPlan->mUsageLimit * 3600) {
			lShouldBeTakenNow = true;
			QString t = KGlobal::locale()->prettyFormatDuration(mPlan->mAccumulatedUsageTime * 1000);
			lUserQuestion = i18n("You've been logged in for %1 since the last backup was taken, "
			                     "do you want to take a backup now?", t);
		}
		break;
	}

	if(lShouldBeTakenNow) {
		// only ask the first time after destination has become available
		if(mPlan->mAskBeforeTakingBackup && mState == WAITING_FOR_FIRST_BACKUP) {
			askUser(lUserQuestion);
		} else {
			enterBackupRunningState();
		}
	} else if(lShouldBeTakenLater){
		// schedule a wakeup for asking again when the time is right.
		mSchedulingTimer->start(lTimeUntilNextWakeup);
	}
}

void PlanExecutor::enterNotAvailableState() {
	mSchedulingTimer->stop();
	mShowFilesAction->setEnabled(false);
	mRunBackupAction->setEnabled(false);
	mActionMenu->setEnabled(false);
	mState = NOT_AVAILABLE;
	emit stateChanged();
}

void PlanExecutor::askUser(const QString &pQuestion) {
	mQuestion = new KNotification(QLatin1String("StartBackup"), KNotification::Persistent);
	mQuestion->setTitle(i18n("Backup Device Available - %1", mPlan->mDescription));
	mQuestion->setText(pQuestion);
	QStringList lAnswers;
	lAnswers << i18n("Yes") <<i18n("No");
	mQuestion->setActions(lAnswers);
	connect(mQuestion, SIGNAL(action1Activated()), SLOT(enterBackupRunningState()));
	connect(mQuestion, SIGNAL(action2Activated()), SLOT(discardUserQuestion()));
	connect(mQuestion, SIGNAL(closed()), SLOT(discardUserQuestion()));
	connect(mQuestion, SIGNAL(ignored()), SLOT(discardUserQuestion()));
	// enter this "do nothing" state, if user answers "no" or ignores, remain there
	mState = WAITING_FOR_MANUAL_BACKUP;
	emit stateChanged();
	mQuestion->sendEvent();
}

void PlanExecutor::discardUserQuestion() {
	if(mQuestion) {
		mQuestion->deleteLater();
		mQuestion = NULL;
	}
}

void PlanExecutor::enterBackupRunningState() {
	if(mQuestion) {
		mQuestion->deleteLater();
		mQuestion = NULL;
	}
	mState = RUNNING;
	emit stateChanged();
	mRunBackupAction->setEnabled(false);
	startBackup();
}

void PlanExecutor::exitBackupRunningState(bool pWasSuccessful) {
	mRunBackupAction->setEnabled(true);
	if(pWasSuccessful) {
		if(mPlan->mScheduleType == BackupPlan::USAGE) {
			//reset usage time after successful backup
			mPlan->mAccumulatedUsageTime =0;
			mPlan->writeConfig();
		}
		mState = WAITING_FOR_BACKUP_AGAIN;
		emit stateChanged();

		//don't know if status actually changed, potentially did... so trigger a re-read of status
		emit backupStatusChanged();

		// re-enter the main "available" state dispatcher
		enterAvailableState();
	} else {
		mState = WAITING_FOR_MANUAL_BACKUP;
		emit stateChanged();
	}
}

void PlanExecutor::updateAccumulatedUsageTime() {
	if(mState == RUNNING) { //usage time during backup doesn't count...
		return;
	}

	if(mPlan->mScheduleType == BackupPlan::USAGE) {
		mPlan->mAccumulatedUsageTime += KUP_USAGE_MONITOR_INTERVAL_S;
		mPlan->writeConfig();
	}

	// trigger refresh of backup status, potentially changed...
	emit backupStatusChanged();

	//if we're waiting to run backup again, check if it is time now.
	if(mPlan->mScheduleType == BackupPlan::USAGE &&
	      (mState == WAITING_FOR_FIRST_BACKUP || mState == WAITING_FOR_BACKUP_AGAIN)) {
		enterAvailableState();
	}
}

void PlanExecutor::showFilesClicked() {
	if(mState == NOT_AVAILABLE)
		return;
	if(mBupFuseProcess) {
		unmountBupFuse();
	} else {
		mountBupFuse();
	}
}

void PlanExecutor::mountBupFuse() {
	mTempDir = KStandardDirs::locateLocal("tmp", mPlan->mDescription + "/");
	mBupFuseProcess = new KProcess(this);
	mBupFuseProcess->setOutputChannelMode(KProcess::OnlyStderrChannel);

	*mBupFuseProcess << QLatin1String("bup");
	*mBupFuseProcess << QLatin1String("-d") << mDestinationPath;
	*mBupFuseProcess << QLatin1String("fuse") << QLatin1String("-f");
	*mBupFuseProcess << mTempDir;

	connect(mBupFuseProcess, SIGNAL(finished(int,QProcess::ExitStatus)), SLOT(bupFuseFinished(int,QProcess::ExitStatus)));
	mBupFuseProcess->start();

	mOkToShowBackup = true;
	QTimer::singleShot(1000, this, SLOT(showMountedBackup()));
}

void PlanExecutor::unmountBupFuse() {
	KProcess lUnmount;
	lUnmount.setOutputChannelMode(KProcess::OnlyStderrChannel);
	lUnmount << QLatin1String("fusermount") << QLatin1String("-u") <<mTempDir;
	if(lUnmount.execute()) {
		KNotification::event(KNotification::Error, i18n("Error when trying to unmount backup"), lUnmount.readAllStandardError());
		mShowFilesAction->setChecked(true);
	}
}

void PlanExecutor::bupFuseFinished(int pExitCode, QProcess::ExitStatus pExitStatus) {
	if(pExitStatus != QProcess::NormalExit || pExitCode != 0) {
		KNotification::event(KNotification::Error, i18n("Error when trying to mount backup"), mBupFuseProcess->readAllStandardError());
	}

	mShowFilesAction->setChecked(false);
	mBupFuseProcess->deleteLater();
	mBupFuseProcess = NULL;
	QDir lDir;
	lDir.rmpath(mTempDir);
	mOkToShowBackup = false;
}

void PlanExecutor::showMountedBackup() {
	if(mOkToShowBackup) {
		KRun::runUrl(KUrl(mTempDir + "kup"), QLatin1String("inode/directory"), 0);
		mShowFilesAction->setChecked(true);
	}
}
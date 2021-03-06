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

#include "planstatuswidget.h"
#include "kupsettings.h"
#include "backupplan.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <KLocalizedString>

PlanStatusWidget::PlanStatusWidget(BackupPlan *pPlan, QWidget *pParent)
    : QGroupBox(pParent), mPlan(pPlan)
{
	auto *lVLayout1 = new QVBoxLayout;
	auto *lVLayout2 = new QVBoxLayout;
	auto *lHLayout1 = new QHBoxLayout;
	auto *lHLayout2 = new QHBoxLayout;

	mDescriptionLabel = new QLabel(mPlan->mDescription);
	QFont lDescriptionFont = mDescriptionLabel->font();
	lDescriptionFont.setPointSizeF(lDescriptionFont.pointSizeF() + 2.0);
	lDescriptionFont.setBold(true);
	mDescriptionLabel->setFont(lDescriptionFont);
	mStatusIconLabel = new QLabel();
	//TODO: add dbus interface to be notified from daemon when this is updated.
	mStatusTextLabel = new QLabel(mPlan->statusText());
	QPushButton *lConfigureButton = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")),
	                                                xi18nc("@action:button", "Configure"));
	connect(lConfigureButton, SIGNAL(clicked()), this, SIGNAL(configureMe()));
	QPushButton *lRemoveButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
	                                             xi18nc("@action:button", "Remove"));
	connect(lRemoveButton, SIGNAL(clicked()), this, SIGNAL(removeMe()));
	QPushButton *lCopyButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-duplicate")),
	                                           xi18nc("@action:button", "Duplicate"));
	connect(lCopyButton, &QPushButton::clicked, this, &PlanStatusWidget::duplicateMe);

	lVLayout2->addWidget(mDescriptionLabel);
	lVLayout2->addWidget(mStatusTextLabel);
	lHLayout1->addLayout(lVLayout2);
	lHLayout1->addStretch();
	lHLayout1->addWidget(mStatusIconLabel);
	lVLayout1->addLayout(lHLayout1);
	lHLayout2->addStretch();
	lHLayout2->addWidget(lCopyButton);
	lHLayout2->addWidget(lConfigureButton);
	lHLayout2->addWidget(lRemoveButton);
	lVLayout1->addLayout(lHLayout2);
	setLayout(lVLayout1);

	updateIcon();
}

void PlanStatusWidget::updateIcon() {
	mStatusIconLabel->setPixmap(QIcon::fromTheme(mPlan->iconName(mPlan->backupStatus())).pixmap(64,64));
}


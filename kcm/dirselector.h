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

#ifndef DIRSELECTOR_H
#define DIRSELECTOR_H

#include <QTreeView>

class KDirModel;

class DirSelector : public QTreeView
{
	Q_OBJECT
public:
	explicit DirSelector(QWidget *pParent = nullptr);
	QUrl url() const;

signals:

public slots:
	void createNewFolder();
	void selectEntry(QModelIndex pIndex);
	void expandToUrl(const QUrl &pUrl);
	void setRootUrl(const QUrl &pUrl);
private:
	KDirModel *mDirModel;
};

#endif // DIRSELECTOR_H

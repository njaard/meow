#include "mainwindow-qt.h"
#include "treeview.h"
#include "player.h"
#include "scrobble.h"
#include "directoryadder.h"
#include "filter.h"

#include <db/file.h>
#include <db/base.h>
#include <db/collection.h>

#include <qpixmap.h>
#include <qicon.h>
#include <qevent.h>
#include <qmenu.h>
#include <qmenubar.h>
#include <qtoolbar.h>
#include <qslider.h>
#include <qsignalmapper.h>
#include <qtoolbutton.h>
#include <qapplication.h>
#include <qfiledialog.h>
#include <qsettings.h>
#include <qurl.h>
#include <qmessagebox.h>
#include <qabstracteventdispatcher.h>
#include <qpainter.h>
#include <qboxlayout.h>

#include <map>
#include <iostream>

#ifdef _WIN32
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <winuser.h>
#endif

#define i18n tr
class SpecialSlider;

struct Meow::MainWindow::MainWindowPrivate
{
	inline MainWindowPrivate() : selectorActions(0) { }
	TreeView *view;
	Player *player;
	QSystemTrayIcon *tray;
	Base db;
	Collection *collection;
	DirectoryAdder *adder;
	
	QAction *itemProperties, *itemRemove;
	QMenu *playbackOrder;
	QToolBar *topToolbar;
	
	QAction *collectionsAction;
	QActionGroup *collectionsActionGroup;
	
	QAction *playPauseAction, *prevAction, *nextAction, *volumeUpAction, *volumeDownAction, *muteAction, *volumeAction;
	
	QAction *toggleToolbarAction, *toggleMenubarAction;
	
	bool nowFiltering, quitting;
	
	ConfigDialog *settingsDialog;
	Scrobble *scrobble;

	QFileDialog *openFileDialog;
	
	QMenu *contextMenu;
	QActionGroup selectorActions;
	std::map<QAction*, TreeView::SelectorType> selectors;
	
	SpecialSlider *volumeSlider;
	Filter *filter;
};

typedef QIcon KIcon;

#include "mainwindow_common.cpp"


#ifdef _WIN32
static Meow::MainWindow *mwEvents=0;

bool Meow::MainWindow::globalEventFilter(void *_m)
{
	MSG *const m = (MSG*)_m;
	if (m->message == WM_HOTKEY)
	{
		const quint32 keycode = HIWORD(m->lParam);
		if (keycode == VK_MEDIA_PLAY_PAUSE || keycode == VK_MEDIA_STOP)
			mwEvents->d->playPauseAction->trigger();
		else if (keycode == VK_MEDIA_NEXT_TRACK)
			mwEvents->d->nextAction->trigger();
		else if (keycode == VK_MEDIA_PREV_TRACK)
			mwEvents->d->prevAction->trigger();
		else if (keycode == VK_VOLUME_UP)
			mwEvents->d->volumeUpAction->trigger();
		else if (keycode == VK_VOLUME_DOWN)
			mwEvents->d->volumeDownAction->trigger();
		else if (keycode == VK_VOLUME_MUTE)
			mwEvents->d->muteAction->trigger();
	}
	return false;
}

#endif

Meow::MainWindow::MainWindow()
{
#ifdef _WIN32
	mwEvents = this;
	{
		RegisterHotKey(winId(), VK_MEDIA_PLAY_PAUSE, 0, VK_MEDIA_PLAY_PAUSE);
		RegisterHotKey(winId(), VK_MEDIA_STOP, 0, VK_MEDIA_STOP);
		RegisterHotKey(winId(), VK_MEDIA_NEXT_TRACK, 0, VK_MEDIA_NEXT_TRACK);
		RegisterHotKey(winId(), VK_MEDIA_PREV_TRACK, 0, VK_MEDIA_PREV_TRACK);
		RegisterHotKey(winId(), VK_VOLUME_UP, 0, VK_VOLUME_UP);
		RegisterHotKey(winId(), VK_VOLUME_DOWN, 0, VK_VOLUME_DOWN);
		RegisterHotKey(winId(), VK_VOLUME_MUTE, 0, VK_VOLUME_MUTE);
		QAbstractEventDispatcher::instance()->setEventFilter(globalEventFilter);
	}
#endif
	setWindowTitle(tr("Meow"));
	d = new MainWindowPrivate;
	d->adder = 0;
	d->nowFiltering = false;
	d->openFileDialog = 0;
	d->quitting=false;
	d->settingsDialog=0;

	d->collection = new Collection(&d->db);

	QWidget *owner = new QWidget(this);
	QVBoxLayout *ownerLayout = new QVBoxLayout(owner);
	ownerLayout->setContentsMargins(0, 0, 0, 0);
	ownerLayout->setSpacing(0);

	d->player = new Player;
	d->view = new TreeView(owner, d->player, d->collection);
	d->view->installEventFilter(this);
	ownerLayout->addWidget(d->view);
	
	d->filter = new Filter(owner);
	d->filter->hide();
	connect(d->filter, SIGNAL(textChanged(QString)), d->view, SLOT(filter(QString)));
	connect(d->filter, SIGNAL(done()), d->view, SLOT(stopFilter()));
	ownerLayout->addWidget(d->filter);
	
	d->scrobble = new Scrobble(this, d->player, d->collection);
	setCentralWidget(owner);
	
	QMenu *const trayMenu = new QMenu(this);
	d->tray = new QSystemTrayIcon(QIcon(":/meow.png"), this);
	d->tray->setContextMenu(trayMenu);
	d->tray->installEventFilter(this);
	d->tray->show();
	connect(
			d->tray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
			SLOT(systemTrayClicked(QSystemTrayIcon::ActivationReason))
		);

	QMenuBar *mbar = menuBar();
	
	QMenu *fileMenu = mbar->addMenu(tr("File"));
	QMenu *settingsMenu = mbar->addMenu(tr("Settings"));
	QMenu *helpMenu = mbar->addMenu(tr("Help"));
	
	QToolBar *topToolbar = d->topToolbar = addToolBar(tr("Main"));
		
	{
		QAction *ac;
		
		ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), SLOT(addFiles()));
		ac->setText(tr("Add &Files..."));
		ac->setIcon(QIcon(":/list-add.png"));
		topToolbar->addAction(ac);
		fileMenu->addAction(ac);
		
		ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->filter, SLOT(show()));
		ac->setText(tr("&Find"));
		{
			QList<QKeySequence> shortcuts;
			shortcuts.append(QKeySequence("/"));
			shortcuts.append(QKeySequence("Ctrl+F"));
			ac->setShortcuts(shortcuts);
		}
		fileMenu->addAction(ac);

		d->prevAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->view, SLOT(previousSong()));
		ac->setText(tr("Previous Song"));
		ac->setIcon(QIcon(":/media-skip-backward.png"));
		topToolbar->addAction(ac);
		trayMenu->addAction(ac);
		
		d->playPauseAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->player, SLOT(playpause()));
		ac->setText(tr("Paws"));
		ac->setIcon(QIcon(":/media-playback-pause.png"));
		topToolbar->addAction(ac);
		trayMenu->addAction(ac);
		
		d->nextAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->view, SLOT(nextSong()));
		ac->setText(tr("Next Song"));
		ac->setIcon(QIcon(":/media-skip-forward.png"));
		topToolbar->addAction(ac);
		
		d->volumeAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), this, SLOT(showVolume()));
		ac->setText(tr("Volume"));
		ac->setIcon(QIcon(":/player-volume.png"));
		topToolbar->addAction(ac);
		
		trayMenu->addAction(d->nextAction);
		trayMenu->addAction(d->prevAction);

		{
			struct Selector
			{
				QString name;
				TreeView::SelectorType selectorType;
			};
			
			const Selector selectors[] = 
			{
				{ tr("Each File"), TreeView::Linear },
				{ tr("Random Song"), TreeView::RandomSong },
				{ tr("Random Album"), TreeView::RandomAlbum },
				{ tr("Random Artist"), TreeView::RandomArtist }
			};
			
			d->playbackOrder = fileMenu->addMenu(tr("Playback Order"));
			
			for (int i=0; i < sizeof(selectors)/sizeof(Selector); i++)
			{
				ac = d->playbackOrder->addAction(selectors[i].name);
				d->selectors[ac] = selectors[i].selectorType;
				d->selectorActions.addAction(ac);
			}
		
			connect(&d->selectorActions, SIGNAL(triggered(QAction*)), SLOT(selectorActivated(QAction*)));
		}
		
		{
			d->collectionsActionGroup = new QActionGroup(this);
			
			d->collectionsAction = new QAction(i18n("&Collection"), this);
			d->collectionsAction->setMenu(new QMenu(this));
			QAction *newCol = new QAction(i18n("&New Collection"), this);
			connect(newCol, SIGNAL(activated()), this, SLOT(newCollection()));
			QAction *copyCol = new QAction(i18n("&Copy Collection"), this);
			connect(copyCol, SIGNAL(activated()), this, SLOT(copyCollection()));
			QAction *renameCol = new QAction(i18n("&Rename Collection"), this);
			connect(renameCol, SIGNAL(activated()), this, SLOT(renameCollection()));
			QAction *delCol = new QAction(i18n("&Delete Collection"), this);
			connect(delCol, SIGNAL(activated()), this, SLOT(deleteCollection()));
			d->collectionsAction->menu()->addAction(newCol);
			d->collectionsAction->menu()->addAction(copyCol);
			d->collectionsAction->menu()->addAction(renameCol);
			d->collectionsAction->menu()->addAction(delCol);
			d->collectionsAction->menu()->addSeparator();
			fileMenu->addAction(d->collectionsAction);
			reloadCollections();
		}

		fileMenu->addSeparator();
		trayMenu->addSeparator();
		
		ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), this, SLOT(quitting()));
		ac->setText(tr("&Quit"));
		trayMenu->addAction(ac);
		fileMenu->addAction(ac);
		
		
#ifndef Q_WS_MAC
		ac = d->toggleMenubarAction = new QAction(this);
		connect(ac, SIGNAL(toggled(bool)), SLOT(toggleMenuBar()));
		ac->setShortcut(QKeySequence("Ctrl+M"));
		ac->setText(tr("Show &Menubar"));
		ac->setCheckable(true);
		settingsMenu->addAction(ac);
		addAction(ac);
#endif
		
		ac = d->toggleToolbarAction = new QAction(this);
		connect(ac, SIGNAL(triggered()), SLOT(toggleToolBar()));
		ac->setText(tr("Show &Toolbar"));
		ac->setCheckable(true);
		settingsMenu->addAction(ac);
		
		settingsMenu->addSeparator();
		
		ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), SLOT(showSettings()));
		ac->setText(tr("&Configure Meow..."));
		settingsMenu->addAction(ac);

		d->volumeUpAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->player, SLOT(volumeUp()));
		ac->setText(tr("&Volume Up"));
		
		d->volumeDownAction = ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), d->player, SLOT(volumeDown()));
		ac->setText(tr("&Volume Down"));
		
		d->muteAction = ac = new QAction(this);
//		connect(ac, SIGNAL(triggered()), d->player, SLOT(volumeMute()));
		ac->setText(tr("&Mute"));
	}
	
	{
		QAction *ac = new QAction(this);
		connect(ac, SIGNAL(triggered()), SLOT(showAbout()));
		ac->setText(tr("&About Meow..."));
		helpMenu->addAction(ac);
	}
	
	{ // context menu
		d->contextMenu = new QMenu(this);
	
		QAction *ac = d->contextMenu->addAction(
				tr("&Remove from playlist"), d->view, SLOT(removeSelected())
			);
		ac->setShortcut(Qt::Key_Delete);
		
		d->itemProperties = d->contextMenu->addAction(
				tr("&Properties"), this, SLOT(itemProperties())
			);
		d->itemProperties->setText(tr("&Properties"));
	}
	
	connect(d->view, SIGNAL(kdeContextMenu(QPoint)), SLOT(showItemContext(QPoint)));
	connect(d->player, SIGNAL(currentItemChanged(File)), SLOT(changeCaption(File)));
	connect(d->player, SIGNAL(playing(bool)), SLOT(isPlaying(bool)));

	d->volumeSlider = new SpecialSlider(this);
	connect(d->volumeSlider, SIGNAL(sliderMoved(int)), d->player, SLOT(setVolume(int)));
	connect(d->player, SIGNAL(volumeChanged(int)), d->volumeSlider, SLOT(setValue(int)));

	setAcceptDrops(true);
	
	d->toggleToolbarAction->setChecked(topToolbar->isVisibleTo(this));
	d->toggleMenubarAction->setChecked(menuBar()->isVisibleTo(this));
		
	QSettings settings;
	d->player->setVolume(settings.value("state/volume", 50).toInt());
	
	{
		QString order = settings.value("state/selector", "linear").toString();
		int index;
		if (order == "randomartist")
			index = TreeView::RandomArtist;
		else if (order == "randomalbum")
			index = TreeView::RandomAlbum;
		else if (order == "randomsong")
			index = TreeView::RandomSong;
		else
			index = TreeView::Linear;
		for (
				std::map<QAction*, TreeView::SelectorType>::iterator i = d->selectors.begin();
				i != d->selectors.end(); ++i
			)
		{
			if (d->selectors[i->first] == index)
			{
				i->first->setChecked(true);
				selectorActivated(i->first);
				break;
			}
		}
	}
	
	
	FileId first = settings.value("state/lastPlayed", 0).toInt();
	
	loadCollection("collection", first);
}

Meow::MainWindow::~MainWindow()
{
//	delete d->collection;
//	delete d;
}


void Meow::MainWindow::addFile(const QUrl &url)
{
	d->collection->add( url.toLocalFile() );
}

void Meow::MainWindow::addFiles()
{
	if (d->openFileDialog)
	{
		d->openFileDialog->show();
		d->openFileDialog->raise();
		return;
	}
	d->openFileDialog = new QFileDialog(
			this,
			tr("Add Files"),
			QString()
		);
	
	d->openFileDialog->setFileMode(QFileDialog::ExistingFiles);
	
	connect(d->openFileDialog, SIGNAL(accepted()), SLOT(fileDialogAccepted()));
	connect(d->openFileDialog, SIGNAL(rejected()), SLOT(fileDialogClosed()));
	d->openFileDialog->show();
}

void Meow::MainWindow::addDirs()
{
	if (d->openFileDialog)
	{
		d->openFileDialog->show();
		d->openFileDialog->raise();
		return;
	}
	d->openFileDialog = new QFileDialog(
			this,
			tr("Add Folder"),
			QString(),
			d->player->mimeTypes().join(";")
		);
	
	d->openFileDialog->setFileMode(QFileDialog::DirectoryOnly);
	
	connect(d->openFileDialog, SIGNAL(accepted()), SLOT(fileDialogAccepted()));
	connect(d->openFileDialog, SIGNAL(rejected()), SLOT(fileDialogClosed()));
	d->openFileDialog->show();
}

void Meow::MainWindow::fileDialogAccepted()
{
	if (!d->openFileDialog) return;

	QStringList files = d->openFileDialog->selectedFiles();
	fileDialogClosed();
	
	d->collection->startJob();
	for(QStringList::Iterator it=files.begin(); it!=files.end(); ++it)
		beginDirectoryAdd(*it);
	d->collection->scheduleFinishJob();
}

void Meow::MainWindow::fileDialogClosed()
{
	d->openFileDialog->deleteLater();
	d->openFileDialog = 0;
}

void Meow::MainWindow::toggleVisible()
{
	isVisible() ? hide() : show();
}
	
void Meow::MainWindow::closeEvent(QCloseEvent *event)
{
	if (!d->quitting)
	{
		toggleVisible();
		event->ignore();
		return;
	}
	QSettings settings;
	settings.setValue("state/volume", d->player->volume());
	settings.setValue("state/lastPlayed", d->player->currentFile().fileId());

	TreeView::SelectorType selector = d->selectors[d->selectorActions.checkedAction()];
	if (selector == TreeView::RandomArtist)
		settings.setValue("state/selector", "randomartist");
	else if (selector == TreeView::RandomAlbum)
		settings.setValue("state/selector", "randomalbum");
	else if (selector == TreeView::RandomSong)
		settings.setValue("state/selector", "randomsong");
	else
		settings.setValue("state/selector", "linear");
	QMainWindow::closeEvent(event);
}

void Meow::MainWindow::quitting()
{
	d->quitting=true;
	close();
}


void Meow::MainWindow::showVolume()
{
	QPoint at = d->topToolbar->mapToGlobal(d->topToolbar->widgetForAction(d->volumeAction)->frameGeometry().bottomLeft());
	d->volumeSlider->move(at);
	d->volumeSlider->show();
}


void Meow::MainWindow::dropEvent(QDropEvent *event)
{
	d->collection->startJob();
	QList<QUrl> files = event->mimeData()->urls();
	for(QList<QUrl>::Iterator it=files.begin(); it!=files.end(); ++it)
		beginDirectoryAdd(it->toLocalFile());
}

void Meow::MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat("text/uri-list"))
		event->acceptProposedAction();
}


void Meow::MainWindow::adderDone()
{
	d->collection->scheduleFinishJob();
	delete d->adder;
	d->adder = 0;
}

void Meow::MainWindow::beginDirectoryAdd(const QString &file)
{
	if (QFileInfo(file).isFile())
	{
		addFile(QUrl::fromLocalFile(file));
		return;
	}
	
	if (!d->adder)
	{
		d->collection->startJob();
		d->adder = new DirectoryAdder(this);
		connect(d->adder, SIGNAL(done()), SLOT(adderDone()));
		connect(d->adder, SIGNAL(addFile(QUrl)), SLOT(addFile(QUrl)));
	}
	d->adder->add(file);
}

void Meow::MainWindow::showItemContext(const QPoint &at)
{
	d->contextMenu->popup(at);
}

void Meow::MainWindow::changeCaption(const File &f)
{
	if (f)
		setWindowTitle(tr("%1 - Meow").arg(f.title()));
	else
		setWindowTitle(tr("Meow"));
}

void Meow::MainWindow::showAbout()
{
	QMessageBox::about(
			this, tr("About Meow"),
			tr(
					"<qt>This is Meow %1. A cute music player.<br/><br/>"
					"By <a href=\"mailto:charles@kde.org\">Charles Samuels</a>. He likes cats.<br/><br/>"
					"<a href=\"http://derkarl.org/meow\">http://derkarl.org/meow</a><br/><br/>"
					"Copyright (c) 2008-2011 Charles Samuels<br/>"
					"Copyright (c) 2004-2006 Allen Sandfeld Jensen (Most of Akode backend)<br/>"
					"Copyright (c) 2000-2007 Stefan Gehn, Charles Samuels (Portions of playback controller)<br/>"
					"Copyright (c) 2000-2007 Josh Coalson (FLAC decoder)<br/>"
					"Copyright (c) 1994-2010 the Xiph.Org Foundation (Vorbis decoder)<br/>"
					"Copyright (c) 2000-2004 Underbit Technologies, Inc (mp3 decoder)<br/>"
					"Copyright (c) 2005 The Musepack Development Team (Musepack decoder)<br/>"
					"Copyright (c) 2001 Ross P. Johnson (Posix threads library for Windows)<br/>"
					"Copyright (c) 2007-2009 Oxygen project (Icons)<br/>"
					"The Public Domain's SQLite<br/><br/>"
					"Meow is Free software, you may modify and share it under the "
					"<a href=\"http://www.gnu.org/licenses/gpl-3.0.html\">terms of the GPL version 3</a>."
					"</qt>"
				).arg(MEOW_VERSION)
		);
}

void Meow::MainWindow::toggleToolBar()
{
	d->topToolbar->setVisible(d->toggleToolbarAction->isChecked());
}

void Meow::MainWindow::toggleMenuBar()
{
	const bool showing = d->toggleMenubarAction->isChecked();
	menuBar()->setVisible(showing);

	if (!showing)
		QMessageBox::information(
				this,
				tr("Hiding Menubar"),
				tr("If you want to show the menubar again, press %1").arg 
					(d->toggleMenubarAction->shortcut().toString(QKeySequence::NativeText))
			);
}

void Meow::MainWindow::isPlaying(bool pl)
{
	if (pl)
	{
		d->playPauseAction->setIcon(QIcon(":/media-playback-pause.png"));
		d->playPauseAction->setText(tr("Paws"));
		d->tray->setIcon(renderIcon(":/meow.png", ":/media-playback-start.png"));
	}
	else
	{
		d->playPauseAction->setIcon(QIcon(":/media-playback-start"));
		d->playPauseAction->setText(tr("Play"));
		d->tray->setIcon(renderIcon(":/meow.png", ":/media-playback-pause.png"));
	}
}

void Meow::MainWindow::systemTrayClicked(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == QSystemTrayIcon::MiddleClick)
		d->player->playpause();
	else if (reason == QSystemTrayIcon::Trigger)
		isVisible() ? hide() : show();
}

void Meow::MainWindow::itemProperties()
{
	QList<File> files = d->view->selectedFiles();
	if (!files.isEmpty())
	{
#ifdef _WIN32
		SHELLEXECUTEINFOW in = {0};
		in.cbSize = sizeof(in);
		in.fMask = SEE_MASK_INVOKEIDLIST;
		in.hwnd = effectiveWinId();
		in.lpVerb = L"properties";
		std::cerr << "file: " << files[0].file().toUtf8().constData() << std::endl;
		in.lpFile = (const WCHAR*)files[0].file().utf16();
		ShellExecuteExW(&in);
#endif
	}
}

void Meow::MainWindow::selectorActivated(QAction* action)
{
	d->view->setSelector( d->selectors[action] );
}

QIcon Meow::MainWindow::renderIcon(const QString& baseIcon, const QString &overlayIcon) const
{
	QPixmap iconPixmap = QIcon(baseIcon).pixmap(16);
	if (!overlayIcon.isEmpty())
	{
		QPixmap overlayPixmap
			= QIcon(overlayIcon)
				.pixmap(22/2);
		QPainter p(&iconPixmap);
		p.drawPixmap(
				iconPixmap.width()-overlayPixmap.width(),
				iconPixmap.height()-overlayPixmap.height(),
				overlayPixmap
			);
		p.end();
	}
	return iconPixmap;
}





// kate: space-indent off; replace-tabs off;

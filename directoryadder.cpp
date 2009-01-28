#include "directoryadder.h"

#include <kfileitem.h>

#include <qfileinfo.h>

Meow::DirectoryAdder::DirectoryAdder(QObject *parent)
	: QObject(parent)
{
	listJob=0;
}

void Meow::DirectoryAdder::add(const KUrl &dir)
{
	if (QFileInfo(dir.path()).isFile())
	{
		emit addFile(dir);
		emit done();
	}
	else
	{
		pendingAddDirectories.append(dir);
		addNextPending();
	}
}

void Meow::DirectoryAdder::addNextPending()
{
	KUrl::List::Iterator pendingIt= pendingAddDirectories.begin();
	if (!listJob && (pendingIt!= pendingAddDirectories.end()))
	{
		currentJobUrl= *pendingIt;
		listJob = KIO::listRecursive(currentJobUrl, KIO::HideProgressInfo, false);
		connect(
				listJob, SIGNAL(entries(KIO::Job*, const KIO::UDSEntryList&)),
				SLOT(slotEntries(KIO::Job*, const KIO::UDSEntryList&))
			);
		connect(
				listJob, SIGNAL(result(KJob*)),
				SLOT(slotResult(KJob*))
			);
		connect(
				listJob, SIGNAL(redirection(KIO::Job *, const KUrl &)),
				SLOT(slotRedirection(KIO::Job *, const KUrl &))
			);
		pendingAddDirectories.erase(pendingIt);
	}
}

void Meow::DirectoryAdder::slotResult(KJob *job)
{
	listJob= 0;
	addNextPending();
	if (!listJob)
		emit done();
}

void Meow::DirectoryAdder::slotEntries(KIO::Job *, const KIO::UDSEntryList &entries)
{
	for (KIO::UDSEntryList::ConstIterator it = entries.begin(); it != entries.end(); ++it)
	{
		KFileItem file(*it, currentJobUrl, false /* no mimetype detection */, true);
		emit addFile(file.url());
	}

}

void Meow::DirectoryAdder::slotRedirection(KIO::Job *, const KUrl & url)
{
	currentJobUrl= url;
}

// kate: space-indent off; replace-tabs off;

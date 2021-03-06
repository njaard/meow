/* This file is part of Meow

  Copyright 2000-2011 by Charles Samuels <charles@kde.org>
  Copyright 2001-2007 by Stefan Gehn <mETz81@web.de>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "player_p.h"
#include "player.h"

#include <qregexp.h>
#include <qfile.h>
#include <qtimer.h>
#include <qstringlist.h>
#include <qlocale.h>
#include <qdir.h>
#include <qpluginloader.h>
#include <qcoreapplication.h>
#ifdef MEOW_WITH_DBUS
#include <qdbusconnection.h>
#endif

#ifdef MEOW_WITH_KDE
#include <kdebug.h>
#include <klocale.h>
#include <kglobal.h>
#endif

#include "akode/plugins/mpg123_decoder.h"
#include "akode/plugins/vorbis_decoder.h"
#ifdef AKODE_WITH_OPUS
#include "akode/plugins/opus_decoder.h"
#endif
#include "akode/plugins/mpc_decoder.h"
#include "akode/plugins/flac113_decoder.h"
#include "akode/plugins/speex_decoder.h"

#ifdef _WIN32
#include "akode/plugins/dsound_sink.h"
#elif __linux__
#include "akode/plugins/alsa_sink.h"
#endif

#include <cmath>
#include <iostream>


namespace Meow
{


Player::State PlayerPrivate::convertState(aKode::Player::State s)
{
	switch(s)
	{
	case aKode::Player::Open:
	case aKode::Player::Loaded:
	case aKode::Player::Playing:
		return Player::PlayingState;
	case aKode::Player::Paused:
		return Player::PausedState;
	default:
		return Player::StoppedState;
	}
}

void PlayerPrivate::initAvKode()
{
	try
	{
		if (akPlayer)
			return;
		akPlayer = new aKode::Player;
	#ifdef _WIN32
		akPlayer->open( aKode::dsound_sink().openSink(device) );
	#elif __linux__
		akPlayer->open( aKode::alsa_sink().openSink(device) );
	#else
	#error No sink
	#endif
		akPlayer->registerDecoderPlugin(&aKode::mpg123_decoder());
		akPlayer->registerDecoderPlugin(&aKode::vorbis_decoder());
	#ifdef AKODE_WITH_OPUS
		akPlayer->registerDecoderPlugin(&aKode::opus_decoder());
	#endif
		akPlayer->registerDecoderPlugin(&aKode::flac_decoder());
	#ifdef AKODE_WITH_MUSEPACK
		akPlayer->registerDecoderPlugin(&aKode::mpc_decoder());
	#endif
		akPlayer->registerDecoderPlugin(&aKode::speex_decoder());
		akPlayer->setManager(shared_from_this());

		q->setVolume(volumePercent);
		
		QObject::connect(
				q, SIGNAL(stStateChangeEvent(int)),
				q, SLOT(tStateChangeEvent(int)),
				Qt::QueuedConnection
			);
		QObject::connect(
				q, SIGNAL(stEofEvent()),
				q, SLOT(tEofEvent()),
				Qt::QueuedConnection
			);
		QObject::connect(
				q, SIGNAL(stErrorEvent()),
				q, SLOT(tErrorEvent()),
				Qt::QueuedConnection
			);
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

void PlayerPrivate::stateChangeEvent(aKode::Player::State state)
{
	emit q->stStateChangeEvent(state);
}

void PlayerPrivate::eofEvent()
{
	emit q->stEofEvent();
}

void PlayerPrivate::errorEvent()
{
	emit q->stErrorEvent();
}

void PlayerPrivate::tStateChangeEvent(int newState)
{
	try
	{
		switch (newState)
		{
		case aKode::Player::Playing:
			emit q->playing();
			emit q->playing(true);
			timer->start(500);
			break;
		case aKode::Player::Paused:
			emit q->playing(false);
			emit q->paused();
			timer->stop();
			break;
		case aKode::Player::Loaded:
			if (nowLoading)
			{
				nowLoading = false;
				akPlayer->play();
			}
			else
			{
				emit q->playing(false);
				emit q->stopped();
				timer->stop();
			}
		default:
			break;
		}
		emit q->stateChanged(convertState(aKode::Player::State(newState)));
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

void PlayerPrivate::tEofEvent()
{
	emit q->finished();
}

void PlayerPrivate::tErrorEvent()
{
	emit q->finished();
}

void PlayerPrivate::tick()
{
	try
	{
		if (std::shared_ptr<aKode::Decoder> dec = akPlayer->decoder())
		{
			emit q->positionChanged(dec->length());
			emit q->lengthChanged(dec->position());
		}
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

// -----------------------------------------------------------------------------

Player::Player()
    : d(new PlayerPrivate)
{
	d->q = this;
	setObjectName("Player");
	d->timer = new QTimer(this);
	connect(d->timer, SIGNAL(timeout()), SLOT(tick()));

	d->akPlayer = 0;
	d->nowLoading = false;
	d->volumePercent = 50;
#ifdef MEOW_WITH_DBUS
	QDBusConnection connection = QDBusConnection::sessionBus();
	connection.registerObject("/player", this, QDBusConnection::ExportScriptableContents);
	connection.registerService("org.kde.meow");
#endif
}


Player::~Player()
{
	delete d->akPlayer;
}

std::vector<std::pair<std::string,std::string>> Player::devices() const
{
#ifdef _WIN32
	return aKode::dsound_sink().deviceNames();
#elif __linux__
	return aKode::alsa_sink().deviceNames();
#endif
}

std::string Player::currentDevice() const
{
	return d->device;
}

void Player::setCurrentDevice(const std::string &name)
{
	if (d->device == name) return;
	try
	{
		d->device = name;
		std::cerr << "Opening device " << name << std::endl;
#ifdef _WIN32
		if (d->akPlayer)
			d->akPlayer->open( aKode::dsound_sink().openSink(name) );
#elif __linux__
		if (d->akPlayer)
			d->akPlayer->open( aKode::alsa_sink().openSink(name) );
#endif
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

Player::State Player::state() const
{
	if (!d->akPlayer)
		return Player::StoppedState;
	return d->convertState(d->akPlayer->state());
}


bool Player::isPlaying() const
{
	return state() == Player::PlayingState;
}


bool Player::isPaused() const
{
	return state() == Player::PausedState;
}


bool Player::isStopped() const
{
	return state() == Player::StoppedState;
}


bool Player::isActive() const
{
	Player::State s = state();
	return s == Player::PausedState || s == Player::PlayingState;
}


void Player::stop()
{
	if (isStopped())
		return;
	try
	{
		d->akPlayer->stop();
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
	d->currentItem.reset();
	emit currentItemChanged(File());
}


void Player::pause()
{
	if (!isPlaying())
		return;
	try
	{
		d->akPlayer->pause();
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}


void Player::play()
{
	try
	{
		Player::State st = state();
		if (d->nowLoading)
			d->akPlayer->play();
		else if (st == Player::PlayingState)
			return;
		else if (st == Player::PausedState)
			d->akPlayer->play(); // unpause
		else if (d->currentItem.get())
			play(*d->currentItem);
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

namespace
{

template<int x> class dirty_trick
{
public:
	dirty_trick() { }
};
template<> class dirty_trick<0>
{
public:
	dirty_trick();
};

}

void Player::play(const File &item)
{
	d->initAvKode();
	if (!d->akPlayer)
		return;

	std::cerr << "Starting to play new current track" << std::endl;
	try
	{
		d->akPlayer->stop();
		d->currentItem.reset(new File(item));
		d->nowLoading = true;
	#ifdef _WIN32
		dirty_trick<sizeof(wchar_t) == sizeof(ushort)>();
		d->akPlayer->load( (wchar_t*)item.file().utf16() );
	#else
		d->akPlayer->load( QFile::encodeName(item.file()).data() );
	#endif
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
	emit currentItemChanged(*d->currentItem);
}


void Player::playpause()
{
	if (isPlaying())
		pause();
	else
		play();
}

void Player::playpause(bool p)
{
	try
	{
		if (p)
			play();
		else
			pause();
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

void Player::setPosition(unsigned int msec)
{
	try
	{
		if (d->akPlayer)
		{
			if (std::shared_ptr<aKode::Decoder> dec = d->akPlayer->decoder())
				dec->seek(msec);
		}
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
}

unsigned int Player::position() const
{
	if (d->akPlayer)
	{
		if (std::shared_ptr<aKode::Decoder> dec = d->akPlayer->decoder())
		{
			unsigned int p = dec->position();
			//std::cerr << "pos: " << p << std::endl;
			return p;
		}
	}
	return 0;
}

static QString formatDuration(int duration)
{
	duration /= 1000; // convert from milliseconds to seconds
	int days    = duration / (60 * 60 * 24);
	int hours   = duration / (60 * 60) % 24;
	int minutes = duration / 60 % 60;
	int seconds = duration % 60;

#ifndef MEOW_WITH_KDE
	return QString("%1%2:%3")
			.arg(duration < 0 ? "-" : QString())
			.arg(minutes, 2, 10, QChar('0'))
			.arg(seconds, 2, 10, QChar('0'));
#else
	KLocale *loc = KGlobal::locale();
	const QChar zeroDigit = QLocale::system().zeroDigit();
	KLocalizedString str;

	if (days > 0)
	{
		QString dayStr = i18np("%1 day", "%1 days", days);
		str = ki18nc("<negativeSign><day(s)> <hours>:<minutes>:<seconds>",
				"%1%2 %3:%4:%5")
				.subs(duration < 0 ? loc->negativeSign() : QString())
				.subs(dayStr)
				.subs(hours, 2, 10, zeroDigit)
				.subs(minutes, 2, 10, zeroDigit)
				.subs(seconds, 2, 10, zeroDigit);
	}
	else if (hours > 0)
	{
		str = ki18nc("<negativeSign><hours>:<minutes>:<seconds>", "%1%2:%3:%4")
				.subs(duration < 0 ? loc->negativeSign() : QString())
				.subs(hours, 2, 10, zeroDigit)
				.subs(minutes, 2, 10, zeroDigit)
				.subs(seconds, 2, 10, zeroDigit);
	}
	else
	{
		str = ki18nc("<negativeSign><minutes>:<seconds>", "%1%2:%3")
				.subs(duration < 0 ? loc->negativeSign() : QString())
				.subs(minutes, 2, 10, zeroDigit)
				.subs(seconds, 2, 10, zeroDigit);
	}
	return str.toString();
#endif
}

QString Player::positionString() const
{
	/*if (d->nInstance->config()->displayRemaining())
	{
		int len = currentLength();
		if (len > 0)
			return formatDuration(len - position());
	}*/
	return formatDuration(position());
}

unsigned int Player::currentLength() const
{
	try
	{
		if (d->akPlayer)
			if (std::shared_ptr<aKode::Decoder> dec = d->akPlayer->decoder())
			{
				unsigned int l = dec->length();
				// std::cerr << "len: " << l << std::endl;
				return l;
			}
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}
	return 0;
}

QString Player::lengthString() const
{
	return formatDuration(currentLength());
}

int Player::volume() const
{
	return d->volumePercent;
}

void Player::setVolume(int percent)
{
	try
	{
		percent = qBound(0, percent, 100);
		double vol = (std::pow(10,percent*.01)-1)/(std::pow(10, 1)-1);
		if (d->akPlayer)
			d->akPlayer->setVolume(vol);
	}
	catch (aKode::ExceptionBase &e)
	{
		std::cerr << "akode error: " << e.what() << std::endl;
	}

	if (d->volumePercent != percent)
	{
		d->volumePercent = percent;
		emit volumeChanged(percent);
	}
}

File Player::currentFile() const
{
	if (!d->currentItem.get())
		return File();
	else
		return *d->currentItem;
}

QString Player::currentTitle() const
{
	if (File f = currentFile())
		return f.title();
	return "";
}

QString Player::currentArtist() const
{
	if (File f = currentFile())
		return f.artist();
	return "";
}

QStringList Player::mimeTypes() const
{
	QStringList m;
	m << "audio/mpeg";
	m << "audio/x-vorbis+ogg";
	m << "audio/ogg";
#ifdef AKODE_WITH_OPUS
	m << "audio/opus";
	m << "audio/x-opus+ogg";
#endif
	m << "audio/flac";
	m << "audio/x-flac";
	m << "audio/x-musepack";
	m << "audio/speex";
	return m;
}

} // namespace

#include "player.moc"
// kate: space-indent off; replace-tabs off;

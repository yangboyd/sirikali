﻿/*
 *
 *  Copyright (c) 2012-2015
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mountinfo.h"
#include "utility.h"
#include "siritask.h"
#include "task.hpp"
#include "win.h"
#include "settings.h"
#include "engines.h"
#include "crypto.h"

#include <QMetaObject>
#include <QtGlobal>

#include <QFile>

#include <vector>
#include <utility>

QString mountinfo::mountProperties( const QString& mountPoint,
				    const QString& mode,
				    const QString& fileSystem,
				    const QString& cipherPath )
{
	return QString( "x x x:x x %1 %2,x - %3 %4 x" ).arg( mountPoint,mode,fileSystem,cipherPath ) ;
}

QString mountinfo::encodeMountPath( const QString& e )
{
	auto m = e ;
	/*
	 * linux's /proc/self/mountinfo makes these substitutions and we make
	 * them too to be consistent with linux
	 */
	//m.replace( "\n","\\012" ) ;
	m.replace( " ","\\040" ) ;
	//m.replace( "\\","\\134" ) ;
	//m.replace( "\\t","\\011" ) ;

	return m ;
}

static QStringList _macox_volumes_1()
{
	QStringList m ;

	// securefs@/Users/adam/woof on /Users/adam/.SiriKali/woof (osxfuse, nodev, nosuid, synchronous, mounted by adam)

	const QString w = "%1 on %2 (%3%4)" ;

	auto readOnly = []( const QStorageInfo& e ){

		if( e.isReadOnly() ){

			return ",read-only" ;
		}else{
			return "" ;
		}
	} ;

	for( const auto& it : QStorageInfo::mountedVolumes() ){

		m.append( w.arg( mountinfo::encodeMountPath( it.device() ),
				 mountinfo::encodeMountPath( it.rootPath() ),
				 it.fileSystemType(),
				 readOnly( it ) ) ) ;
	}

	return m ;
}

static QStringList _macox_volumes()
{
	QStringList s ;
	QString mode ;
	QString fs ;

	for( const auto& it : _macox_volumes_1() ){

		auto e = utility::split( it,' ' ) ;

		if( e.size() > 2 ){

			if( e.at( 3 ).contains( "read-only" ) ){

				mode = "ro" ;
			}else{
				mode = "rw" ;
			}

			fs = "fuse." + it.mid( 0,it.indexOf( '@' ) ) ;

			s.append( mountinfo::mountProperties( e.at( 2 ),mode,fs,e.at( 0 ) ) ) ;
		}
	}

	return s ;
}

static QStringList _windows_volumes()
{
	QStringList s ;

	for( const auto& e : SiriKali::Windows::getMountOptions() ){

		auto fs = "fuse." + e.subtype ;

		auto m = e.subtype + "@" + e.cipherFolder ;

		s.append( mountinfo::mountProperties( e.mountPointPath,e.mode,fs,m ) ) ;
	}

	return s ;
}

static QStringList _unlocked_volumes()
{
	auto a = [](){

		if( utility::platformIsLinux() ){

			return utility::split( utility::fileContents( "/proc/self/mountinfo" ) ) ;

		}else if( utility::platformIsOSX() ){

			return _macox_volumes() ;
		}else{
			return _windows_volumes() ;
		}
	}() ;

	return a + engines::instance().mountInfo( a ) ;
}

mountinfo::mountinfo( QObject * parent,bool e,std::function< void() >&& quit ) :
	m_parent( parent ),
	m_quit( std::move( quit ) ),
	m_announceEvents( e ),
	m_oldMountList( _unlocked_volumes() ),
	m_folderMountEvents( [ this ]( const QString& e ){ this->autoMount( e ) ; } )
{
	if( utility::platformIsLinux() ){

		this->linuxMonitor() ;

	}else if( utility::platformIsOSX() ){

		this->osxMonitor() ;
	}else{
		this->windowsMonitor() ;
	}
}

mountinfo::~mountinfo()
{
}

Task::future< std::vector< volumeInfo > >& mountinfo::unlockedVolumes()
{
	return Task::run( [](){

		auto _decode = []( QString path,bool set_offset ){

			path.replace( "\\012","\n" ) ;
			path.replace( "\\040"," " ) ;
			path.replace( "\\134","\\" ) ;
			path.replace( "\\011","\\t" ) ;
			path.replace( "SiriKaliSpecialCharacter001","," ) ;

			if( set_offset ){

				return path.mid( path.indexOf( '@' ) + 1 ) ;
			}else{
				return path ;
			}
		} ;

		auto _starts_with = []( const engines::engine& e,const QString& s ){

			for( const auto& it : e.names() ){

				if( s.startsWith( it + "@" ) ){

					return true ;
				}
			}

			return false ;
		} ;

		auto _fs = []( QString e ){

			e.replace( "fuse.","" ) ;

			return e ;
		} ;

		auto _ro = []( const QStringList& l ){

			return l.at( 5 ).mid( 0,2 ) ;
		} ;

		std::vector< volumeInfo > e ;

		volumeInfo::mountinfo info ;

		const auto& engines = engines::instance() ;

		for( const auto& it : _unlocked_volumes() ){

			const auto k = utility::split( it,' ' ) ;

			const auto s = k.size() ;

			if( s < 8 ){

				continue ;
			}

			const auto& cf = k.at( s - 2 ) ;

			const auto& m = k.at( 4 ) ;

			const auto& fs = k.at( s - 3 ) ;

			const auto& engine = engines.getByFuseName( fs ) ;

			if( engine.known() ){

				if( _starts_with( engine,cf ) ){

					info.volumePath = _decode( cf,true ) ;

				}else if( engine.setsCipherPath() ){

					info.volumePath = _decode( cf,false ) ;
				}else{
					info.volumePath = crypto::sha256( m ).mid( 0,20 ) ;
				}

				info.mountPoint   = _decode( m,false ) ;
				info.fileSystem   = _fs( fs ) ;
				info.mode         = _ro( k ) ;
				info.mountOptions = k.last() ;

				e.emplace_back( info ) ;
			}
		}

		return e ;
	} ) ;
}

void mountinfo::stop()
{
	if( m_stop ){

		m_stop() ;
	}else{
		m_quit() ;
	}
}

void mountinfo::volumeUpdate()
{
	m_newMountList = _unlocked_volumes() ;

	auto _volumeWasMounted = [ & ](){

		return m_oldMountList.size() < m_newMountList.size() ;
	} ;

	auto _mountedVolume = [ & ]( const QString& e ){

		return !m_oldMountList.contains( e ) ;
	} ;

	if( m_announceEvents ){

		this->pbUpdate() ;

		if( _volumeWasMounted() ){

			for( const auto& it : m_newMountList ){

				const auto e = utility::split( it,' ' ) ;

				if( _mountedVolume( it ) && e.size() > 3 ){

					this->autoMount( e.at( 4 ) ) ;
				}
			}
		}
	}

	m_oldMountList = std::move( m_newMountList ) ;
}

void mountinfo::updateVolume()
{
	QMetaObject::invokeMethod( this,"volumeUpdate",Qt::QueuedConnection ) ;
}

void mountinfo::pbUpdate()
{
	QMetaObject::invokeMethod( m_parent,"pbUpdate",Qt::QueuedConnection ) ;
}

void mountinfo::autoMount( const QString& e )
{
	QMetaObject::invokeMethod( m_parent,
				   "autoMountFavoritesOnAvailable",
				   Qt::QueuedConnection,
				   Q_ARG( QString,e ) ) ;
}

void mountinfo::announceEvents( bool s )
{
	m_announceEvents = s ;
}

void mountinfo::linuxMonitor()
{
	auto s = std::addressof( Task::run( [ this ](){

		QFile s( "/proc/self/mountinfo" ) ;
		struct pollfd m ;

		s.open( QIODevice::ReadOnly ) ;
		m.fd     = s.handle() ;
		m.events = POLLPRI ;

		while( true ){

			poll( &m,1,-1 ) ;

			this->updateVolume() ;
		}
	} ) ) ;

	m_stop = [ s ](){ s->first_thread()->terminate() ; } ;

	if( m_folderMountEvents.monitor() ){

		auto e = std::addressof( Task::run( [ & ](){

			m_folderMountEvents.start() ;
		} ) ) ;

		e->then( [ & ](){

			m_folderMountEvents.stop() ;
			m_quit() ;
		} ) ;

		s->then( [ = ](){

			e->first_thread()->terminate() ;
		} ) ;
	}else{
		s->then( std::move( m_quit ) ) ;
	}
}

void mountinfo::pollForUpdates()
{
	m_exit = false ;

	m_stop = [ this ](){ m_exit = true ; } ;

	auto interval = settings::instance().pollForUpdatesInterval() ;

	Task::run( [ &,interval ](){

		auto previous = QStorageInfo::mountedVolumes() ;
		auto now = previous ;

		while( true ){

			if( m_exit ){

				break ;
			}else{
				utility::Task::wait( interval ) ;

				now = QStorageInfo::mountedVolumes() ;

				if( now != previous ){

					this->updateVolume() ;
				}

				previous = std::move( now ) ;
			}
		}

	} ).then( std::move( m_quit ) ) ;
}

void mountinfo::osxMonitor()
{
	this->pollForUpdates() ;
}

void mountinfo::windowsMonitor()
{
	SiriKali::Windows::updateVolumeList( [ this ]{ this->updateVolume() ; } ) ;
}

#ifdef Q_OS_LINUX

#include <sys/inotify.h>
#include <sys/select.h>

mountinfo::folderMountEvents::folderMountEvents( std::function< void( const QString& ) > function ) :
	m_inotify_fd( inotify_init() ),m_update( std::move( function ) )
{
	if( m_inotify_fd != -1 ){

		for( const auto& it : settings::instance().mountMonitorFolderPaths() ){

			int e = inotify_add_watch( m_inotify_fd,it.toLatin1(),IN_CREATE ) ;

			if( e != -1 ){

				if( it.endsWith( "/" ) ){

					auto s = it ;

					while( s.endsWith( "/" ) ){

						s = utility::removeLast( s,1 ) ;
					}

					m_fds.emplace_back( e,s ) ;
				}else{
					m_fds.emplace_back( e,it ) ;
				}
			}
		}
	}
}

void mountinfo::folderMountEvents::start()
{
	const char * currentEvent ;
	const char * end ;

	std::vector< char > buffer( 10485760 ) ;

	fd_set rfds ;

	FD_ZERO( &rfds ) ;

	struct timeval tv ;

	int select_fd = m_inotify_fd + 1 ;

	int interval = settings::instance().mountMonitorFolderPollingInterval() ;

	auto _reset_variables = [ & ](){

		FD_SET( m_inotify_fd,&rfds ) ;

		tv.tv_sec = interval ;
		tv.tv_usec = 0 ;
	} ;

	struct event{
		bool eventReceived ;
		bool timeout ;
	} ;

	auto _eventsReceived = [ & ]()->event{

		_reset_variables() ;

		int r = [ & ](){

			if( tv.tv_sec > 0 ){

				return select( select_fd,&rfds,nullptr,nullptr,&tv ) ;
			}else{
				return select( select_fd,&rfds,nullptr,nullptr,nullptr ) ;
			}
		}() ;

		if( r == 0 ){

			return { false,true } ;

		}else if( r > 0 ){

			auto s = read( m_inotify_fd,buffer.data(),buffer.size() ) ;

			if( s > 0 ){

				end          = buffer.data() + s ;
				currentEvent = buffer.data() ;

				return { true,false } ;
			}
		}

		return { false,false } ;
	} ;

	auto _processEvent = [ & ]( const struct inotify_event * event ){

		if( event->mask & IN_CREATE ){

			for( const auto& it : m_fds ){

				if( it.fd() == event->wd ){

					m_update( it.path() + "/" + event->name ) ;

					break ;
				}
			}
		}
	} ;

	auto _processTimeout = [ & ](){

		for( auto& it : m_fds ){

			it.contentCountChanged( m_update ) ;
		}
	} ;

	while( true ){

		auto e = _eventsReceived() ;

		if( e.eventReceived ){

			while( currentEvent < end ){

				auto event = reinterpret_cast< const struct inotify_event * >( currentEvent ) ;

				_processEvent( event ) ;
				currentEvent += sizeof( struct inotify_event ) + event->len ;
			}

		}else if( e.timeout ){

			_processTimeout() ;
		}
	}
}

void mountinfo::folderMountEvents::stop()
{
	close( m_inotify_fd ) ;

	for( const auto& it : m_fds ){

		close( it.fd() ) ;
	}
}

bool mountinfo::folderMountEvents::monitor()
{
	return m_inotify_fd != -1 && m_fds.size() > 0 ;
}

#else

mountinfo::folderMountEvents::folderMountEvents( std::function< void( const QString& ) > e )
{
	Q_UNUSED( e )
}
void mountinfo::folderMountEvents::start()
{
}
void mountinfo::folderMountEvents::stop()
{
}
bool mountinfo::folderMountEvents::monitor()
{
	return false ;
}

#endif

/*  aKode: Sink abstract-type

    Copyright (C) 2004 Allan Sandfeld Jensen <kde@carewolf.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef _AKODE_SINK_H
#define _AKODE_SINK_H

#include "plugin.h"

#include "akode_export.h"

#include <vector>
#include <memory>

namespace aKode
{

class AudioFrame;
class AudioBuffer;
class AudioConfiguration;


//! A generic interface for all sinks

/*!
 * Sinks are where an audiostream goes. It can in aKodeLib be either an
 * encoder or an audio-output such as ALSA or OSS.
 */
class AKODE_EXPORT Sink
{
public:
    virtual ~Sink() {};
    /*!
     * Opens the sink for playing and returns true if succesfull.
     */
    virtual bool open() = 0;
    /*!
     * Closes the sink and releases any held resources or processes.
     */
    virtual void close() {};
    /*!
     * Tries to configure the sink for the configuration \a config.
     * Returns 0 if a perfect match was possible, positive if a close
     * match was possible, and negative to indicate complete failure.
     */
    virtual int setAudioConfiguration(const AudioConfiguration* config) = 0;
    /*!
     * Returns the configuration the sink currently expects.
     */
    virtual const AudioConfiguration* audioConfiguration() const = 0;
    /*!
     * Writes one frame to the sink. If the frame has a different configuration
     * than the sink expects, the result is undefined.
     *
     * Some sinks might handle automatic reconfiguration gracefully, but this
     * will be plugin-specific and cannot be generally relied upon.
     */
    virtual bool writeFrame(AudioFrame* frame) = 0;
    /*!
     * Pauses buffer-playback in the sink in case it has a buffer of significance.
     * Notice that not all sinks are capable of precise pausing.
     */
    virtual void pause() {};
    /*!
     * Resume from a paused state
     */
    virtual void resume() {};
};

class SinkPlugin : public Plugin
{
public:
    SinkPlugin(const std::string &name) : Plugin(name + "_sink") { }
    virtual ~SinkPlugin() { }
    /*!
     * Asks the plugin to open a Sink, returns 0 if the
     * plugin could not.
     */
    virtual std::shared_ptr<Sink> openSink(const std::string &deviceName) = 0;
    
    /**
     * asks the plugin for a list of available device names,
     * which can be specified to @p openSink
     **/
    virtual std::vector<std::pair<std::string, std::string>> deviceNames() = 0;
};

} // namespace

#endif

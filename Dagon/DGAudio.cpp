////////////////////////////////////////////////////////////
//
// DAGON - An Adventure Game Engine
// Copyright (c) 2011 Senscape s.r.l.
// All rights reserved.
//
// NOTICE: Senscape permits you to use, modify, and
// distribute this file in accordance with the terms of the
// license agreement accompanying it.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////

#include "DGAudio.h"

using namespace std;

////////////////////////////////////////////////////////////
// Implementation - Constructor
////////////////////////////////////////////////////////////

DGAudio::DGAudio() {
    config = &DGConfig::getInstance();
    log = &DGLog::getInstance();
    
    _oggCallbacks.read_func = _oggRead;
	_oggCallbacks.seek_func = _oggSeek;
	_oggCallbacks.close_func = _oggClose;
	_oggCallbacks.tell_func = _oggTell;
    
    _retainCount = 0;
    
    // Default volume is the loudest possible
    _defaultVolume = 1.0f;
    _currentVolume = 1.0f;
    _targetVolume = 1.0f;
    
    // And reasonably smooth default fades
    _fadeStep = 0.1f;
    _volumeFadeStep = 0.1f;
    
    _isFading = false;
    _isLoaded = false;
    
    // For convenience, this always defaults to false
    _isLoopable = false;
    _isPlaying = false;
    
    this->setType(DGObjectAudio);
}

////////////////////////////////////////////////////////////
// Implementation - Destructor
////////////////////////////////////////////////////////////

DGAudio::~DGAudio() {
    // Force an unload
    this->unload();
}

////////////////////////////////////////////////////////////
// Implementation - Checks
////////////////////////////////////////////////////////////

bool DGAudio::isLoaded() {
    return _isLoaded;
}

bool DGAudio::isLoopable() {
    return _isLoopable;
}

bool DGAudio::isPlaying() {
    return _isPlaying;
}

////////////////////////////////////////////////////////////
// Implementation - Gets
////////////////////////////////////////////////////////////

int DGAudio::retainCount() {
    return _retainCount;
}

////////////////////////////////////////////////////////////
// Implementation - Sets
////////////////////////////////////////////////////////////

void DGAudio::fadeIn() {
    _currentVolume = 0.0f;
    _targetVolume = _defaultVolume;
    _fadeDirection = DGAudioFadeIn;
    _isFading = true;
}

void DGAudio::fadeOut() {
    _targetVolume = 0.0f;
    _fadeDirection = DGAudioFadeOut;
    _isFading = true;
}

void DGAudio::forceVolume(float newVolume) {
    _currentVolume = newVolume;
    _targetVolume = newVolume;
}

void DGAudio::setFadeTime(int withMilliseconds) {
    _fadeStep = 1.0f / (float)withMilliseconds;
}

void DGAudio::setFadeForVolumeChanges(int withMilliseconds) {
    _volumeFadeStep = 1.0f / (float)withMilliseconds; 
}

void DGAudio::setLoopable(bool loopable) {
    _isLoopable = loopable;
}

void DGAudio::setPosition(unsigned int onFace) {    
    switch (onFace) {
        case DGNorth:
            alSource3f(_alSource, AL_POSITION, 0.0, 0.0, -1.0);
            break;
        case DGEast:
            alSource3f(_alSource, AL_POSITION, 1.0, 0.0, 0.0);
            break;
        case DGSouth:
            alSource3f(_alSource, AL_POSITION, 0.0, 0.0, 1.0);
            break;
        case DGWest:
            alSource3f(_alSource, AL_POSITION, -1.0, 0.0, 0.0);
            break;
        case DGUp:
            alSource3f(_alSource, AL_POSITION, 0.0, 1.0, 0.0);
            break;
        case DGDown:
            alSource3f(_alSource, AL_POSITION, 0.0, -1.0, 0.0);
            break;            
    }
}

void DGAudio::setResource(const char* fromFileName) {
    strncpy(_resource.name, fromFileName, DGMaxFileLength);
}

void DGAudio::setVolume(float targetVolume) {
    _targetVolume = targetVolume;
}

////////////////////////////////////////////////////////////
// Implementation - State changes
////////////////////////////////////////////////////////////

void DGAudio::load() {
    if (!_isLoaded) {    
        FILE* fh;
        
        fh = fopen(config->path(DGPathRes, _resource.name), "rb");	
        
        if (fh != NULL) {
            fseek(fh, 0, SEEK_END);
            _resource.dataSize = ftell(fh);
            fseek(fh, 0, SEEK_SET);
            _resource.data = (char*)malloc(_resource.dataSize);
            fread(_resource.data, _resource.dataSize, 1, fh);
            _resource.dataRead = 0;
            
            if (ov_open_callbacks(this, &_oggStream, NULL, 0, _oggCallbacks) < 0) {
                log->error(DGModAudio, "%s", DGMsg270007);
                return;
            }
            
            // Get file info
            vorbis_info* info = ov_info(&_oggStream, -1);
            
            _channels = info->channels;
            _rate = info->rate;
            
            if (_channels == 1) _alFormat = AL_FORMAT_MONO16;
            else if (_channels == 2 ) _alFormat = AL_FORMAT_STEREO16;
            else {
                // Invalid number of channels
                log->error(DGModAudio, "%s: %s", DGMsg270006, _resource.name);
                return;
            }
            
            // We no longer require the file handle
            fclose(fh);
            
            alGenBuffers(DGAudioNumberOfBuffers, _alBuffers);
            alGenSources(1, &_alSource);
            
            alSourcef (_alSource, AL_GAIN,			  _currentVolume);
            alSource3f(_alSource, AL_POSITION,        0.0, 0.0, 0.0);
            alSource3f(_alSource, AL_VELOCITY,        0.0, 0.0, 0.0);
            alSource3f(_alSource, AL_DIRECTION,       0.0, 0.0, 0.0);
            
            _verifyError("load");
            
            _isLoaded = true;
        }
        else log->error(DGModAudio, "%s: %s", DGMsg270005, _resource.name);
    }
}

void DGAudio::play() {
    if (_isLoaded && !_isPlaying) {    
        for (int i = 0; i < DGAudioNumberOfBuffers; i++) {
            if (!_stream(_alBuffers[i])) {
                // Raise an error
                
                return;
            }
        }
        
        alSourceQueueBuffers(_alSource, DGAudioNumberOfBuffers, _alBuffers);
        alSourcePlay(_alSource);
        _isPlaying = true;
        
        _verifyError("play");
    }
}

void DGAudio::pause() {
    if (_isPlaying) {
        int queued;
        
        alSourceStop(_alSource);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        
        while (queued--) {
            ALuint buffer;
            alSourceUnqueueBuffers(_alSource, 1, &buffer);
        }
        
        _isPlaying = false;
        
        _verifyError("pause");
    } 
}

void DGAudio::stop() {
    if (_isPlaying) {
        int queued;
        
        alSourceStop(_alSource);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        
        while (queued--) {
            ALuint buffer;
            alSourceUnqueueBuffers(_alSource, 1, &buffer);
        }
        
        ov_raw_seek(&_oggStream, 0);
        _isPlaying = false;
        
        _verifyError("stop");
    }
}

void DGAudio::unload() {
    if (_isLoaded) {
        this->stop();
        
        alDeleteSources(1, &_alSource);
		alDeleteBuffers(1, _alBuffers);
        ov_clear(&_oggStream);
        free(_resource.data);
        
        _isLoaded = false;
    }
}

void DGAudio::update() {
    if (_isPlaying) {
        int processed;
        
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
        
        while (processed--) {
            ALuint buffer;
            
            alSourceUnqueueBuffers(_alSource, 1, &buffer);
            _stream(buffer);
            alSourceQueueBuffers(_alSource, 1, &buffer);
        }
        
        if (_isFading) {
            switch (_fadeDirection) {
                case DGAudioFadeIn:
                    _currentVolume += _fadeStep;
                    if (_currentVolume > _targetVolume)
                        _isFading = false;
                    
                    break;
                case DGAudioFadeOut:
                    _currentVolume -= _fadeStep;
                    if (_currentVolume < _targetVolume)
                        _isFading = false;
                    
                    break;
            }
        }
        else {
            // If the audio is not fading strongly, we check if we must
            // smoothly update the volume. Note this isn't as accurate as above.
            if (_currentVolume < _targetVolume)
                _currentVolume += _volumeFadeStep;
            else if (_currentVolume > _targetVolume)
                _currentVolume -= _volumeFadeStep;
        }
        
        // FIXME: Not very elegant as we're doing this every time
        alSourcef (_alSource, AL_GAIN, _currentVolume);
    }
}

void DGAudio::retain() {
    _retainCount++;
}

void DGAudio::release() {
    _retainCount--;
}

////////////////////////////////////////////////////////////
// Implementation - Private methods
////////////////////////////////////////////////////////////

bool DGAudio::_stream(ALuint buffer) {
    // This is a failsafe; if this is true, we won't attempt
    // to stream anymore
    static bool _hasStreamingError = false;
    
    if (!_hasStreamingError) {
        char data[DGAudioBufferSize];
        int size = 0;
        int section;
        int result;
        
        while (size < DGAudioBufferSize) {
            result = ov_read(&_oggStream, data + size, DGAudioBufferSize - size, 0, 2, 1, &section);
            
            if (result > 0)
                size += result;
            else if (result == 0) {
                // EOF
                if (_isLoopable)
                    ov_raw_seek(&_oggStream, 0);
                else
                    this->stop();
                
                return false;
            }
            else if (result == OV_HOLE) {
                // May return OV_HOLE after we rewind the stream,
                // so we just re-loop
                continue;
            }
            else if (result < 0) {
                // Error
                log->error(DGModAudio, "%s: %s", DGMsg270004, _resource.name);
                _hasStreamingError = true;
                
                return false;
            }
        }
        
        alBufferData(buffer, _alFormat, data, size, _rate);
        
        return true;
    }
    else return false;
}

ALboolean DGAudio::_verifyError(const char* operation) {
   	ALint error = alGetError();
    
	if (error != AL_NO_ERROR) {
		log->error(DGModAudio, "%s: %s (%d)", DGMsg270003, operation, error);
        
		return AL_FALSE;
	}
    
	return AL_TRUE; 
}

// And now... The Vorbisfile callbacks

size_t DGAudio::_oggRead(void* ptr, size_t size, size_t nmemb, void* datasource) {
    DGAudio* audio = (DGAudio*)datasource;
    size_t nSize = size * nmemb;
    
    if ((audio->_resource.dataRead + nSize) > audio->_resource.dataSize)
        nSize = audio->_resource.dataSize - audio->_resource.dataRead;
    
    memcpy(ptr, audio->_resource.data + audio->_resource.dataRead, nSize);
    audio->_resource.dataRead += nSize;
    
	return nSize;
}

int DGAudio::_oggSeek(void* datasource, ogg_int64_t offset, int whence) {
    DGAudio* audio = (DGAudio*)datasource;
    
	switch (whence) {
        case SEEK_SET: 
            audio->_resource.dataRead = (size_t)offset; 
            break;
        case SEEK_CUR: 
            audio->_resource.dataRead += (size_t)offset;
            break;
        case SEEK_END: 
            audio->_resource.dataRead = (size_t)(audio->_resource.dataSize - offset); 
            break;
	}
    
	if (audio->_resource.dataRead > audio->_resource.dataSize) {
		audio->_resource.dataRead = 0;
		return -1;
	}
    
	return 0;
}

int DGAudio::_oggClose(void* datasource) {
    DGAudio* audio = (DGAudio*)datasource;
	audio->_resource.dataRead = 0;
    
	return 0;
}

long DGAudio::_oggTell(void* datasource) {
    DGAudio* audio = (DGAudio*)datasource;
	return (long)audio->_resource.dataRead;
}

/* Copyright (c) 2014-2018 by Mercer Road Corp
*
* Permission to use, copy, modify or distribute this software in binary or source form
* for any purpose is allowed only under explicit prior consent in writing from Mercer Road Corp
*
* THE SOFTWARE IS PROVIDED "AS IS" AND MERCER ROAD CORP DISCLAIMS
* ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL MERCER ROAD CORP
* BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
* DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
* PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
* ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
* SOFTWARE.
*/
#include "vivoxclientapi/audiodeviceid.h"
#include "allocator_utils.h"

namespace VivoxClientApi {
	AudioDeviceId::~AudioDeviceId()
	{
		Deallocate((void*)m_deviceId);
		Deallocate((void*)m_displayName);
	}
    AudioDeviceId::AudioDeviceId() {
        m_deviceId = StrDup("");
		m_displayName = StrDup("");
    }
    AudioDeviceId::AudioDeviceId(const char *device_id, const char *display_name) {
		m_deviceId = StrDup(device_id);
		m_displayName = StrDup(display_name);
    }
	AudioDeviceId::AudioDeviceId(const AudioDeviceId& other)
	{
		m_deviceId = StrDup(other.m_deviceId);
		m_displayName = StrDup(other.m_displayName);
	}
    const char* AudioDeviceId::GetAudioDeviceId() const {
        return m_deviceId;
    }
    const char* AudioDeviceId::GetAudioDeviceDisplayName() const {
        return m_displayName;
    }
    bool AudioDeviceId::operator==(const AudioDeviceId &RHS) const {
        return strcmp(m_deviceId,RHS.m_deviceId) == 0;
    }
    bool AudioDeviceId::operator!=(const AudioDeviceId &RHS) const {
        return !operator==(RHS);
    }
    AudioDeviceId & AudioDeviceId::operator=(const AudioDeviceId &RHS) {
		Deallocate((void*)m_deviceId);
        m_deviceId = StrDup(RHS.m_deviceId);
		Deallocate((void*)m_displayName);
        m_displayName = StrDup(RHS.m_displayName);
        return *this;
    }
    bool AudioDeviceId::operator<(const AudioDeviceId &RHS) const {
        return strcmp(m_deviceId, RHS.m_deviceId) < 0;
    }
    bool AudioDeviceId::IsValid() const {
        return m_deviceId[0] != 0;
    }
}

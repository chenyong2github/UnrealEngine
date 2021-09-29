// Copyright Epic Games, Inc. All Rights Reserved.

/**
	Concrete implementation of FAudioDevice for XAudio2

	See https://msdn.microsoft.com/en-us/library/windows/desktop/hh405049%28v=vs.85%29.aspx
*/

#include "AudioMixerPlatformXAudio2.h"
#include "AudioMixer.h"
#include "AudioDeviceNotificationSubsystem.h"

#if PLATFORM_WINDOWS

#include "ScopedCom.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#define INITGUID
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
THIRD_PARTY_INCLUDES_END

#if !NO_LOGGING
// PSStringFromPropertyKey needs this lib, which we use for logging the Property GUIDs.
#pragma comment(lib, "Propsys.lib")
#endif //!NO_LOGGING

static Audio::EAudioDeviceState ConvertWordToDeviceState(DWORD InWord)
{
	switch (InWord)
	{
	case DEVICE_STATE_ACTIVE: return Audio::EAudioDeviceState::Active;
	case DEVICE_STATE_DISABLED: return Audio::EAudioDeviceState::Disabled;
	case DEVICE_STATE_UNPLUGGED: return Audio::EAudioDeviceState::Unplugged;
	case DEVICE_STATE_NOTPRESENT: return Audio::EAudioDeviceState::NotPresent;
	default:
		break;
	}
	checkNoEntry();
	return Audio::EAudioDeviceState::NotPresent;
}


class FWindowsMMNotificationClient final : public IMMNotificationClient, 
										   public IAudioSessionEvents
{
public:
	FWindowsMMNotificationClient()
		: Ref(1)
		, DeviceEnumerator(nullptr)
	{
		bComInitialized = FWindowsPlatformMisc::CoInitialize();
		HRESULT Result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DeviceEnumerator.Obj));
		if (Result == S_OK)
		{
			DeviceEnumerator->RegisterEndpointNotificationCallback(this);
		}

		// Register for session events from default endpoint.
		if (DeviceEnumerator)
		{
			Audio::TScopeComObject<IMMDevice> DefaultDevice;
			if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &DefaultDevice.Obj)))
			{
				RegisterForSessionNotifications(DefaultDevice);
			}
		}
	}

	bool RegisterForSessionNotifications(const Audio::TScopeComObject<IMMDevice>& InDevice)
	{
		FScopeLock ScopeLock(&MutationCs);

		// If we're already listening to this device, we can early out.
		if (DeviceListeningToSessionEvents == InDevice)
		{
			return true;
		}

		DeviceListeningToSessionEvents = InDevice;
				
		// Unregister for any device we're already listening to.
		if (SessionControls)
		{
			SessionControls->UnregisterAudioSessionNotification(this);
			SessionControls.Reset();
		}
		if (SessionManager)
		{
			SessionManager.Reset();
		}
		
		if(InDevice) 
		{
			if (SUCCEEDED(InDevice->Activate(__uuidof(IAudioSessionManager), CLSCTX_INPROC_SERVER, NULL, (void**)&SessionManager.Obj)))
			{
				if (SUCCEEDED(SessionManager->GetAudioSessionControl(NULL, 0, &SessionControls.Obj)))
				{
					if (SUCCEEDED(SessionControls->RegisterAudioSessionNotification(this)))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	bool RegisterForSessionNotifications(const FString& InDeviceId)
	{
		if (Audio::TScopeComObject<IMMDevice> Device = GetDevice(InDeviceId))
		{
			return RegisterForSessionNotifications(Device);
		}
		return false;
	}
	
	virtual ~FWindowsMMNotificationClient()
	{
		if (SessionControls)
		{
			SessionControls->UnregisterAudioSessionNotification(this);
		}

		if (DeviceEnumerator)
		{
			DeviceEnumerator->UnregisterEndpointNotificationCallback(this);
		}

		if (bComInitialized)
		{
			FWindowsPlatformMisc::CoUninitialize();
		}
	}

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow InFlow, ERole InRole, LPCWSTR pwstrDeviceId) override
	{
		FScopeLock ScopeLock(&MutationCs);

		if (Audio::IAudioMixer::ShouldLogDeviceSwaps())
		{
			UE_LOG(LogAudioMixer, Warning, TEXT("OnDefaultDeviceChanged: %d, %d, %s"), InFlow, InRole, pwstrDeviceId);
		}

		Audio::EAudioDeviceRole AudioDeviceRole;

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		if (InRole == eConsole)
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Console;
		}
		else if (InRole == eMultimedia)
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Multimedia;
		}
		else
		{
			AudioDeviceRole = Audio::EAudioDeviceRole::Communications;
		}

		if (InFlow == eRender)
		{
			for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
			{
				Listener->OnDefaultRenderDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}
		else if (InFlow == eCapture)
		{
			for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
			{
				Listener->OnDefaultCaptureDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}
		else
		{
			for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
			{
				Listener->OnDefaultCaptureDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
				Listener->OnDefaultRenderDeviceChanged(AudioDeviceRole, FString(pwstrDeviceId));
			}
		}


		return S_OK;
	}

	// TODO: Ideally we'd use the cache instead of ask for this.
	bool IsRenderDevice(const FString& InDeviceId) const
	{
		bool bIsRender = true;
		if (Audio::TScopeComObject<IMMDevice> Device = GetDevice(InDeviceId))
		{
			Audio::TScopeComObject<IMMEndpoint> Endpoint;
			if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(&Endpoint.Obj))))
			{
				EDataFlow DataFlow = eRender;
				if (SUCCEEDED(Endpoint->GetDataFlow(&DataFlow)))
				{
					bIsRender = DataFlow == eRender;
				}
			}
		}
		return bIsRender;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override
	{
		FScopeLock ScopeLock(&MutationCs);

		UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Verbose, TEXT("OnDeviceAdded: %s"), *GetFriendlyName(pwstrDeviceId));
			
		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		bool bIsRender = IsRenderDevice(pwstrDeviceId);

		for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
		{
			Listener->OnDeviceAdded(FString(pwstrDeviceId), bIsRender);
		}
		return S_OK;
	};

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override
	{
		FScopeLock ScopeLock(&MutationCs);

		UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(),LogAudioMixer, Verbose, TEXT("OnDeviceRemoved: %s"), *GetFriendlyName(pwstrDeviceId));

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		bool bIsRender = IsRenderDevice(pwstrDeviceId);
		for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
		{
			Listener->OnDeviceRemoved(FString(pwstrDeviceId), bIsRender);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override
	{
		FScopeLock ScopeLock(&MutationCs);

		UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Verbose, TEXT("OnDeviceStateChanged: %s, %d"), *GetFriendlyName(pwstrDeviceId), dwNewState);

		if (Audio::IAudioMixer::ShouldIgnoreDeviceSwaps())
		{
			return S_OK;
		}

		bool bIsRender = IsRenderDevice(pwstrDeviceId);
		if (dwNewState == DEVICE_STATE_ACTIVE || dwNewState == DEVICE_STATE_DISABLED || dwNewState == DEVICE_STATE_UNPLUGGED || dwNewState == DEVICE_STATE_NOTPRESENT)
		{
			Audio::EAudioDeviceState State = ConvertWordToDeviceState(dwNewState);
						
			for (Audio::IAudioMixerDeviceChangedListener* Listener : Listeners)
			{
				Listener->OnDeviceStateChanged(FString(pwstrDeviceId), State, bIsRender);
			}
		}
		return S_OK;
	}

	FString PropToString(const PROPERTYKEY Key)
	{
#if !NO_LOGGING
		#define IF_PROP_STRING(PROP) if(Key.fmtid == PROP.fmtid) return TEXT(#PROP)

		IF_PROP_STRING(PKEY_AudioEndpoint_PhysicalSpeakers);
		IF_PROP_STRING(PKEY_AudioEngine_DeviceFormat);
		IF_PROP_STRING(PKEY_AudioEngine_OEMFormat);
		IF_PROP_STRING(PKEY_AudioEndpoint_Association);
		IF_PROP_STRING(PKEY_AudioEndpoint_ControlPanelPageProvider);
		IF_PROP_STRING(PKEY_AudioEndpoint_Disable_SysFx);
		IF_PROP_STRING(PKEY_AudioEndpoint_FormFactor);
		IF_PROP_STRING(PKEY_AudioEndpoint_FullRangeSpeakers);
		IF_PROP_STRING(PKEY_AudioEndpoint_GUID);
		IF_PROP_STRING(PKEY_AudioEndpoint_Supports_EventDriven_Mode);

		#undef IF_PROP_STRING

		TCHAR KeyString[PKEYSTR_MAX];
		HRESULT HR = PSStringFromPropertyKey(Key, KeyString, ARRAYSIZE(KeyString));
		if(SUCCEEDED(HR))
				{
			return FString(KeyString);
				}
#endif //!NO_LOGGING
		return TEXT("Unknown");
			}

	FString GetFriendlyName(const FString InDeviceID)
	{
		if (InDeviceID.IsEmpty())
		{
			return TEXT("System Default");
		}

		FString FriendlyName = TEXT("[No Friendly Name for Device]");
			
		// Get device.
		if (Audio::TScopeComObject<IMMDevice> Device = GetDevice(*InDeviceID))
		{
			// Get property store.
			Audio::TScopeComObject<IPropertyStore> PropStore;
			HRESULT Hr = Device->OpenPropertyStore(STGM_READ, &PropStore.Obj);

			// Get friendly name.
			if (SUCCEEDED(Hr) && PropStore)
	{
				PROPVARIANT PropString;
				PropVariantInit(&PropString);

				// Get the endpoint device's friendly-name property.
				Hr = PropStore->GetValue(PKEY_Device_FriendlyName, &PropString);
				if (SUCCEEDED(Hr))
				{
					// Copy friendly name.
					if (PropString.pwszVal)
		{
						FriendlyName = PropString.pwszVal;
					}
				}

				PropVariantClear(&PropString);
			}
		}
		return FriendlyName;
		}

	Audio::TScopeComObject<IMMDevice> GetDevice(const FString InDeviceID) const
	{
		// Get device.
		Audio::TScopeComObject<IMMDevice> Device;
		HRESULT Hr = DeviceEnumerator->GetDevice(*InDeviceID, &Device.Obj);
		if (SUCCEEDED(Hr))
		{
			return Device;
		}
		
		// Fail.
		return {};
		}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
	{
		UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Verbose, TEXT("OnPropertyValueChanged: %s : %s"), *GetFriendlyName(pwstrDeviceId), *PropToString(key));
		
		if (key.fmtid == PKEY_AudioEngine_DeviceFormat.fmtid ||
			/*key.fmtid == PKEY_AudioEngine_OEMFormat.fmtid ||*/
			key.fmtid == PKEY_AudioEndpoint_PhysicalSpeakers.fmtid )
		{
			// Get device.
			FString DeviceId = pwstrDeviceId;
			Audio::TScopeComObject<IMMDevice> Device;			
			HRESULT Hr = DeviceEnumerator->GetDevice(*DeviceId, &Device.Obj);

			// Get property store.
			Audio::TScopeComObject<IPropertyStore> PropertyStore;
			if (SUCCEEDED(Hr) && Device)
			{
				Hr = Device->OpenPropertyStore(STGM_READ, &PropertyStore.Obj);
				if (SUCCEEDED(Hr) && PropertyStore)
				{
					// Device Format
					PROPVARIANT Prop;
					PropVariantInit(&Prop);

					if (key.fmtid == PKEY_AudioEngine_DeviceFormat.fmtid ||
			key.fmtid == PKEY_AudioEngine_OEMFormat.fmtid)
		{
						// WAVEFORMATEX blobs.
						if (SUCCEEDED(PropertyStore->GetValue(key, &Prop)) && Prop.blob.pBlobData)
						{
							const WAVEFORMATEX* WaveFormatEx = (const WAVEFORMATEX*)(Prop.blob.pBlobData);

							Audio::IAudioMixerDeviceChangedListener::FFormatChangedData FormatChanged;
							FormatChanged.NumChannels = FMath::Clamp((int32)WaveFormatEx->nChannels, 2, 8);
							FormatChanged.SampleRate = WaveFormatEx->nSamplesPerSec;
							FormatChanged.ChannelConfig = WaveFormatEx->wFormatTag == WAVE_FORMAT_EXTENSIBLE ?
								((const WAVEFORMATEXTENSIBLE*)WaveFormatEx)->dwChannelMask : 0;

							FScopeLock ScopeLock(&MutationCs);
							for (Audio::IAudioMixerDeviceChangedListener* i : Listeners)
							{
								i->OnFormatChanged(DeviceId, FormatChanged);
							}
						}
					}
					else if(key.fmtid == PKEY_AudioEndpoint_PhysicalSpeakers.fmtid)
					{
						if (SUCCEEDED(PropertyStore->GetValue(PKEY_AudioEndpoint_PhysicalSpeakers, &Prop)))
						{
							FScopeLock ScopeLock(&MutationCs);
							for (Audio::IAudioMixerDeviceChangedListener* i : Listeners)
			{
								i->OnSpeakerConfigChanged(DeviceId, Prop.uintVal);
							}
						}
					}
					PropVariantClear(&Prop);
				}
			}
		}
		return S_OK;
			}

	HRESULT STDMETHODCALLTYPE QueryInterface(const IID& IId, void** UnknownPtrPtr) override
	{
		// Three rules of QueryInterface: https://docs.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface
		// 1. Objects must have identity.
		// 2. The set of interfaces on an object instance must be static.
		// 3. It must be possible to query successfully for any interface on an object from any other interface.

		// If ppvObject(the address) is nullptr, then this method returns E_POINTER.
		if (!UnknownPtrPtr)
		{
			return E_POINTER;
		}

		// https://docs.microsoft.com/en-us/windows/win32/com/implementing-reference-counting
		// Whenever a client calls a method(or API function), such as QueryInterface, that returns a new interface pointer, 
		// the method being called is responsible for incrementing the reference count through the returned pointer.
		// For example, when a client first creates an object, it receives an interface pointer to an object that, 
		// from the client's point of view, has a reference count of one. If the client then calls AddRef on the interface pointer, 
		// the reference count becomes two. The client must call Release twice on the interface pointer to drop all of its references to the object.
		if (IId == __uuidof(IMMNotificationClient) || IId == __uuidof(IUnknown))
		{
			*UnknownPtrPtr = (IMMNotificationClient*)(this);
			AddRef();
		return S_OK;
	}
		else if (IId == __uuidof(IAudioSessionEvents))
	{
			*UnknownPtrPtr = (IAudioSessionEvents*)this;
			AddRef();
		return S_OK;
	}


		// This method returns S_OK if the interface is supported, and E_NOINTERFACE otherwise.
		*UnknownPtrPtr = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&Ref);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG ulRef = InterlockedDecrement(&Ref);
		if (0 == ulRef)
		{
			delete this;
		}
		return ulRef;
	}

	void RegisterDeviceChangedListener(Audio::IAudioMixerDeviceChangedListener* DeviceChangedListener)
	{
		FScopeLock ScopeLock(&MutationCs);
		Listeners.Add(DeviceChangedListener);
	}

	void UnRegisterDeviceDeviceChangedListener(Audio::IAudioMixerDeviceChangedListener* DeviceChangedListener)
	{
		FScopeLock ScopeLock(&MutationCs);
		Listeners.Remove(DeviceChangedListener);
	}

	// Begin IAudioSessionEvents
	HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(
		LPCWSTR NewDisplayName,
		LPCGUID EventContext) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnIconPathChanged(
		LPCWSTR NewIconPath,
		LPCGUID EventContext) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(
		float NewVolume,
		BOOL NewMute,
		LPCGUID EventContext) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(
		DWORD ChannelCount,
		float NewChannelVolumeArray[],
		DWORD ChangedChannel,
		LPCGUID EventContext) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(
		LPCGUID NewGroupingParam,
		LPCGUID EventContext) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnStateChanged(
		AudioSessionState NewState) override
	{		
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnSessionDisconnected(
		AudioSessionDisconnectReason InDisconnectReason)
	{
		UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Log, TEXT("Session Disconnect: %s"), GetDisconnectReasonString(InDisconnectReason));

		Audio::IAudioMixerDeviceChangedListener::EDisconnectReason Reason = AudioSessionDisconnectToEDisconnectReason(InDisconnectReason);
		FScopeLock ScopeLock(&MutationCs);
		for (Audio::IAudioMixerDeviceChangedListener* i : Listeners)
		{
			i->OnSessionDisconnect(Reason);
		}
		return S_OK;
	}
	// End IAudioSessionEvents

	static const TCHAR* GetDisconnectReasonString(AudioSessionDisconnectReason InDisconnectReason)
	{
		#define CASE_TO_STRING(X) case X: return TEXT(#X)
		switch (InDisconnectReason)
		{
			CASE_TO_STRING(DisconnectReasonDeviceRemoval);
			CASE_TO_STRING(DisconnectReasonServerShutdown);
			CASE_TO_STRING(DisconnectReasonFormatChanged);
			CASE_TO_STRING(DisconnectReasonSessionLogoff);
			CASE_TO_STRING(DisconnectReasonSessionDisconnected);
			CASE_TO_STRING(DisconnectReasonExclusiveModeOverride);
			default:
				checkNoEntry();
				break;
		}
		return TEXT("Unknown");
		#undef CASE_TO_STRING
	}
	
	static Audio::IAudioMixerDeviceChangedListener::EDisconnectReason 
	AudioSessionDisconnectToEDisconnectReason(AudioSessionDisconnectReason InDisconnectReason)
	{
		using namespace Audio;
		switch (InDisconnectReason)
		{
		case DisconnectReasonDeviceRemoval:			return IAudioMixerDeviceChangedListener::EDisconnectReason::DeviceRemoval;
		case DisconnectReasonServerShutdown:		return IAudioMixerDeviceChangedListener::EDisconnectReason::ServerShutdown;
		case DisconnectReasonFormatChanged:			return IAudioMixerDeviceChangedListener::EDisconnectReason::FormatChanged;
		case DisconnectReasonSessionLogoff:			return IAudioMixerDeviceChangedListener::EDisconnectReason::SessionLogoff;
		case DisconnectReasonSessionDisconnected:	return IAudioMixerDeviceChangedListener::EDisconnectReason::SessionDisconnected;
		case DisconnectReasonExclusiveModeOverride:	return IAudioMixerDeviceChangedListener::EDisconnectReason::ExclusiveModeOverride;
		default: break;
		}

		checkNoEntry();
		return IAudioMixerDeviceChangedListener::EDisconnectReason::DeviceRemoval;
	}
		
private:
	LONG Ref;
	TSet<Audio::IAudioMixerDeviceChangedListener*> Listeners;
	FCriticalSection MutationCs;
	Audio::TScopeComObject<IMMDeviceEnumerator> DeviceEnumerator;
	Audio::TScopeComObject<IAudioSessionManager> SessionManager;
	Audio::TScopeComObject<IAudioSessionControl> SessionControls;
	Audio::TScopeComObject<IMMDevice> DeviceListeningToSessionEvents;
	bool bComInitialized;
};

namespace Audio
{	
	TSharedPtr<FWindowsMMNotificationClient> WindowsNotificationClient;

	void RegisterForSessionEvents(const FString& InDeviceId)
	{
		if (WindowsNotificationClient)
		{
			WindowsNotificationClient->RegisterForSessionNotifications(InDeviceId);
		}
	}

	struct FWindowsMMDeviceCache : IAudioMixerDeviceChangedListener, IAudioPlatformDeviceInfoCache
	{
		struct FCacheEntry
		{
			enum class EEndpointType { Unknown, Render, Capture};

			FName DeviceId;							// Key
			FString FriendlyName;
			FString DeviceFriendlyName;
			EAudioDeviceState State;
			int32 NumChannels = 0;
			int32 SampleRate = 0;
			EEndpointType Type = EEndpointType::Unknown;
			uint32 SpeakerConfig = 0;				// Bitfield used to build output channels, for easy comparison.

			TArray<EAudioMixerChannel::Type> OutputChannels;	// TODO. Generate this from the ChannelNum and bitmask when we are asked for it.
			mutable FRWLock MutationLock;

			FCacheEntry& operator=(const FCacheEntry& InOther)
{
				// Copy everything but the lock. 
				DeviceId = InOther.DeviceId;
				FriendlyName = InOther.FriendlyName;
				DeviceFriendlyName = InOther.DeviceFriendlyName;
				State = InOther.State;
				NumChannels = InOther.NumChannels;
				SampleRate = InOther.SampleRate;
				Type = InOther.Type;
				SpeakerConfig = InOther.SpeakerConfig;
				OutputChannels = InOther.OutputChannels;
				return *this;
			}

			FCacheEntry& operator=(FCacheEntry&& InOther)
			{
				DeviceId = MoveTemp(InOther.DeviceId);
				FriendlyName = MoveTemp(InOther.FriendlyName);
				DeviceFriendlyName = MoveTemp(InOther.DeviceFriendlyName);
				State = MoveTemp(InOther.State);
				NumChannels = MoveTemp(InOther.NumChannels);
				SampleRate = MoveTemp(InOther.SampleRate);
				Type = MoveTemp(InOther.Type);
				SpeakerConfig = MoveTemp(InOther.SpeakerConfig);
				OutputChannels = MoveTemp(InOther.OutputChannels);
				return *this;
			}

			FCacheEntry(const FCacheEntry& InOther)
			{
				*this = InOther;
			}

			FCacheEntry(FCacheEntry&& InOther)
			{
				*this = MoveTemp(InOther);
			}

			FCacheEntry(const FString& InDeviceId)
				: DeviceId{ InDeviceId }
			{}
		};
		
		TScopeComObject<IMMDeviceEnumerator> DeviceEnumerator;
		
		mutable FRWLock CacheMutationLock;							// R/W lock protects map and default arrays.
		TMap<FName, FCacheEntry> Cache;								// DeviceID GUID -> Info.
		FName DefaultCaptureId[(int32)EAudioDeviceRole::COUNT];		// Role -> DeviceID GUID
		FName DefaultRenderId[(int32)EAudioDeviceRole::COUNT];		// Role -> DeviceID GUID

		FWindowsMMDeviceCache()
		{
			ensure(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DeviceEnumerator.Obj))) && DeviceEnumerator);

			EnumerateEndpoints();
			EnumerateDefaults();
		}
		virtual ~FWindowsMMDeviceCache() = default;

		bool EnumSpeakerMask(uint32 InMask, FCacheEntry& OutInfo)
		{
			// Loop through the extensible format channel flags in the standard order and build our output channel array
			// From https://msdn.microsoft.com/en-us/library/windows/hardware/dn653308(v=vs.85).aspx
			// The channels in the interleaved stream corresponding to these spatial positions must appear in the order specified above. This holds true even in the 
			// case of a non-contiguous subset of channels. For example, if a stream contains left, bass enhance and right, then channel 1 is left, channel 2 is right, 
			// and channel 3 is bass enhance. This enables the linkage of multi-channel streams to well-defined multi-speaker configurations.

			static const uint32 ChannelTypeMap[EAudioMixerChannel::ChannelTypeCount] =
			{
				SPEAKER_FRONT_LEFT,
				SPEAKER_FRONT_RIGHT,
				SPEAKER_FRONT_CENTER,
				SPEAKER_LOW_FREQUENCY,
				SPEAKER_BACK_LEFT,
				SPEAKER_BACK_RIGHT,
				SPEAKER_FRONT_LEFT_OF_CENTER,
				SPEAKER_FRONT_RIGHT_OF_CENTER,
				SPEAKER_BACK_CENTER,
				SPEAKER_SIDE_LEFT,
				SPEAKER_SIDE_RIGHT,
				SPEAKER_TOP_CENTER,
				SPEAKER_TOP_FRONT_LEFT,
				SPEAKER_TOP_FRONT_CENTER,
				SPEAKER_TOP_FRONT_RIGHT,
				SPEAKER_TOP_BACK_LEFT,
				SPEAKER_TOP_BACK_CENTER,
				SPEAKER_TOP_BACK_RIGHT,
				SPEAKER_RESERVED,
			};

			OutInfo.SpeakerConfig = InMask;

			// No need to enumerate speakers for capture devices.
			if (OutInfo.Type == FCacheEntry::EEndpointType::Capture)
			{
				return true;
			}

			uint32 ChanCount = 0;
			for (uint32 ChannelTypeIndex = 0; ChannelTypeIndex < EAudioMixerChannel::ChannelTypeCount && ChanCount < (uint32)OutInfo.NumChannels; ++ChannelTypeIndex)
			{
				if (InMask & ChannelTypeMap[ChannelTypeIndex])
				{
					OutInfo.OutputChannels.Add((EAudioMixerChannel::Type)ChannelTypeIndex);
					++ChanCount;
				}
			}

			// We didn't match channel masks for all channels, revert to a default ordering
			if (ChanCount < (uint32)OutInfo.NumChannels)
			{
				UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Warning, TEXT("Did not find the channel type flags for audio device '%s'. Reverting to a default channel ordering."), *OutInfo.FriendlyName);

				OutInfo.OutputChannels.Reset();

				static const EAudioMixerChannel::Type DefaultChannelOrdering[] = {
					EAudioMixerChannel::FrontLeft,
					EAudioMixerChannel::FrontRight,
					EAudioMixerChannel::FrontCenter,
					EAudioMixerChannel::LowFrequency,
					EAudioMixerChannel::SideLeft,
					EAudioMixerChannel::SideRight,
					EAudioMixerChannel::BackLeft,
					EAudioMixerChannel::BackRight,
				};

				const EAudioMixerChannel::Type* ChannelOrdering = DefaultChannelOrdering;

				// Override channel ordering for some special cases
				if (OutInfo.NumChannels == 4)
				{
					static EAudioMixerChannel::Type DefaultChannelOrderingQuad[] = {
						EAudioMixerChannel::FrontLeft,
						EAudioMixerChannel::FrontRight,
						EAudioMixerChannel::BackLeft,
						EAudioMixerChannel::BackRight,
					};

					ChannelOrdering = DefaultChannelOrderingQuad;
				}
				else if (OutInfo.NumChannels == 6)
				{
					static const EAudioMixerChannel::Type DefaultChannelOrdering51[] = {
						EAudioMixerChannel::FrontLeft,
						EAudioMixerChannel::FrontRight,
						EAudioMixerChannel::FrontCenter,
						EAudioMixerChannel::LowFrequency,
						EAudioMixerChannel::BackLeft,
						EAudioMixerChannel::BackRight,
					};

					ChannelOrdering = DefaultChannelOrdering51;
				}

				check(OutInfo.NumChannels <= 8);
				for (int32 Index = 0; Index < OutInfo.NumChannels; ++Index)
				{
					OutInfo.OutputChannels.Add(ChannelOrdering[Index]);
				}
			}
			return true;
		}
		
		bool EnumerateSpeakers(const WAVEFORMATEX* InFormat, FCacheEntry& OutInfo)
		{
			OutInfo.OutputChannels.Empty();

			// Extensible format supports surround sound so we need to parse the channel configuration to build our channel output array
			if (InFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
			{
				// Cast to the extensible format to get access to extensible data
				const WAVEFORMATEXTENSIBLE* WaveFormatExtensible = (const WAVEFORMATEXTENSIBLE*)(InFormat);
				return EnumSpeakerMask(WaveFormatExtensible->dwChannelMask, OutInfo);
			}
			else
			{
				// Non-extensible formats only support mono or stereo channel output
				OutInfo.OutputChannels.Add(EAudioMixerChannel::FrontLeft);
				if (OutInfo.NumChannels == 2)
				{
					OutInfo.OutputChannels.Add(EAudioMixerChannel::FrontRight);
				}
			}

			// Aways success for now.
			return true;
		}

		FCacheEntry::EEndpointType QueryDeviceDataFlow(const Audio::TScopeComObject<IMMDevice>& InDevice) const
		{
			Audio::TScopeComObject<IMMEndpoint> Endpoint;
			if (SUCCEEDED(InDevice->QueryInterface(IID_PPV_ARGS(&Endpoint.Obj))))
			{
				EDataFlow DataFlow = eRender;
				if (SUCCEEDED(Endpoint->GetDataFlow(&DataFlow)))
				{
					switch (DataFlow)
					{
					case eRender: 
						return FCacheEntry::EEndpointType::Render;
					case eCapture: 
						return FCacheEntry::EEndpointType::Capture;
					default:
						break;
					}
				}
			}
			return FCacheEntry::EEndpointType::Unknown;
		}

		bool EnumerateDeviceProps(const Audio::TScopeComObject<IMMDevice>& InDevice, FCacheEntry& OutInfo)
		{
			// Mark if this is a Render Device or Capture or Unknown.
			OutInfo.Type = QueryDeviceDataFlow(InDevice);

			// Also query the device state.
			DWORD DeviceState = DEVICE_STATE_NOTPRESENT;
			if (SUCCEEDED(InDevice->GetState(&DeviceState)))
			{
				OutInfo.State = ConvertWordToDeviceState(DeviceState);
			}

			Audio::TScopeComObject<IPropertyStore> PropertyStore;
			if (SUCCEEDED(InDevice->OpenPropertyStore(STGM_READ, &PropertyStore.Obj)))
			{
				// Friendly Name
				PROPVARIANT FriendlyName;
				PropVariantInit(&FriendlyName);
				if (SUCCEEDED(PropertyStore->GetValue(PKEY_Device_FriendlyName, &FriendlyName)) && FriendlyName.pwszVal)
				{
					OutInfo.FriendlyName = FString(FriendlyName.pwszVal);
					PropVariantClear(&FriendlyName);
				}

				auto EnumDeviceFormat = [this](const Audio::TScopeComObject<IPropertyStore>& InPropStore, REFPROPERTYKEY InKey, FCacheEntry& OutInfo) -> bool
				{
					// Device Format
					PROPVARIANT DeviceFormat;
					PropVariantInit(&DeviceFormat);
					
					if (SUCCEEDED(InPropStore->GetValue(InKey, &DeviceFormat)) && DeviceFormat.blob.pBlobData)
					{
						const WAVEFORMATEX* WaveFormatEx = (const WAVEFORMATEX*)(DeviceFormat.blob.pBlobData);
						OutInfo.NumChannels = FMath::Clamp((int32)WaveFormatEx->nChannels, 2, 8);
						OutInfo.SampleRate = WaveFormatEx->nSamplesPerSec;

						EnumerateSpeakers(WaveFormatEx, OutInfo);
						PropVariantClear(&DeviceFormat);
						return true;
					}
					return false;
				};

				if (EnumDeviceFormat(PropertyStore, PKEY_AudioEngine_DeviceFormat, OutInfo) ||
					EnumDeviceFormat(PropertyStore, PKEY_AudioEngine_OEMFormat, OutInfo))
				{
				}
				else
				{
					// Log a warning if this device is active as we failed to ask for a format
					UE_CLOG(DeviceState == DEVICE_STATE_ACTIVE, LogAudioMixer, Warning, TEXT("Failed to get Format for active device '%s'"), *OutInfo.FriendlyName);
				}
			}
	
			// Aways success for now.
			return true;
		}

		void EnumerateEndpoints()
		{
			// Build a new cache from scratch.
			TMap<FName, FCacheEntry> NewCache;

			// Get Device Enumerator.
			if (DeviceEnumerator)
			{
				// Get Render Device Collection. (note we ask for ALL states, which include disabled/unplugged devices.).
				Audio::TScopeComObject<IMMDeviceCollection> DeviceCollection;
				uint32 DeviceCount = 0;
				if (SUCCEEDED(DeviceEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &DeviceCollection.Obj)) && DeviceCollection &&
					SUCCEEDED(DeviceCollection->GetCount(&DeviceCount)))
				{
					for (uint32 i = 0; i < DeviceCount; ++i)
					{
						Audio::TScopeComObject<IMMDevice> Device;
						if (SUCCEEDED(DeviceCollection->Item(i, &Device.Obj)) && Device)
						{
							// Get the device id string (guid)
							Audio::FScopeComString DeviceIdString;
							if (SUCCEEDED(Device->GetId(&DeviceIdString.StringPtr)) && DeviceIdString)
							{
								FCacheEntry Info{ DeviceIdString.Get() };

								// Enumerate props into our info object.
								EnumerateDeviceProps(Device, Info);

								UE_LOG(LogAudioMixer, Verbose, TEXT("%s Device '%s' ID='%s'"),
									Info.Type == FCacheEntry::EEndpointType::Capture ? TEXT("Capture") :
									Info.Type == FCacheEntry::EEndpointType::Render ? TEXT("Render") :
									TEXT("UNKNOWN!"),
									*Info.DeviceId.ToString(),
									*Info.FriendlyName
								);

								check(!NewCache.Contains(Info.DeviceId));
								NewCache.Emplace(Info.DeviceId, Info);
							}
						}
					}
				}

				// Finally, Replace cache with new one.
				{
					FWriteScopeLock Lock(CacheMutationLock);
					Cache = MoveTemp(NewCache);
				}
			}
		}			

		void EnumerateDefaults()
		{
			auto GetDefaultDeviceID = [this](EDataFlow InDataFlow, ERole InRole, FName& OutDeviceId) -> bool
			{
				// Mark default device.
				bool bSuccess = false;
				TScopeComObject<IMMDevice> DefaultDevice;
				if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(InDataFlow, InRole, &DefaultDevice.Obj)))
				{
					Audio::FScopeComString DeviceIdString;
					if (SUCCEEDED(DefaultDevice->GetId(&DeviceIdString.StringPtr)) && DeviceIdString)
					{	
						OutDeviceId = DeviceIdString.Get();
						bSuccess = true;
					}
				}
				return bSuccess;
			};

			// Get defaults (render, capture).
			FWriteScopeLock Lock(CacheMutationLock);
			static_assert((int32)EAudioDeviceRole::COUNT == ERole_enum_count, "EAudioDeviceRole should be the same as ERole");
			for (int32 i = 0; i < ERole_enum_count; ++i)
			{
				FName DeviceIdName;
				if (GetDefaultDeviceID(eRender, static_cast<ERole>(i), DeviceIdName))
				{
					DefaultRenderId[i] = DeviceIdName;
				}
				if (GetDefaultDeviceID(eCapture, static_cast<ERole>(i), DeviceIdName))
				{
					DefaultCaptureId[i] = DeviceIdName;
				}
			}
		}

		void OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) override
		{
			FWriteScopeLock WriteLock(CacheMutationLock);
			check(InAudioDeviceRole < EAudioDeviceRole::COUNT);
			DefaultCaptureId[(int32)InAudioDeviceRole] = *DeviceId;
		}

		void OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) override
		{
			FWriteScopeLock WriteLock(CacheMutationLock);
			check(InAudioDeviceRole < EAudioDeviceRole::COUNT);
			DefaultRenderId[(int32)InAudioDeviceRole] = *DeviceId;
		}

		void OnDeviceAdded(const FString& DeviceId, bool bIsRender) override
		{
			if (ensure(DeviceEnumerator))
			{
				Audio::TScopeComObject<IMMDevice> Device;
				if(SUCCEEDED(DeviceEnumerator->GetDevice(*DeviceId, &Device.Obj)))
				{
					FCacheEntry Info{ *DeviceId };
					if (EnumerateDeviceProps(Device, Info))
					{
						FWriteScopeLock WriteLock(CacheMutationLock);
						check(!Cache.Contains(Info.DeviceId));
						Cache.Add(Info.DeviceId, Info);
					}
				}
			}
		}

		void OnDeviceRemoved(const FString& DeviceId, bool) override
		{
			FWriteScopeLock WriteLock(CacheMutationLock);
			FName DeviceIdName = *DeviceId;
			check(Cache.Contains(DeviceIdName));
			Cache.Remove(DeviceIdName);
		}

		TOptional<FCacheEntry> BuildCacheEntry(const FString& DeviceId)
		{
			Audio::TScopeComObject<IMMDevice> Device;
			if (SUCCEEDED(DeviceEnumerator->GetDevice(*DeviceId, &Device.Obj)))
			{
				FCacheEntry Info{ *DeviceId };
				if (EnumerateDeviceProps(Device, Info))
				{
					return Info;
				}
			}
			return {};
		}

		void OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState, bool ) override
		{
			TOptional<FCacheEntry> Info = BuildCacheEntry(DeviceId);
			
			UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(), LogAudioMixer, Verbose, TEXT("Device '%s' - '%s' state changed."), Info ? *Info->FriendlyName : TEXT("Unknown"), *DeviceId);
			
			FReadScopeLock ReadLock(CacheMutationLock);
			FName DeviceIdName = *DeviceId;
			ensureMsgf(Cache.Contains(DeviceIdName), TEXT("Expecting to find '%s' in cache '%s'"), *DeviceId, Info ? *Info->FriendlyName : TEXT("Unknown"));

			if (FCacheEntry* Entry = Cache.Find(DeviceIdName))
			{
				FWriteScopeLock WriteLock(Entry->MutationLock);
				Entry->State = InState;
			}
		}

		void OnFormatChanged(const FString& InDeviceId, const FFormatChangedData& InFormat) override
		{
			FName DeviceName(InDeviceId);
			bool bDirty = false;
		
			{
				FReadScopeLock ReadLock(CacheMutationLock);
				if (FCacheEntry* Found = Cache.Find(DeviceName))
				{
					if (Found->NumChannels != InFormat.NumChannels)
					{
						FWriteScopeLock WriteLock(Found->MutationLock);
						UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(),LogAudioMixer, Verbose, TEXT("Device '%s' changed default format from %d channels to %d."), *InDeviceId, Found->NumChannels, InFormat.NumChannels);
						Found->NumChannels = InFormat.NumChannels;
						bDirty = true;
					}
					if (Found->SampleRate != InFormat.SampleRate)
					{
						FWriteScopeLock WriteLock(Found->MutationLock);
						UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(),LogAudioMixer, Verbose, TEXT("Device '%s' changed default format from %dhz to %dhz."), *InDeviceId, Found->SampleRate, InFormat.SampleRate);
						Found->SampleRate = InFormat.SampleRate;
						bDirty = true;
					}
					if (Found->SpeakerConfig != InFormat.ChannelConfig)
					{
						FWriteScopeLock WriteLock(Found->MutationLock);
						UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(),LogAudioMixer, Verbose, TEXT("Device '%s' changed default format from 0x%x to 0x%x"), *InDeviceId, Found->SpeakerConfig, InFormat.ChannelConfig);
						Found->SpeakerConfig = InFormat.ChannelConfig;
						bDirty = true;
					}
				}
			}
		}

		void OnSpeakerConfigChanged(const FString& InDeviceId, uint32 InSpeakerBitmask ) override
		{
			FName DeviceName(InDeviceId);
			FReadScopeLock ReadLock(CacheMutationLock);

			if (FCacheEntry* Found = Cache.Find(DeviceName))
			{
				bool bDirty = false;
				if (Found->SpeakerConfig != InSpeakerBitmask)
				{
					FWriteScopeLock WriteLock(Found->MutationLock);
					
					UE_CLOG(Audio::IAudioMixer::ShouldLogDeviceSwaps(),LogAudioMixer, Verbose, TEXT("Device '%s' changed default format from 0x%x to 0x%x"),
						*InDeviceId, Found->SpeakerConfig, InSpeakerBitmask);
					Found->SpeakerConfig = InSpeakerBitmask;

					// Update speaker array based on new bit mask. (maybe we just do this when have successfully changed).
					EnumSpeakerMask(InSpeakerBitmask, *Found);
					
					bDirty = true;
				}			
			}
		}

		void MakeDeviceInfo(const FCacheEntry& InEntry, FAudioPlatformDeviceInfo& OutInfo) const
		{
			OutInfo.Reset();
			OutInfo.Name				= InEntry.FriendlyName;
			OutInfo.DeviceId			= InEntry.DeviceId.GetPlainNameString();
			OutInfo.NumChannels			= InEntry.NumChannels;
			OutInfo.SampleRate			= InEntry.SampleRate;
			OutInfo.OutputChannelArray	= InEntry.OutputChannels;
			OutInfo.Format				= EAudioMixerStreamDataFormat::Float;
			
			{
				FReadScopeLock Lock(CacheMutationLock);
				bool bConsoleRenderDefault = DefaultRenderId[(int32)EAudioDeviceRole::Console] == InEntry.DeviceId;
				bool bMultimediaRenderDefault = DefaultRenderId[(int32)EAudioDeviceRole::Multimedia] == InEntry.DeviceId;
				OutInfo.bIsSystemDefault = bConsoleRenderDefault || bMultimediaRenderDefault;
			}
		}
	
		virtual TArray<FAudioPlatformDeviceInfo> GetAllActiveOutputDevices() const override
		{
			SCOPED_NAMED_EVENT(FWindowsMMDeviceCache_GetAllActiveOutputDevices, FColor::Blue);

			// Find all active devices.
			TArray<FAudioPlatformDeviceInfo> ActiveDevices;
	
			// Read lock on map.
			FReadScopeLock MapReadLock(CacheMutationLock);
			ActiveDevices.Reserve(Cache.Num());

			// Walk cache, read lock for each entry.
			for (const auto& i : Cache)
			{
				// Read lock on each entry.
				FReadScopeLock CacheEntryReadLock(i.Value.MutationLock);
				if (i.Value.State == EAudioDeviceState::Active && 
					i.Value.Type == FCacheEntry::EEndpointType::Render )
				{
					FAudioPlatformDeviceInfo& Info = ActiveDevices.Emplace_GetRef();
					
					// Note: This opens another read lock to pull defaults, but we already own the lock.
					MakeDeviceInfo(i.Value, Info);
				}
			}

			// RVO
			return ActiveDevices;
		}

		FName GetDefaultOutputDevice() const
		{
			FReadScopeLock MapReadLock(CacheMutationLock);
			if( !DefaultRenderId[(int32)EAudioDeviceRole::Multimedia].IsNone())
			{
				return DefaultRenderId[(int32)EAudioDeviceRole::Multimedia];
			}
			if (!DefaultRenderId[(int32)EAudioDeviceRole::Console].IsNone())
			{
				return DefaultRenderId[(int32)EAudioDeviceRole::Console];
			}
			return NAME_None;
		}
		
		TOptional<FAudioPlatformDeviceInfo> FindDefaultOutputDevice() const override
		{
			return FindActiveOutputDevice(NAME_None);
		}

		TOptional<FAudioPlatformDeviceInfo> FindActiveOutputDevice(FName InDeviceID) const override
		{
			SCOPED_NAMED_EVENT(FWindowsMMDeviceCache_FindActiveOutputDevice, FColor::Blue);

			FReadScopeLock MapReadLock(CacheMutationLock);
			
			// Asking for Default?
			if (InDeviceID.IsNone())
			{
				InDeviceID = GetDefaultOutputDevice();
				if (InDeviceID.IsNone())
				{
					// No default set, fail.
					return {};
				}
			}

			// Find entry matching that device ID.
			if (const FCacheEntry* Found = Cache.Find(InDeviceID))
			{
				FReadScopeLock EntryReadLock(Found->MutationLock);
				if (Found->State == EAudioDeviceState::Active && 
					Found->Type == FCacheEntry::EEndpointType::Render )
				{
					FAudioPlatformDeviceInfo Info;
					MakeDeviceInfo(*Found, Info);
					return Info;
				}
			}
			// Fail.
			return {};
		}
	};

	void FMixerPlatformXAudio2::RegisterDeviceChangedListener()
	{
		if (!WindowsNotificationClient.IsValid())
		{
			// Shared (This is a COM object, so we don't delete it, just derecement the ref counter).
			WindowsNotificationClient = TSharedPtr<FWindowsMMNotificationClient>(
				new FWindowsMMNotificationClient, 
				[](FWindowsMMNotificationClient* InPtr) { InPtr->Release(); }
			);
		}
		if (!DeviceInfoCache.IsValid())
		{
			// Setup device info cache.
			DeviceInfoCache = MakeUnique<FWindowsMMDeviceCache>();
			WindowsNotificationClient->RegisterDeviceChangedListener(static_cast<FWindowsMMDeviceCache*>(DeviceInfoCache.Get()));
		}

		WindowsNotificationClient->RegisterDeviceChangedListener(this);
	}

	void FMixerPlatformXAudio2::UnregisterDeviceChangedListener() 
	{
		if (WindowsNotificationClient.IsValid())
		{
			if (DeviceInfoCache.IsValid())
			{
				// Unregister and kill cache.
				WindowsNotificationClient->UnRegisterDeviceDeviceChangedListener(static_cast<FWindowsMMDeviceCache*>(DeviceInfoCache.Get()));
				DeviceInfoCache.Reset();
			}
			
			WindowsNotificationClient->UnRegisterDeviceDeviceChangedListener(this);
		}
	}

	void FMixerPlatformXAudio2::OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId)
	{
		if (UAudioDeviceNotificationSubsystem* AudioDeviceNotifSubsystem = UAudioDeviceNotificationSubsystem::Get())
		{
			AudioDeviceNotifSubsystem->OnDefaultCaptureDeviceChanged(InAudioDeviceRole, DeviceId);
		}
	}

	void FMixerPlatformXAudio2::OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId)
	{
		if (!AllowDeviceSwap())
		{
			return;
		}

		// Ignore changes that are for a change in default communication device.
		if (InAudioDeviceRole != EAudioDeviceRole::Communications)
		{
		if (AudioDeviceSwapCriticalSection.TryLock())
		{
				UE_LOG(LogAudioMixer, Warning, TEXT("Changing default audio render device to new device: %s."), *WindowsNotificationClient->GetFriendlyName(DeviceId));

				NewAudioDeviceId = DeviceId;
			bMoveAudioStreamToNewAudioDevice = true;

			AudioDeviceSwapCriticalSection.Unlock();
		}
		}

		if (UAudioDeviceNotificationSubsystem* AudioDeviceNotifSubsystem = UAudioDeviceNotificationSubsystem::Get())
		{
			AudioDeviceNotifSubsystem->OnDefaultRenderDeviceChanged(InAudioDeviceRole, DeviceId);
		}
	}

	void FMixerPlatformXAudio2::OnDeviceAdded(const FString& DeviceId, bool bIsRenderDevice)
	{
		// Ignore changes in capture device.
		if (!bIsRenderDevice)
	{
			return;
		}
		
		if (AudioDeviceSwapCriticalSection.TryLock())
		{
			// If the device that was added is our original device and our current device is NOT our original device, 
			// move our audio stream to this newly added device.
			if (AudioStreamInfo.DeviceInfo.DeviceId != OriginalAudioDeviceId && DeviceId == OriginalAudioDeviceId)
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Original audio device re-added. Moving audio back to original audio device %s."), *WindowsNotificationClient->GetFriendlyName(*OriginalAudioDeviceId));

				NewAudioDeviceId = OriginalAudioDeviceId;
				bMoveAudioStreamToNewAudioDevice = true;
			}

			AudioDeviceSwapCriticalSection.Unlock();
		}

		if (UAudioDeviceNotificationSubsystem* AudioDeviceNotifSubsystem = UAudioDeviceNotificationSubsystem::Get())
		{
			AudioDeviceNotifSubsystem->OnDeviceAdded(DeviceId, bIsRenderDevice);
		}
	}

	void FMixerPlatformXAudio2::OnDeviceRemoved(const FString& DeviceId, bool bIsRenderDevice)
	{
		// Ignore changes in capture device.
		if (!bIsRenderDevice)
		{
			return;
		}
		
		if (AudioDeviceSwapCriticalSection.TryLock())
		{
			// If the device we're currently using was removed... then switch to the new default audio device.
			if (AudioStreamInfo.DeviceInfo.DeviceId == DeviceId)
			{
				UE_LOG(LogAudioMixer, Warning, TEXT("Audio device removed [%s], falling back to other windows default device."), *WindowsNotificationClient->GetFriendlyName(DeviceId) );

				NewAudioDeviceId = "";
				bMoveAudioStreamToNewAudioDevice = true;
			}
			AudioDeviceSwapCriticalSection.Unlock();
		}

		if (UAudioDeviceNotificationSubsystem* AudioDeviceNotifSubsystem = UAudioDeviceNotificationSubsystem::Get())
		{
			AudioDeviceNotifSubsystem->OnDeviceRemoved(DeviceId, bIsRenderDevice);
		}
	}

	void FMixerPlatformXAudio2::OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState, bool bIsRenderDevice)
	{
		// Ignore changes in capture device.
		if (!bIsRenderDevice)
	{
			return;
		}
		
		if (UAudioDeviceNotificationSubsystem* AudioDeviceNotifSubsystem = UAudioDeviceNotificationSubsystem::Get())
		{
			AudioDeviceNotifSubsystem->OnDeviceStateChanged(DeviceId, InState, bIsRenderDevice);
		}
	}

	FString FMixerPlatformXAudio2::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}
}

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

#else 
// Nothing for XBOXOne
namespace Audio
{
	void FMixerPlatformXAudio2::RegisterDeviceChangedListener() {}
	void FMixerPlatformXAudio2::UnregisterDeviceChangedListener() {}
	void FMixerPlatformXAudio2::OnDefaultCaptureDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDefaultRenderDeviceChanged(const EAudioDeviceRole InAudioDeviceRole, const FString& DeviceId) {}
	void FMixerPlatformXAudio2::OnDeviceAdded(const FString& DeviceId, bool bIsRender) {}
	void FMixerPlatformXAudio2::OnDeviceRemoved(const FString& DeviceId, bool bIsRender) {}
	void FMixerPlatformXAudio2::OnDeviceStateChanged(const FString& DeviceId, const EAudioDeviceState InState, bool bIsRender){}
	FString FMixerPlatformXAudio2::GetDeviceId() const
	{
		return AudioStreamInfo.DeviceInfo.DeviceId;
	}
}
#endif


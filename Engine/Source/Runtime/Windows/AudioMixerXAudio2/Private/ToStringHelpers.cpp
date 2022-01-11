// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToStringHelpers.h"

#if PLATFORM_WINDOWS

#if !NO_LOGGING
// PSStringFromPropertyKey needs this lib, which we use for logging the Property GUIDs.
#pragma comment(lib, "Propsys.lib")
#endif //!NO_LOGGING

namespace Audio
{
	const TCHAR* ToString(AudioSessionDisconnectReason InDisconnectReason)
	{
#if !NO_LOGGING
#define CASE_TO_STRING(X) case AudioSessionDisconnectReason::X: return TEXT(#X)
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
#undef CASE_TO_STRING
#endif //!NO_LOGGING
		return TEXT("Unknown");
	}

	const TCHAR* ToString(ERole InRole)
	{
#if !NO_LOGGING
#define CASE_TO_STRING(X) case ERole::X: return TEXT(#X)
		switch (InRole)
		{
			CASE_TO_STRING(eConsole);
			CASE_TO_STRING(eMultimedia);
			CASE_TO_STRING(eCommunications);
		default:
			checkNoEntry();
			break;
		}
#undef CASE_TO_STRING
#endif //!NO_LOGGING
		return TEXT("Unknown");
	}

	const TCHAR* ToString(EDataFlow InFlow)
	{
#if !NO_LOGGING
#define CASE_TO_STRING(X) case EDataFlow::X: return TEXT(#X)
		switch (InFlow)
		{
			CASE_TO_STRING(eRender);
			CASE_TO_STRING(eCapture);
			CASE_TO_STRING(eAll);
		default:
			checkNoEntry();
			break;
		}
#endif //!NO_LOGGING
#undef CASE_TO_STRING
		return TEXT("Unknown");
	}

	const TCHAR* ToString(EAudioDeviceRole InRole)
	{
#define CASE_TO_STRING(X) case EAudioDeviceRole::X: return TEXT(#X)
		switch (InRole)
		{
			CASE_TO_STRING(Console);
			CASE_TO_STRING(Multimedia);
			CASE_TO_STRING(Communications);
		default:
			return TEXT("Unknown");
		}
#undef CASE_TO_STRING
	}

	const TCHAR* ToString(EAudioDeviceState InState)
	{
#define CASE_TO_STRING(X) case EAudioDeviceState::X: return TEXT(#X)
		switch (InState)
		{
			CASE_TO_STRING(Active);
			CASE_TO_STRING(Disabled);
			CASE_TO_STRING(NotPresent);
			CASE_TO_STRING(Unplugged);
		default:
			return TEXT("Unknown");
		}
#undef CASE_TO_STRING
	}

	const FString ToFString(const TArray<EAudioMixerChannel::Type>& InChannels)
	{
		FString ChannelString;
		static const int32 ApproxChannelNameLength = 18;
		ChannelString.Reserve(ApproxChannelNameLength * InChannels.Num());
		for (EAudioMixerChannel::Type i : InChannels)
		{
			ChannelString.Append(EAudioMixerChannel::ToString(i));
			ChannelString.Append(TEXT("|"));
		}
		return ChannelString;
	}

	FString ToFString(const PROPERTYKEY Key)
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
		if (SUCCEEDED(HR))
		{
			return FString(KeyString);
		}
#endif //!NO_LOGGING
		return TEXT("Unknown");
	}

}

#endif //PLATFORM_WINDOWS
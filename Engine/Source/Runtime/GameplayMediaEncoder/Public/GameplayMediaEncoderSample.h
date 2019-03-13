// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
	#include <mftransform.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"

#elif PLATFORM_XBOXONE

#include "XboxOne/XboxOneAllowPlatformTypes.h"
#include "XboxOne/XboxOnePreApi.h"
	#include <mftransform.h>
#include "XboxOne/XboxOnePostApi.h"
#include "XboxOne/XboxOneHidePlatformTypes.h"
#endif

enum class EMediaType { Audio = 0, Video = 1, Invalid = 2 };
inline const TCHAR* MediaTypeStr(EMediaType MediaType)
{
	const TCHAR* Str[] = { TEXT("audio"), TEXT("video"), TEXT("invalid media type") };
	return Str[static_cast<int>(MediaType)];
}

class GAMEPLAYMEDIAENCODER_API FGameplayMediaEncoderSample
{
public:
	FGameplayMediaEncoderSample(EMediaType InMediaType = EMediaType::Invalid, IMFSample* InSample = nullptr) :
		MediaType(InMediaType),
		Sample(InSample)
	{}

	EMediaType GetType() const
	{
		return MediaType;
	}

	const IMFSample* GetSample() const
	{
		return Sample;
	}

	IMFSample* GetSample()
	{
		return Sample;
	}

	bool CreateSample();

	FTimespan GetTime() const;

	void SetTime(FTimespan Time);

	FTimespan GetDuration() const;

	void SetDuration(FTimespan Duration);

	bool IsVideoKeyFrame() const;

	bool IsValid() const
	{
		return Sample.IsValid();
	}

	void Reset()
	{
		Sample = nullptr;
	}

	FGameplayMediaEncoderSample Clone() const;

private:
	EMediaType MediaType;
	TRefCountPtr<IMFSample> Sample;
};
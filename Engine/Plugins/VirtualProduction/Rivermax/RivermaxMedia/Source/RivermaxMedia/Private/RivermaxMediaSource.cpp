// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaSource.h"

#include "MediaIOCorePlayerBase.h"
#include "RivermaxMediaSourceOptions.h"


/*
 * IMediaOptions interface
 */

bool URivermaxMediaSource::GetMediaOption(const FName& Key, bool DefaultValue) const
{
	return Super::GetMediaOption(Key, DefaultValue);
}

int64 URivermaxMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == RivermaxMediaOption::Port)
	{
		return Port;
	}
	else if (Key == RivermaxMediaOption::PixelFormat)
	{
		return (int64)PixelFormat;
	}
	else if (Key == FMediaIOCoreMediaOption::FrameRateNumerator)
	{
		return FrameRate.Numerator;
	}
	else if (Key == FMediaIOCoreMediaOption::FrameRateDenominator)
	{
		return FrameRate.Denominator;
	}
	else if (Key == FMediaIOCoreMediaOption::ResolutionWidth)
	{
		return Resolution.X;
	}
	else if (Key == FMediaIOCoreMediaOption::ResolutionHeight)
	{
		return Resolution.Y;
	}
	return Super::GetMediaOption(Key, DefaultValue);
}

FString URivermaxMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	if (Key == FMediaIOCoreMediaOption::VideoModeName)
	{
		return FString::Printf(TEXT("FormatDescriptorTodo"));
	}
	else if (Key == RivermaxMediaOption::SourceAddress)
	{
		return SourceAddress;
	}
	else if (Key == RivermaxMediaOption::DestinationAddress)
	{
		return DestinationAddress;
	}
	return Super::GetMediaOption(Key, DefaultValue);
}

bool URivermaxMediaSource::HasMediaOption(const FName& Key) const
{
	if ((Key == RivermaxMediaOption::SourceAddress) ||
		(Key == RivermaxMediaOption::DestinationAddress) ||
		(Key == RivermaxMediaOption::Port) ||
		(Key == RivermaxMediaOption::PixelFormat) ||
		(Key == FMediaIOCoreMediaOption::FrameRateNumerator) ||
		(Key == FMediaIOCoreMediaOption::FrameRateDenominator) ||
		(Key == FMediaIOCoreMediaOption::ResolutionWidth) ||
		(Key == FMediaIOCoreMediaOption::ResolutionHeight) ||
		(Key == FMediaIOCoreMediaOption::VideoModeName))
	{
		return true;
	}

	return Super::HasMediaOption(Key);
}

/*
 * UMediaSource interface
 */

FString URivermaxMediaSource::GetUrl() const
{
	return TEXT("rmax://");//todo support proper url
}

bool URivermaxMediaSource::Validate() const
{
	return true;
}


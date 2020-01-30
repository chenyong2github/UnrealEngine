// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSource.h"

/* IMediaOptions interface
 *****************************************************************************/

FName UMediaSource::GetDesiredPlayerName() const
{
	return NAME_None;
}


bool UMediaSource::GetMediaOption(const FName& Key, bool DefaultValue) const
{
	const FVariant* Variant = GetMediaOptionDefault(Key);
	if ((Variant != nullptr) && (Variant->GetType() == EVariantTypes::Bool))
	{
		return Variant->GetValue<bool>();
	}
	else
	{
		return DefaultValue;
	}
}


double UMediaSource::GetMediaOption(const FName& Key, double DefaultValue) const
{
	const FVariant* Variant = GetMediaOptionDefault(Key);
	if ((Variant != nullptr) && (Variant->GetType() == EVariantTypes::Double))
	{
		return Variant->GetValue<double>();
	}
	else
	{
		return DefaultValue;
	}
}


int64 UMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	const FVariant* Variant = GetMediaOptionDefault(Key);
	if ((Variant != nullptr) && (Variant->GetType() == EVariantTypes::Int64))
	{
		return Variant->GetValue<int64>();
	}
	else
	{
		return DefaultValue;
	}
}


FString UMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	const FVariant* Variant = GetMediaOptionDefault(Key);
	if ((Variant != nullptr) && (Variant->GetType() == EVariantTypes::String))
	{
		return Variant->GetValue<FString>();
	}
	else
	{
		return DefaultValue;
	}
}


FText UMediaSource::GetMediaOption(const FName& Key, const FText& DefaultValue) const
{
	return DefaultValue;
}


bool UMediaSource::HasMediaOption(const FName& Key) const
{
	return MediaOptionsMap.Contains(Key);
}


void UMediaSource::SetMediaOptionBool(const FName& Key, bool Value)
{
	FVariant Variant(Value);
	SetMediaOption(Key, Variant);
}


void UMediaSource::SetMediaOptionFloat(const FName& Key, float Value)
{
	SetMediaOptionDouble(Key, (double)Value);
}


void UMediaSource::SetMediaOptionDouble(const FName& Key, double Value)
{
	FVariant Variant(Value);
	SetMediaOption(Key, Variant);
}


void UMediaSource::SetMediaOptionInt64(const FName& Key, int64 Value)
{
	FVariant Variant(Value);
	SetMediaOption(Key, Variant);
}


void UMediaSource::SetMediaOptionString(const FName& Key, const FString& Value)
{
	FVariant Variant(Value);
	SetMediaOption(Key, Variant);
}


const FVariant* UMediaSource::GetMediaOptionDefault(const FName& Key) const
{
	return MediaOptionsMap.Find(Key);
}


void UMediaSource::SetMediaOption(const FName& Key, FVariant& Value)
{
	MediaOptionsMap.Emplace(Key, Value);
}



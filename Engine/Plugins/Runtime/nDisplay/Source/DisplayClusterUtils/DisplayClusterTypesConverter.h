// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"
#include "DisplayClusterEnums.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"
#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"


/**
 * Auxiliary class with different type conversion functions
 */
class FDisplayClusterTypesConverter
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// TYPE --> STRING
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertFrom>
	static FString ToString(const ConvertFrom& from);

	//////////////////////////////////////////////////////////////////////////////////////////////
	// STRING --> TYPE
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertTo>
	static ConvertTo FromString(const FString& from);
};

template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FString& from)    { return from; }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const bool& from)       { return (from ? DisplayClusterStrings::cfg::spec::ValTrue : DisplayClusterStrings::cfg::spec::ValFalse); }
template <> inline FString FDisplayClusterTypesConverter::ToString<> (const int8& from)      { return FString::FromInt(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<> (const uint8& from)     { return ToString(static_cast<int8>(from)); }
template <> inline FString FDisplayClusterTypesConverter::ToString<> (const int32& from)     { return FString::FromInt(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<> (const uint32& from)    { return ToString(static_cast<int32>(from)); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const float& from)      { return FString::SanitizeFloat(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const double& from)     { return FString::Printf(TEXT("%lf"), from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FVector& from)    { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FVector2D& from)  { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FRotator& from)   { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FMatrix& from)    { return from.ToString(); }

// We can't just use FTimecode ToString as that loses information.
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FTimecode& from)  { return FString::Printf(TEXT("%d;%d;%d;%d;%d"), from.bDropFrameFormat ? 1 : 0, from.Hours, from.Minutes, from.Seconds, from.Frames); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FFrameRate& from) { return FString::Printf(TEXT("%d;%d"), from.Numerator, from.Denominator); }

template <> inline FString FDisplayClusterTypesConverter::ToString<>(const EDisplayClusterOperationMode& from)
{
	switch (from)
	{
	case EDisplayClusterOperationMode::Cluster:
		return FString("cluster");
	case EDisplayClusterOperationMode::Standalone:
		return FString("standalone");
	case EDisplayClusterOperationMode::Editor:
		return FString("editor");
	case EDisplayClusterOperationMode::Disabled:
		return FString("disabled");
	default:
		return FString("unknown");
	}
}

template <> inline FString FDisplayClusterTypesConverter::ToString<>(const EDisplayClusterSyncGroup& from)
{
	switch (from)
	{
	case EDisplayClusterSyncGroup::PreTick:
		return FString("PreTick");
	case EDisplayClusterSyncGroup::Tick:
		return FString("Tick");
	case EDisplayClusterSyncGroup::PostTick:
		return FString("PostTick");
	}

	return FString("Tick");
}


template <> inline FString   FDisplayClusterTypesConverter::FromString<> (const FString& from) { return from; }
template <> inline bool      FDisplayClusterTypesConverter::FromString<> (const FString& from) { return (from == FString("1") || (from.Compare(DisplayClusterStrings::cfg::spec::ValTrue, ESearchCase::IgnoreCase) == 0)); }
template <> inline int8      FDisplayClusterTypesConverter::FromString<> (const FString& from) { return FCString::Atoi(*from); }
template <> inline uint8     FDisplayClusterTypesConverter::FromString<> (const FString& from) { return static_cast<uint8>(FromString<int8>(from)); }
template <> inline int32     FDisplayClusterTypesConverter::FromString<> (const FString& from) { return FCString::Atoi(*from); }
template <> inline uint32    FDisplayClusterTypesConverter::FromString<> (const FString& from) { return static_cast<uint32>(FromString<int32>(from)); }
template <> inline float     FDisplayClusterTypesConverter::FromString<> (const FString& from) { return FCString::Atof(*from); }
template <> inline double    FDisplayClusterTypesConverter::FromString<> (const FString& from) { return FCString::Atod(*from); }
template <> inline FVector   FDisplayClusterTypesConverter::FromString<> (const FString& from) { FVector vec;  vec.InitFromString(from); return vec; }
template <> inline FVector2D FDisplayClusterTypesConverter::FromString<> (const FString& from) { FVector2D vec;  vec.InitFromString(from); return vec; }
template <> inline FRotator  FDisplayClusterTypesConverter::FromString<> (const FString& from) { FRotator rot; rot.InitFromString(from); return rot; }

template <> inline FMatrix   FDisplayClusterTypesConverter::FromString<>(const FString& from)
{
	FMatrix ResultMatrix = FMatrix::Identity;
	FPlane  Planes[4];

	int32 IdxStart = 0;
	int32 IdxEnd = 0;
	for (int PlaneNum = 0; PlaneNum < 4; ++PlaneNum)
	{
		IdxStart = from.Find(FString(TEXT("[")), ESearchCase::IgnoreCase, ESearchDir::FromStart, IdxEnd);
		IdxEnd   = from.Find(FString(TEXT("]")), ESearchCase::IgnoreCase, ESearchDir::FromStart, IdxStart);
		if (IdxStart == INDEX_NONE || IdxEnd == INDEX_NONE || (IdxEnd - IdxStart) < 8)
		{
			return ResultMatrix;
		}

		FString StrPlane = from.Mid(IdxStart + 1, IdxEnd - IdxStart - 1);

		int32 StrLen = 0;
		int32 NewStrLen = -1;
		while (NewStrLen < StrLen)
		{
			StrLen = StrPlane.Len();
			StrPlane.ReplaceInline(TEXT("  "), TEXT(" "));
			NewStrLen = StrPlane.Len();
		}

		TArray<FString> StrPlaneValues;
		StrPlane.ParseIntoArray(StrPlaneValues, TEXT(" "));
		if (StrPlaneValues.Num() != 4)
		{
			return ResultMatrix;
		}

		float PlaneValues[4] = { 0.f };
		for (int i = 0; i < 4; ++i)
		{
			PlaneValues[i] = FromString<float>(StrPlaneValues[i]);
		}

		Planes[PlaneNum] = FPlane(PlaneValues[0], PlaneValues[1], PlaneValues[2], PlaneValues[3]);
	}

	ResultMatrix = FMatrix(Planes[0], Planes[1], Planes[2], Planes[3]);

	return ResultMatrix;
}

template <> inline FTimecode FDisplayClusterTypesConverter::FromString<> (const FString& from)
{
	FTimecode timecode;

	TArray<FString> parts;
	parts.Reserve(5);
	const int32 found = from.ParseIntoArray(parts, TEXT(";"));

	// We are expecting 5 "parts" - DropFrame, Hours, Minutes, Seconds, Frames.
	if (found == 5)
	{
		timecode.bDropFrameFormat = FromString<bool>(parts[0]);
		timecode.Hours = FromString<int32>(parts[1]);
		timecode.Minutes = FromString<int32>(parts[2]);
		timecode.Seconds = FromString<int32>(parts[3]);
		timecode.Frames = FromString<int32>(parts[4]);
	}

	return timecode;
}

template <> inline FFrameRate FDisplayClusterTypesConverter::FromString<> (const FString& from)
{
	FFrameRate frameRate;

	TArray<FString> parts;
	parts.Reserve(2);
	const int32 found = from.ParseIntoArray(parts, TEXT(";"));

	// We are expecting 2 "parts" - Numerator, Denominator.
	if (found == 2)
	{
		frameRate.Numerator   = FromString<int32>(parts[0]);
		frameRate.Denominator = FromString<int32>(parts[1]);
	}

	return frameRate;
}

template <> inline EDisplayClusterSyncGroup FDisplayClusterTypesConverter::FromString<>(const FString& from)
{
	if (from.Equals(TEXT("PreTick")))
	{
		return EDisplayClusterSyncGroup::PreTick;
	}
	else if (from.Equals(TEXT("Tick")))
	{
		return EDisplayClusterSyncGroup::Tick;
	}
	else if (from.Equals(TEXT("PostTick")))
	{
		return EDisplayClusterSyncGroup::PostTick;
	}

	return EDisplayClusterSyncGroup::Tick;
}

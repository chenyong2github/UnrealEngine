// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Timecode.h"
#include "Misc/FrameRate.h"
#include "Misc/QualifiedFrameTime.h"
#include "DisplayClusterEnums.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>


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

	//////////////////////////////////////////////////////////////////////////////////////////////
	// TYPE --> HEX STRING
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertFrom>
	static FString ToHexString(const ConvertFrom& from);

	//////////////////////////////////////////////////////////////////////////////////////////////
	// HEX STRING --> TYPE
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertTo>
	static ConvertTo FromHexString(const FString& from);
};


//////////////////////////////////////////////////////////////////////////////////////////////
// TYPE --> STRING
//////////////////////////////////////////////////////////////////////////////////////////////
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FString& from)    { return from; }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const bool& from)       { return (from ? DisplayClusterStrings::cfg::spec::ValTrue : DisplayClusterStrings::cfg::spec::ValFalse); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const int8& from)       { return FString::FromInt(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const uint8& from)      { return ToString(static_cast<int8>(from)); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const int32& from)      { return FString::FromInt(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const uint32& from)     { return ToString(static_cast<int32>(from)); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const float& from)      { return FString::SanitizeFloat(from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const double& from)     { return FString::Printf(TEXT("%lf"), from); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FVector& from)    { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FVector2D& from)  { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FRotator& from)   { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FMatrix& from)    { return from.ToString(); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FQuat& from)      { return from.ToString(); }

// We can't just use FTimecode ToString as that loses information.
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FTimecode& from)  { return FString::Printf(TEXT("%d;%d;%d;%d;%d"), from.bDropFrameFormat ? 1 : 0, from.Hours, from.Minutes, from.Seconds, from.Frames); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FFrameRate& from) { return FString::Printf(TEXT("%d;%d"), from.Numerator, from.Denominator); }
template <> inline FString FDisplayClusterTypesConverter::ToString<>(const FQualifiedFrameTime& from) { return FString::Printf(TEXT("%d;%s;%d;%d"), from.Time.GetFrame().Value, *FString::SanitizeFloat(from.Time.GetSubFrame()), from.Rate.Numerator, from.Rate.Denominator); }

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


//////////////////////////////////////////////////////////////////////////////////////////////
// STRING --> TYPE
//////////////////////////////////////////////////////////////////////////////////////////////
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

template <> inline FQuat FDisplayClusterTypesConverter::FromString<>(const FString& from)
{
	FQuat Result = FQuat::Identity;
	const bool bSuccessful
		=  FParse::Value(*from, TEXT("X="), Result.X)
		&& FParse::Value(*from, TEXT("Y="), Result.Y)
		&& FParse::Value(*from, TEXT("Z="), Result.Z)
		&& FParse::Value(*from, TEXT("W="), Result.W);

	if (!bSuccessful)
	{
		return FQuat::Identity;
	}

	return Result;
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

template <> inline FQualifiedFrameTime FDisplayClusterTypesConverter::FromString<>(const FString& from)
{
	FQualifiedFrameTime frameTime;

	TArray<FString> parts;
	parts.Reserve(4);
	const int32 found = from.ParseIntoArray(parts, TEXT(";"));

	// We are expecting 4 "parts" - Frame, SubFrame, Numerator, Denominator.
	if (found == 4)
	{
		frameTime.Time = FFrameTime(FromString<int32>(parts[0]), FromString<float>(parts[1]));
		frameTime.Rate.Numerator = FromString<int32>(parts[2]);
		frameTime.Rate.Denominator = FromString<int32>(parts[3]);
	}

	return frameTime;
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


//////////////////////////////////////////////////////////////////////////////////////////////
// TYPE --> HEX STRING
//////////////////////////////////////////////////////////////////////////////////////////////
template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const float& from)
{
	static_assert(std::numeric_limits<float>::is_iec559, "Native float must be an IEEE float");

	union { float fval; std::uint32_t ival; };
	fval = from;

	std::ostringstream stm;
	stm << std::hex << std::nouppercase << ival;

	return FString(FString(TEXT("0x")) + FString(stm.str().c_str()));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const double& from)
{
	static_assert(std::numeric_limits<double>::is_iec559, "Native double must be an IEEE double");

	union { double dval; std::uint64_t ival; };
	dval = from;

	std::ostringstream stm;
	stm << std::hex << std::nouppercase << ival;

	return FString(FString(TEXT("0x")) + FString(stm.str().c_str()));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FVector& from)
{
	return FString::Printf(TEXT("X=%s Y=%s Z=%s"), 
		*FDisplayClusterTypesConverter::template ToHexString<>(from.X),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Y),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Z));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FVector2D& from)
{
	return FString::Printf(TEXT("X=%s Y=%s"),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.X),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Y));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FRotator& from)
{
	return FString::Printf(TEXT("P=%s Y=%s R=%s"),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Pitch),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Yaw),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Roll));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FMatrix& from)
{
	FString Result;

	for (int i = 0; i < 4; ++i)
	{
		Result += FString::Printf(TEXT("[%s %s %s %s] "),
			*FDisplayClusterTypesConverter::template ToHexString<>(from.M[i][0]),
			*FDisplayClusterTypesConverter::template ToHexString<>(from.M[i][1]),
			*FDisplayClusterTypesConverter::template ToHexString<>(from.M[i][2]),
			*FDisplayClusterTypesConverter::template ToHexString<>(from.M[i][3]));
	}

	return Result;
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FTransform& from)
{
	return FString::Printf(TEXT("%s|%s|%s"),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.GetLocation()),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.GetRotation().Rotator()),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.GetScale3D()));
}

template <> inline FString FDisplayClusterTypesConverter::ToHexString<>(const FQuat& from)
{
	return FString::Printf(TEXT("X=%s Y=%s Z=%s W=%s"),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.X),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Y),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.Z),
		*FDisplayClusterTypesConverter::template ToHexString<>(from.W));
}



//////////////////////////////////////////////////////////////////////////////////////////////
// HEX STRING --> TYPE
//////////////////////////////////////////////////////////////////////////////////////////////
template <> inline float FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	static_assert(std::numeric_limits<float>::is_iec559, "Native float must be an IEEE float");

	union { float fval; std::uint32_t ival; };

	std::string str(TCHAR_TO_UTF8(*from));
	std::stringstream stm(str);
	stm >> std::hex >> ival;
	return fval;
}

template <> inline double FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	static_assert(std::numeric_limits<double>::is_iec559, "Native double must be an IEEE double");

	union { double dval; std::uint64_t ival; };

	std::string str(TCHAR_TO_UTF8(*from));
	std::stringstream stm(str);
	stm >> std::hex >> ival;
	return dval;
}

template <> inline FVector FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	FString X, Y, Z;
	FVector Result;

	const bool bSuccessful = FParse::Value(*from, TEXT("X="), X) && FParse::Value(*from, TEXT("Y="), Y) && FParse::Value(*from, TEXT("Z="), Z);
	if (bSuccessful)
	{
		Result.X = FDisplayClusterTypesConverter::template FromHexString<float>(X);
		Result.Y = FDisplayClusterTypesConverter::template FromHexString<float>(Y);
		Result.Z = FDisplayClusterTypesConverter::template FromHexString<float>(Z);
	}

	return Result;
}

template <> inline FVector2D FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	FString X, Y;
	FVector2D Result;

	const bool bSuccessful = FParse::Value(*from, TEXT("X="), X) && FParse::Value(*from, TEXT("Y="), Y);
	if (bSuccessful)
	{
		Result.X = FDisplayClusterTypesConverter::template FromHexString<float>(X);
		Result.Y = FDisplayClusterTypesConverter::template FromHexString<float>(Y);
	}

	return Result;
}

template <> inline FRotator FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	FString P, Y, R;
	FRotator Result;

	const bool bSuccessful = FParse::Value(*from, TEXT("P="), P) && FParse::Value(*from, TEXT("Y="), Y) && FParse::Value(*from, TEXT("R="), R);
	if (bSuccessful)
	{
		Result.Pitch = FDisplayClusterTypesConverter::template FromHexString<float>(P);
		Result.Yaw   = FDisplayClusterTypesConverter::template FromHexString<float>(Y);
		Result.Roll  = FDisplayClusterTypesConverter::template FromHexString<float>(R);
	}

	return Result;
}

template <> inline FMatrix FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	FMatrix ResultMatrix = FMatrix::Identity;
	FPlane  Planes[4];

	int32 IdxStart = 0;
	int32 IdxEnd = 0;
	for (int PlaneNum = 0; PlaneNum < 4; ++PlaneNum)
	{
		IdxStart = from.Find(FString(TEXT("[")), ESearchCase::IgnoreCase, ESearchDir::FromStart, IdxEnd);
		IdxEnd = from.Find(FString(TEXT("]")), ESearchCase::IgnoreCase, ESearchDir::FromStart, IdxStart);
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
			PlaneValues[i] = FDisplayClusterTypesConverter::template FromHexString<float>(StrPlaneValues[i]);
		}

		Planes[PlaneNum] = FPlane(PlaneValues[0], PlaneValues[1], PlaneValues[2], PlaneValues[3]);
	}

	ResultMatrix = FMatrix(Planes[0], Planes[1], Planes[2], Planes[3]);

	return ResultMatrix;
}

template <> inline FTransform FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	TArray<FString> ComponentStrings;
	from.ParseIntoArray(ComponentStrings, TEXT("|"), true);
	if (ComponentStrings.Num() != 3)
	{
		return FTransform::Identity;
	}

	const FVector  ParsedTranslation = FDisplayClusterTypesConverter::template FromHexString<FVector>(ComponentStrings[0]);
	const FRotator ParsedRotation    = FDisplayClusterTypesConverter::template FromHexString<FRotator>(ComponentStrings[1]);
	const FVector  ParsedScale       = FDisplayClusterTypesConverter::template FromHexString<FVector>(ComponentStrings[2]);

	const FTransform Result(ParsedRotation, ParsedTranslation, ParsedScale);

	return Result;
}

template <> inline FQuat FDisplayClusterTypesConverter::FromHexString<>(const FString& from)
{
	FString X, Y, Z, W;
	FQuat Result = FQuat::Identity;

	const bool bSuccessful = FParse::Value(*from, TEXT("X="), X) && FParse::Value(*from, TEXT("Y="), Y) && FParse::Value(*from, TEXT("Z="), Z) && FParse::Value(*from, TEXT("W="), W);
	if (bSuccessful)
	{
		Result.X = FDisplayClusterTypesConverter::template FromHexString<float>(X);
		Result.Y = FDisplayClusterTypesConverter::template FromHexString<float>(Y);
		Result.Z = FDisplayClusterTypesConverter::template FromHexString<float>(Z);
		Result.W = FDisplayClusterTypesConverter::template FromHexString<float>(W);
	}

	return Result;
}

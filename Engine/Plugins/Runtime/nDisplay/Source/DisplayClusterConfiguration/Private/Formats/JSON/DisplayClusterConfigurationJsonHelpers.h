// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes.h"


/**
 * Auxiliary class with different type conversion functions
 */
class DisplayClusterConfigurationJsonHelpers
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// TYPE --> STRING
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertFrom>
	static FString ToString(const ConvertFrom& From);

	//////////////////////////////////////////////////////////////////////////////////////////////
	// STRING --> TYPE
	//////////////////////////////////////////////////////////////////////////////////////////////
	template <typename ConvertTo>
	static ConvertTo FromString(const FString& From);
};


// EDisplayClusterConfigurationEyeStereoOffset
template <>
inline FString DisplayClusterConfigurationJsonHelpers::ToString<>(const EDisplayClusterConfigurationEyeStereoOffset& From)
{
	switch (From)
	{
	case EDisplayClusterConfigurationEyeStereoOffset::None:
		return DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetNone;

	case EDisplayClusterConfigurationEyeStereoOffset::Left:
		return DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetLeft;

	case EDisplayClusterConfigurationEyeStereoOffset::Right:
		return DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetRight;

	default:
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Unexpected camera stereo offset type"));
		break;
	}

	return DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetNone;
}

template <>
inline EDisplayClusterConfigurationEyeStereoOffset DisplayClusterConfigurationJsonHelpers::FromString<>(const FString& From)
{
	EDisplayClusterConfigurationEyeStereoOffset Result = EDisplayClusterConfigurationEyeStereoOffset::None;

	if (From.Equals(DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetNone, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationEyeStereoOffset::None;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetLeft, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationEyeStereoOffset::Left;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::scene::camera::CameraStereoOffsetRight, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationEyeStereoOffset::Right;
	}

	return Result;
}


// EDisplayClusterConfigurationKeyboardReflectionType
template <>
inline FString DisplayClusterConfigurationJsonHelpers::ToString<>(const EDisplayClusterConfigurationKeyboardReflectionType& From)
{
	switch (From)
	{
	case EDisplayClusterConfigurationKeyboardReflectionType::None:
		return DisplayClusterConfigurationStrings::config::input::devices::ReflectNone;

	case EDisplayClusterConfigurationKeyboardReflectionType::nDisplay:
		return DisplayClusterConfigurationStrings::config::input::devices::ReflectNdisplay;

	case EDisplayClusterConfigurationKeyboardReflectionType::Core:
		return DisplayClusterConfigurationStrings::config::input::devices::ReflectCore;

	case EDisplayClusterConfigurationKeyboardReflectionType::All:
		return DisplayClusterConfigurationStrings::config::input::devices::ReflectAll;

	default:
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Unexpected keyboard reflection type"));
		break;
	}

	return DisplayClusterConfigurationStrings::config::input::devices::ReflectNone;
}

template <>
inline EDisplayClusterConfigurationKeyboardReflectionType DisplayClusterConfigurationJsonHelpers::FromString<>(const FString& From)
{
	EDisplayClusterConfigurationKeyboardReflectionType Result = EDisplayClusterConfigurationKeyboardReflectionType::None;

	if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::ReflectNone, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationKeyboardReflectionType::None;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::ReflectNdisplay, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationKeyboardReflectionType::nDisplay;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::ReflectCore, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationKeyboardReflectionType::Core;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::ReflectAll, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationKeyboardReflectionType::All;
	}

	return Result;
}


// EDisplayClusterConfigurationTrackerMapping
template <>
inline FString DisplayClusterConfigurationJsonHelpers::ToString<>(const EDisplayClusterConfigurationTrackerMapping& From)
{
	switch (From)
	{
	case EDisplayClusterConfigurationTrackerMapping::X:
		return DisplayClusterConfigurationStrings::config::input::devices::MapX;

	case EDisplayClusterConfigurationTrackerMapping::NX:
		return DisplayClusterConfigurationStrings::config::input::devices::MapNX;

	case EDisplayClusterConfigurationTrackerMapping::Y:
		return DisplayClusterConfigurationStrings::config::input::devices::MapY;

	case EDisplayClusterConfigurationTrackerMapping::NY:
		return DisplayClusterConfigurationStrings::config::input::devices::MapNY;

	case EDisplayClusterConfigurationTrackerMapping::Z:
		return DisplayClusterConfigurationStrings::config::input::devices::MapZ;

	case EDisplayClusterConfigurationTrackerMapping::NZ:
		return DisplayClusterConfigurationStrings::config::input::devices::MapNZ;

	default:
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Unexpected axis mapping value"));
		break;
	}

	return DisplayClusterConfigurationStrings::config::input::devices::MapX;
}

template <>
inline EDisplayClusterConfigurationTrackerMapping DisplayClusterConfigurationJsonHelpers::FromString<>(const FString& From)
{
	EDisplayClusterConfigurationTrackerMapping Result = EDisplayClusterConfigurationTrackerMapping::X;

	if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapX, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::X;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapNX, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::NX;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapY, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::Y;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapNY, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::NY;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapZ, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::Z;
	}
	else if (From.Equals(DisplayClusterConfigurationStrings::config::input::devices::MapNZ, ESearchCase::IgnoreCase))
	{
		Result = EDisplayClusterConfigurationTrackerMapping::NZ;
	}

	return Result;
}

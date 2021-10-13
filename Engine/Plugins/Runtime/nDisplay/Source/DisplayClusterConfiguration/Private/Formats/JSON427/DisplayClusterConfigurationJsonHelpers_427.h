// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterConfigurationLog.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes.h"


namespace JSON427
{
	// since we cannot modify public headers we need to put those string in private headers temporary
	namespace config
	{
		namespace node
		{
			namespace viewport
			{
				namespace overscan
				{
					static constexpr auto OverscanNone = TEXT("none");
					static constexpr auto OverscanPixels = TEXT("pixels");
					static constexpr auto OverscanPercent = TEXT("percent");
				}
			}
		}
	}

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

	// EDisplayClusterConfigurationViewportOverscanMode
	template <>
	inline FString DisplayClusterConfigurationJsonHelpers::ToString<>(const EDisplayClusterConfigurationViewportOverscanMode& InOverscanMode)
	{
		switch (InOverscanMode)
		{
		case EDisplayClusterConfigurationViewportOverscanMode::None:
			return JSON427::config::node::viewport::overscan::OverscanNone;

		case EDisplayClusterConfigurationViewportOverscanMode::Pixels:
			return JSON427::config::node::viewport::overscan::OverscanPixels;

		case EDisplayClusterConfigurationViewportOverscanMode::Percent:
			return JSON427::config::node::viewport::overscan::OverscanPercent;

		default:
			UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Unexpected overscan type"));
			break;
		}

		return JSON427::config::node::viewport::overscan::OverscanNone;
	}

	template <>
	inline EDisplayClusterConfigurationViewportOverscanMode DisplayClusterConfigurationJsonHelpers::FromString<>(const FString& InOverscanModeString)
	{
		EDisplayClusterConfigurationViewportOverscanMode Result = EDisplayClusterConfigurationViewportOverscanMode::None;

		if (InOverscanModeString.Equals(JSON427::config::node::viewport::overscan::OverscanNone, ESearchCase::IgnoreCase))
		{
			Result = EDisplayClusterConfigurationViewportOverscanMode::None;
		}
		else if (InOverscanModeString.Equals(JSON427::config::node::viewport::overscan::OverscanPixels, ESearchCase::IgnoreCase))
		{
			Result = EDisplayClusterConfigurationViewportOverscanMode::Pixels;
		}
		else if (InOverscanModeString.Equals(JSON427::config::node::viewport::overscan::OverscanPercent, ESearchCase::IgnoreCase))
		{
			Result = EDisplayClusterConfigurationViewportOverscanMode::Percent;
		}

		return Result;
	}
}
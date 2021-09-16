// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace PixelStreamingProtocol
{
	enum class EToStreamerMsg : uint8
	{
		/**********************************************************************/
		
		/*
		 * Control Messages. Range = 0..49.
		 */
		
		IFrameRequest = 0,
		RequestQualityControl = 1,
		MaxFpsRequest = 2,
		AverageBitrateRequest = 3,
		StartStreaming = 4,
		StopStreaming = 5,
		LatencyTest = 6,
		RequestInitialSettings = 7,

		/**********************************************************************/

		/*
		 * Input Messages. Range = 50..89.
		 */

		// Generic Input Messages. Range = 50..59.
		UIInteraction = 50,
		Command = 51,

		// Keyboard Input Message. Range = 60..69.
		KeyDown = 60,
		KeyUp = 61,
		KeyPress = 62,

		// Mouse Input Messages. Range = 70..79.
		MouseEnter = 70,
		MouseLeave = 71,
		MouseDown = 72,
		MouseUp = 73,
		MouseMove = 74,
		MouseWheel = 75,

		// Touch Input Messages. Range = 80..89.
		TouchStart = 80,
		TouchEnd = 81,
		TouchMove = 82,

		// Gamepad Input Messages. Range = 90..99
		GamepadButtonPressed = 90,
		GamepadButtonReleased = 91,
		GamepadAnalog = 92,
		
		/**********************************************************************/

		/*
		 * Ensure Count is the final entry.
		 */
		Count

		/**********************************************************************/
	};

	//! Messages that can be sent to the webrtc players
	enum class EToPlayerMsg : uint8 {
		QualityControlOwnership = 0,
		Response = 1,
		Command = 2,
		FreezeFrame = 3,
		UnfreezeFrame = 4,
		VideoEncoderAvgQP = 5, // average Quantisation Parameter value of Video Encoder, roughly depicts video encoding quality
		LatencyTest = 6,
		InitialSettings = 7
	};

	template<typename T>
	static const T& ParseBuffer(const uint8*& Data, uint32& Size)
	{
		checkf(sizeof(T) <= Size, TEXT("%d - %d"), sizeof(T), Size);
		const T& Value = *reinterpret_cast<const T*>(Data);
		Data += sizeof(T);
		Size -= sizeof(T);
		return Value;
	}

	static FString ParseString(const uint8*& Data, uint32& Size)
	{
		uint16 StrLen = ParseBuffer<uint16>(Data, Size);
		checkf(StrLen * sizeof(TCHAR) <= Size, TEXT("%d - %d"), StrLen, Size);
		FString Res;
		Res.GetCharArray().SetNumUninitialized(StrLen + 1);
		FMemory::Memcpy(Res.GetCharArray().GetData(), Data, StrLen * sizeof(TCHAR));
		Res.GetCharArray()[StrLen] = '\0';
		Data += StrLen * sizeof(TCHAR);
		Size -= StrLen * sizeof(TCHAR);
		return Res;
	}

};
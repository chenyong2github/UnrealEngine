// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE::PixelStreaming::Protocol
{
	enum class EToStreamerMsg : uint8
	{
		/**********************************************************************/

		/*
		 * Control Messages. Range = 0..49.
		 */

		IFrameRequest = 0,
		RequestQualityControl = 1,
		FpsRequest = 2,
		AverageBitrateRequest = 3,
		StartStreaming = 4,
		StopStreaming = 5,
		LatencyTest = 6,
		RequestInitialSettings = 7,
		TestEcho = 8,

		/**********************************************************************/

		/*
		 * Input Messages. Range = 50+. (DO NOT ADD ANY NEW INPUT MESSAGE <50 ).
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

		// ARKit Transform Input Messages. Range = 100..109
		ARKitTransform = 100,

		/**********************************************************************/

		/*
		 * Ensure Count is the final entry.
		 */
		Count

		/**********************************************************************/
	};

	//! Messages that can be sent to the webrtc players
	enum class EToPlayerMsg : uint8
	{
		QualityControlOwnership = 0,
		Response = 1,
		Command = 2,
		FreezeFrame = 3,
		UnfreezeFrame = 4,
		VideoEncoderAvgQP = 5, // average Quantisation Parameter value of Video Encoder, roughly depicts video encoding quality
		LatencyTest = 6,
		InitialSettings = 7,
		FileExtension = 8,
		FileMimeType = 9,
		FileContents = 10,
		TestEcho = 11,
		InputControlOwnership = 12
	};

}; // namespace UE::PixelStreaming::Protocol

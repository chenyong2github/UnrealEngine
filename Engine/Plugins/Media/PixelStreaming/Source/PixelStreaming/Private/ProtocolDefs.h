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
		
		/**********************************************************************/

		/*
		 * Ensure Count is the final entry.
		 */
		Count

		/**********************************************************************/
	};

	//! Messages that can be sent to the webrtc players
	enum class EToPlayerMsg : uint8 {
		QualityControlOwnership, Response, Command, FreezeFrame, UnfreezeFrame
	};
};
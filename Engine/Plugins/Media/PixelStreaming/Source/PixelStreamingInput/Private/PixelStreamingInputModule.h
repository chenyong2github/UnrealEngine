// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputModule.h"

namespace UE::PixelStreamingInput
{
	class FPixelStreamingInputModule : public IPixelStreamingInputModule
	{
	public:
		virtual void RegisterMessage(EPixelStreamingMessageDirection MessageDirection, const FString& MessageType, FPixelStreamingInputMessage Message, const TFunction<void(FMemoryReader)>& Handler) override;
		virtual TFunction<void(FMemoryReader)> FindMessageHandler(const FString& MessageType) override;
		virtual TSharedPtr<IPixelStreamingInputHandler> GetInputHandler() override { return InputHandler; }

	private:
		/** IModuleInterface implementation */
		void StartupModule() override;
		void ShutdownModule() override;
		/** End IModuleInterface implementation */

		/** IInputDeviceModule implementation */
		virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
		/** End IInputDeviceModule implementation */

		// NOTE: There is only ever a single input handler which all of the streamers share. This provides
		// a central point for the multiple streamers -> single UE instance
		TSharedPtr<IPixelStreamingInputHandler> InputHandler;

		void PopulateProtocol();
	};
} // namespace UE::PixelStreamingInput
// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingInputModule.h"
#include "IPixelStreamingHMDModule.h"
#include "Framework/Application/SlateApplication.h"
#include "ApplicationWrapper.h"
#include "Settings.h"
#include "PixelStreamingInputHandler.h"

namespace UE::PixelStreamingInput
{
	void FPixelStreamingInputModule::StartupModule()
	{
		TSharedPtr<FPixelStreamingApplicationWrapper> PixelStreamerApplicationWrapper = MakeShareable(new FPixelStreamingApplicationWrapper(FSlateApplication::Get().GetPlatformApplication()));
		TSharedPtr<FGenericApplicationMessageHandler> BaseHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();
		InputHandler = MakeShared<FPixelStreamingInputHandler>(PixelStreamerApplicationWrapper, BaseHandler);

		Settings::InitialiseSettings();

		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	}

	void FPixelStreamingInputModule::ShutdownModule()
	{
		IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
	}

	void FPixelStreamingInputModule::RegisterMessage(EPixelStreamingMessageDirection MessageDirection, const FString& MessageType, FPixelStreamingInputMessage Message, const TFunction<void(FMemoryReader)>& Handler)
	{
		if (MessageDirection == EPixelStreamingMessageDirection::ToStreamer)
		{
			FPixelStreamingInputProtocol::ToStreamerProtocol.Add(MessageType, Message);
			InputHandler->RegisterMessageHandler(MessageType, Handler);
		}
		else if (MessageDirection == EPixelStreamingMessageDirection::FromStreamer)
		{
			FPixelStreamingInputProtocol::FromStreamerProtocol.Add(MessageType, Message);
		}
		OnProtocolUpdated.Broadcast();
	}

	TFunction<void(FMemoryReader)> FPixelStreamingInputModule::FindMessageHandler(const FString& MessageType)
	{
		return InputHandler->FindMessageHandler(MessageType);
	}

	TSharedPtr<IInputDevice> FPixelStreamingInputModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		return InputHandler;
	}
} // namespace UE::PixelStreamingInput

IMPLEMENT_MODULE(UE::PixelStreamingInput::FPixelStreamingInputModule, PixelStreamingInput)
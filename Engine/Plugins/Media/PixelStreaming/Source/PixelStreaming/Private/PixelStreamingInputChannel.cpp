// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingInputChannel.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"

namespace UE::PixelStreaming
{
	FPixelStreamingInputChannel::FPixelStreamingInputChannel()
		: PixelStreamerApplicationWrapper(MakeShareable(new FPixelStreamingApplicationWrapper(FSlateApplication::Get().GetPlatformApplication())))
	{
		TSharedPtr<FGenericApplicationMessageHandler> TargetHandler = FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler();
		MessageHandler = MakeShared<FPixelStreamingMessageHandler>(PixelStreamerApplicationWrapper, TargetHandler);
	}

	FPixelStreamingInputChannel::~FPixelStreamingInputChannel()
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->SetTargetWindow(nullptr);
		}
		if (PixelStreamerApplicationWrapper.IsValid())
		{
			PixelStreamerApplicationWrapper->SetTargetWindow(nullptr);
		}
	}

	void FPixelStreamingInputChannel::Tick(float DeltaTime)
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->Tick(DeltaTime);
		}
	}

	void FPixelStreamingInputChannel::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		MessageHandler->SetTargetHandler(InMessageHandler);
	}

	bool FPixelStreamingInputChannel::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		return GEngine->Exec(InWorld, Cmd, Ar);
	}

	void FPixelStreamingInputChannel::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		// TODO: Implement FFB
	}

	void FPixelStreamingInputChannel::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values)
	{
		// TODO: Implement FFB
	}

	void FPixelStreamingInputChannel::OnMessage(const webrtc::DataBuffer& Buffer)
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->OnMessage(Buffer);
		}
	}

	void FPixelStreamingInputChannel::SetTargetWindow(TWeakPtr<SWindow> InWindow)
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->SetTargetWindow(InWindow);
		}
		if (PixelStreamerApplicationWrapper.IsValid())
		{
			PixelStreamerApplicationWrapper->SetTargetWindow(InWindow);
		}
	}

	TWeakPtr<SWindow> FPixelStreamingInputChannel::GetTargetWindow()
	{
		if (MessageHandler.IsValid())
		{
			return MessageHandler->GetTargetWindow();
		}
		return nullptr;
	}

	void FPixelStreamingInputChannel::SetTargetScreenSize(TWeakPtr<FIntPoint> InScreenSize)
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->SetTargetScreenSize(InScreenSize);
		}
	}

	TWeakPtr<FIntPoint> FPixelStreamingInputChannel::GetTargetScreenSize()
	{
		if (MessageHandler.IsValid())
		{
			return MessageHandler->GetTargetScreenSize();
		}
		return nullptr;
	}

	void FPixelStreamingInputChannel::SetTargetViewport(TWeakPtr<SViewport> InViewport)
	{
		if (MessageHandler.IsValid())
		{
			MessageHandler->SetTargetViewport(InViewport);
		}
	}

	TWeakPtr<SViewport> FPixelStreamingInputChannel::GetTargetViewport()
	{
		if (MessageHandler.IsValid())
		{
			return MessageHandler->GetTargetViewport();
		}
		return nullptr;
	}

    bool FPixelStreamingInputChannel::IsFakingTouchEvents() const
    {
        return FSlateApplication::Get().IsFakingTouchEvents();
    }

    void FPixelStreamingInputChannel::RegisterHandler(const FString& MessageType, const TFunction<void(FMemoryReader)>& Handler)
    {
        if(MessageHandler.IsValid())
        {
            MessageHandler->RegisterHandler(MessageType, Handler);
        }
    }
} // namespace UE::PixelStreaming

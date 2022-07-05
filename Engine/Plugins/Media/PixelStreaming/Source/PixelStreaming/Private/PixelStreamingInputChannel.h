// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputChannel.h"
#include "IPixelStreamingModule.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "WebRTCIncludes.h"
#include "PixelStreamingMessageHandler.h"
#include "PixelStreamingApplicationWrapper.h"

namespace UE::PixelStreaming
{
    /**
     * FPixelStreamingInputChannel implements the IInputDevice interface and routes input messages to it's FPixelStreamingMessageHandler, 
     * whilst handling application specifics in FPixelStreamingApplicationWrapper
     */
    class FPixelStreamingInputChannel : public IPixelStreamingInputChannel
    {
    public:
        FPixelStreamingInputChannel();

        ~FPixelStreamingInputChannel();

        virtual void Tick(float DeltaTime) override;

        /** Poll for controller state and send events if needed */
		virtual void SendControllerEvents() override { };

		/** Set which MessageHandler will route input  */
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

		/** Exec handler to allow console commands to be passed through for debugging */
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

        virtual void OnMessage(const webrtc::DataBuffer& Buffer) override;

        /**
		* IForceFeedbackSystem pass through functions
		*/
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;

		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values) override;

        void SetTargetWindow(TWeakPtr<SWindow> InWindow) override;
        TWeakPtr<SWindow> GetTargetWindow() override;

		virtual bool IsFakingTouchEvents() const override;

        virtual void SetTargetViewport(FSceneViewport* InTargetViewport) override;
		virtual FSceneViewport* GetTargetViewport() override;

    protected:
        TSharedPtr<class FPixelStreamingMessageHandler> MessageHandler;

        /** For convenience we keep a reference to the Pixel Streaming plugin. */
		IPixelStreamingModule* PixelStreamingModule;

        /**
		* A special wrapper over the GenericApplication layer which allows us to
		* override certain behavior.
		*/
		TSharedPtr<class FPixelStreamingApplicationWrapper> PixelStreamerApplicationWrapper;
    };
}
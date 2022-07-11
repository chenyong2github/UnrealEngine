// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "WebRTCIncludes.h"
#include "IPixelStreamingModule.h"
#include "InputCoreTypes.h"
#include "PixelStreamingApplicationWrapper.h"

DECLARE_DELEGATE_OneParam(FMessageDispatch, FMemoryReader&);

namespace UE::PixelStreaming
{
    class FPixelStreamingMessageHandler
    {
    public:
        FPixelStreamingMessageHandler(TSharedPtr<FPixelStreamingApplicationWrapper> InApplicationWrapper, const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler);

        virtual ~FPixelStreamingMessageHandler();

        void Tick(const float InDeltaTime);

        void OnMessage(const webrtc::DataBuffer& Buffer);

        void SetTargetWindow(TWeakPtr<SWindow> InWindow);
        TWeakPtr<SWindow> GetTargetWindow();

        void SetTargetViewport(FSceneViewport* InViewport);
        FSceneViewport* GetTargetViewport();

        void SetTargetHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler);

        bool IsFakingTouchEvents() const { return bFakingTouchEvents; }

    protected:
        /**
         * Key press handling
         */
        virtual void HandleOnKeyChar(FMemoryReader& Ar);
        virtual void HandleOnKeyDown(FMemoryReader& Ar);
        virtual void HandleOnKeyUp(FMemoryReader& Ar);
        /**
         * Touch handling
         */
        virtual void HandleOnTouchStarted(FMemoryReader& Ar);
        virtual void HandleOnTouchMoved(FMemoryReader& Ar);
        virtual void HandleOnTouchEnded(FMemoryReader& Ar);
        /**
         * Controller handling
         */
    	UE_DEPRECATED(5.1, "This version of HandleOnControllerAnalog is deprecated, use HandleOnControllerAnalogWithPlatformUser instead.")
        virtual void HandleOnControllerAnalog(FMemoryReader& Ar);
    	UE_DEPRECATED(5.1, "This version of HandleOnControllerButtonPressed is deprecated, use HandleOnControllerButtonPressedWithPlatformUser instead.")
        virtual void HandleOnControllerButtonPressed(FMemoryReader& Ar);
    	UE_DEPRECATED(5.1, "This version of HandleOnControllerButtonReleased is deprecated, use HandleOnControllerButtonReleasedWithPlatformUser instead.")
        virtual void HandleOnControllerButtonReleased(FMemoryReader& Ar);

    	virtual void HandleOnControllerAnalogWithPlatformUser(FMemoryReader& Ar);
    	virtual void HandleOnControllerButtonPressedWithPlatformUser(FMemoryReader& Ar);
    	virtual void HandleOnControllerButtonReleasedWithPlatformUser(FMemoryReader& Ar);
    	
        /**
         * Mouse handling
         */
        virtual void HandleOnMouseEnter(FMemoryReader& Ar);
        virtual void HandleOnMouseLeave(FMemoryReader& Ar);
        virtual void HandleOnMouseUp(FMemoryReader& Ar);
        virtual void HandleOnMouseDown(FMemoryReader& Ar);
        virtual void HandleOnMouseMove(FMemoryReader& Ar);
        virtual void HandleOnMouseWheel(FMemoryReader& Ar);
        /**
         * Command handling
         */
        virtual void HandleCommand(FMemoryReader& Ar);
        /**
         * UI Interaction handling
         */
        virtual void HandleUIInteraction(FMemoryReader& Ar);
        /**
         * ARKit Transform handling
         */
        virtual void HandleARKitTransform(FMemoryReader& Ar);

	    FIntPoint ConvertFromNormalizedScreenLocation(const FVector2D& ScreenLocation, bool bIncludeOffset = true);

        FGamepadKeyNames::Type ConvertAxisIndexToGamepadAxis(uint8 AnalogAxis);
        FGamepadKeyNames::Type ConvertButtonIndexToGamepadButton(uint8 ButtonIndex);

        void FindFocusedWidget();
        bool FilterKey(const FKey& Key);

        struct FMessage
        {
            FMessageDispatch* Dispatch;
            TArray<uint8> Data;
        };

        TWeakPtr<SWindow>					            TargetWindow;
    	FSceneViewport*         			            TargetViewport;
        uint8                                           NumActiveTouches;
        bool                                            bIsMouseActive;
        TMap<uint8, FMessageDispatch>                   DispatchTable;
        TQueue<FMessage>                                Messages;

        /** Reference to the message handler which events should be passed to. */
		TSharedPtr<FGenericApplicationMessageHandler>   MessageHandler;

        /** For convenience we keep a reference to the Pixel Streaming plugin. */
		IPixelStreamingModule*                          PixelStreamingModule;

        /** For convenience, we keep a reference to the application wrapper owned by the input channel */
        TSharedPtr<FPixelStreamingApplicationWrapper>   PixelStreamerApplicationWrapper;

        /**
		* Is the application faking touch events by dragging the mouse along
		* the canvas? If so then we must put the browser canvas in a special
		* state to replicate the behavior of the application.
		*/
		bool bFakingTouchEvents;

        /**
		* Touch only. Location of the focused UI widget. If no UI widget is focused
		* then this has the UnfocusedPos value.
		*/
		FVector2D FocusedPos;

        /**
		* Touch only. A special position which indicates that no UI widget is
		* focused.
		*/
		const FVector2D UnfocusedPos;

        /*
		* Padding for string parsing when handling messages.
		* 1 character for the actual message and then
		* 2 characters for the length which are skipped
		*/
		const size_t MessageHeaderOffset = 1;
    };
}
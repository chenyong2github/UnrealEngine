// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealitySpatialInput.h"

#include "MixedRealityInterop.h"
#include "WindowsMixedRealityStatics.h"
#include "WindowsMixedRealityInteropUtility.h"
#include "Misc/Parse.h"
#include "WindowsMixedRealitySpatialInputFunctionLibrary.h"
#include "WindowsMixedRealitySpatialInputTypes.h"
#include "WindowsMixedRealityAvailability.h"

#define LOCTEXT_NAMESPACE "WindowsMixedRealitySpatialInput"
#define MotionControllerDeviceTypeName "WindowsMixedRealitySpatialInput"

#define WindowsMixedRealityCategory "WindowsMixedRealitySubCategory"
#define WindowsMixedRealityCategoryName "WindowsMixedReality"
#define WindowsMixedRealityCategoryFriendlyName "Windows Mixed Reality"

namespace WindowsMixedReality
{
	FWindowsMixedRealitySpatialInput::FWindowsMixedRealitySpatialInput(
		const TSharedRef< FGenericApplicationMessageHandler > & InMessageHandler)
		: MessageHandler(InMessageHandler)
	{
		InitializeSpatialInput();
	}

	FWindowsMixedRealitySpatialInput::~FWindowsMixedRealitySpatialInput()
	{
		FWindowsMixedRealityStatics::OnConfigureGesturesDelegate.Remove(FWindowsMixedRealityStatics::ConfigureGesturesHandle);
		FWindowsMixedRealityStatics::ConfigureGesturesHandle.Reset();

		UninitializeSpatialInput();
	}

	void FWindowsMixedRealitySpatialInput::Tick(float DeltaTime)
	{
		if (!FWindowsMixedRealityStatics::SupportsSpatialInput())
		{
			return;
		}

		if (!IsInitialized)
		{
			// We failed to initialize in the constructor. Try again.
			InitializeSpatialInput();
			return;
		}
	}

	void FWindowsMixedRealitySpatialInput::SendControllerEvents()
	{
#if WITH_WINDOWS_MIXED_REALITY
		SendQueuedButtonAndAxisEvents();

		if (!FWindowsMixedRealityStatics::PollInput())
		{
			return;
		}

		const uint32 sourceId = 0;
		SendButtonEvents(sourceId);
		SendAxisEvents(sourceId);
#endif
	}

#if WITH_WINDOWS_MIXED_REALITY
	void SendControllerButtonEvent(
		TSharedPtr< FGenericApplicationMessageHandler > messageHandler,
		uint32 controllerId,
		FKey button,
		HMDInputPressState pressState) noexcept
	{
		check(IsInGameThread());

		FName buttonName = button.GetFName();

		if (pressState == HMDInputPressState::NotApplicable)
		{
			// No event should be sent.
			return;
		}

		if (pressState == HMDInputPressState::Pressed)
		{
			// Send the press event.
			messageHandler->OnControllerButtonPressed(
				buttonName,
				static_cast<int32>(controllerId),
				false);
		}
		else
		{
			// Send the release event
			messageHandler->OnControllerButtonReleased(
				buttonName,
				static_cast<int32>(controllerId),
				false);
		}
	}

	void SendControllerAxisEvent(
		TSharedPtr< FGenericApplicationMessageHandler > messageHandler,
		uint32 controllerId,
		FKey axis,
		double axisPosition) noexcept
	{
		check(IsInGameThread());

		FName axisName = axis.GetFName();

		messageHandler->OnControllerAnalog(
			axisName,
			static_cast<int32>(controllerId),
			static_cast<float>(axisPosition));
	}

	FKey GetThumbstickDirectionKey(FVector2D TouchDir, HMDHand Hand)
	{
		const FVector2D UpDir(0.f, 1.f);
		const FVector2D RightDir(1.f, 0.f);

		const float VerticalDot = TouchDir | UpDir;
		const float RightDot = TouchDir | RightDir;

		if (VerticalDot >= UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_Up :
				EKeys::MixedReality_Right_Thumbstick_Up;
		}
		else if (VerticalDot <= -UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_Down :
				EKeys::MixedReality_Right_Thumbstick_Down;
		}
		else if (RightDot <= -UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_Left :
				EKeys::MixedReality_Right_Thumbstick_Left;
		}
		else if (RightDot >= UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_Right :
				EKeys::MixedReality_Right_Thumbstick_Right;
		}
		return EKeys::Invalid;
	}

	FKey GetTouchpadDirectionKey(FVector2D TouchDir, HMDHand Hand)
	{
		const FVector2D UpDir(0.f, 1.f);
		const FVector2D RightDir(1.f, 0.f);

		const float VerticalDot = TouchDir | UpDir;
		const float RightDot = TouchDir | RightDir;

		if (VerticalDot >= UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_Up :
				EKeys::MixedReality_Right_Trackpad_Up;
		}
		else if (VerticalDot <= -UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_Down :
				EKeys::MixedReality_Right_Trackpad_Down;
		}
		else if (RightDot <= -UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_Left :
				EKeys::MixedReality_Right_Trackpad_Left;
		}
		else if (RightDot >= UE_HALF_SQRT_2)
		{
			return (Hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_Right :
				EKeys::MixedReality_Right_Trackpad_Right;
		}
		return EKeys::Invalid;
	}

	// Gesture events come in from some microsoft thread, so we have to queue them up and send them from the game thread
	// to avoid problems with systems that handle the events directly (UI is an example).
	void FWindowsMixedRealitySpatialInput::EnqueueControllerButtonEvent(
		uint32 controllerId,
		FKey button,
		HMDInputPressState pressState) noexcept
	{
		FScopeLock IndexLock(&EnqueuedContollerEventBufferWriteIndexMutex);
		TArray<FEnqueuedControllerEvent>& WriteBuffer = EnqueuedControllerEventBuffers[EnqueuedContollerEventBufferWriteIndex];
		WriteBuffer.Add(FEnqueuedControllerEvent(controllerId, button, pressState));
	}

	void FWindowsMixedRealitySpatialInput::EnqueueControllerAxisEvent(
		uint32 controllerId,
		FKey axis,
		double axisPosition) noexcept
	{
		FScopeLock IndexLock(&EnqueuedContollerEventBufferWriteIndexMutex);
		TArray<FEnqueuedControllerEvent>& WriteBuffer = EnqueuedControllerEventBuffers[EnqueuedContollerEventBufferWriteIndex];
		WriteBuffer.Add(FEnqueuedControllerEvent(controllerId, axis, axisPosition));
	}

	void FWindowsMixedRealitySpatialInput::SendQueuedButtonAndAxisEvents()
	{
		check(IsInGameThread());

		// Flip the buffer
		{
			FScopeLock IndexLock(&EnqueuedContollerEventBufferWriteIndexMutex);
			EnqueuedContollerEventBufferWriteIndex = 1 - EnqueuedContollerEventBufferWriteIndex;
		}

		// Send any queued events FIFO
		TArray<FEnqueuedControllerEvent>& ReadBuffer = EnqueuedControllerEventBuffers[1 - EnqueuedContollerEventBufferWriteIndex];

		for (const FEnqueuedControllerEvent& Event : ReadBuffer)
		{
			if (Event.bIsAxis)
			{
				SendControllerAxisEvent(MessageHandler, Event.ControllerId, Event.Key, Event.AxisPosition);
			}
			else
			{
				SendControllerButtonEvent(MessageHandler, Event.ControllerId, Event.Key, Event.PressState);
			}
		}

		// Clear the buffer
		ReadBuffer.Empty();
	}
#endif


#if WITH_WINDOWS_MIXED_REALITY
	void FWindowsMixedRealitySpatialInput::SendAxisEvents(uint32 source)
	{
		FKey key;
		FVector2D position;

		for (int i = 0; i < 2; i++)
		{
			HMDHand hand = (HMDHand)i;

			// Trigger
			position.X = FWindowsMixedRealityStatics::GetAxisPosition(hand, HMDInputControllerAxes::SelectValue);
			key = (hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trigger_Axis :
				EKeys::MixedReality_Right_Trigger_Axis;

			SendControllerAxisEvent(MessageHandler, source, key, position.X);

			// Thumbstick X
			position.X = FWindowsMixedRealityStatics::GetAxisPosition(hand, HMDInputControllerAxes::ThumbstickX);
			key = (hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_X :
				EKeys::MixedReality_Right_Thumbstick_X;

			SendControllerAxisEvent(MessageHandler, source, key, position.X);

			// Thumbstick Y
			position.Y = FWindowsMixedRealityStatics::GetAxisPosition(hand, HMDInputControllerAxes::ThumbstickY);
			key = (hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Thumbstick_Y :
				EKeys::MixedReality_Right_Thumbstick_Y;

			SendControllerAxisEvent(MessageHandler, source, key, position.Y);

			// Thumbstick direction
			if (position.Size() > 0.5f)
			{
				key = GetThumbstickDirectionKey(position, hand);
				if (key.IsValid())
				{
					if (ThumbstickDirection[i].IsValid() && key != ThumbstickDirection[i])
					{
						SendControllerButtonEvent(MessageHandler, source, ThumbstickDirection[i], HMDInputPressState::Released);
					}
					SendControllerButtonEvent(MessageHandler, source, key, HMDInputPressState::Pressed);
				}
				ThumbstickDirection[i] = key;
			}
			else
			{
				if (ThumbstickDirection[i].IsValid())
				{
					SendControllerButtonEvent(MessageHandler, source, ThumbstickDirection[i], HMDInputPressState::Released);
				}
				ThumbstickDirection[i] = EKeys::Invalid;
			}

			// Touchpad X
			position.X = FWindowsMixedRealityStatics::GetAxisPosition(hand, HMDInputControllerAxes::TouchpadX);
			key = (hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_X :
				EKeys::MixedReality_Right_Trackpad_X;

			if ((key == EKeys::MixedReality_Left_Trackpad_X && !isLeftTouchpadTouched) ||
				(key == EKeys::MixedReality_Right_Trackpad_X && !isRightTouchpadTouched))
			{
				position.X = 0.0f;
			}

			SendControllerAxisEvent(MessageHandler, source, key, position.X);

			// Touchpad Y
			position.Y = FWindowsMixedRealityStatics::GetAxisPosition(hand, HMDInputControllerAxes::TouchpadY);
			key = (hand == HMDHand::Left) ?
				EKeys::MixedReality_Left_Trackpad_Y :
				EKeys::MixedReality_Right_Trackpad_Y;

			if ((key == EKeys::MixedReality_Left_Trackpad_Y && !isLeftTouchpadTouched) ||
				(key == EKeys::MixedReality_Right_Trackpad_Y && !isRightTouchpadTouched))
			{
				position.Y = 0.0f;
			}

			SendControllerAxisEvent(MessageHandler, source, key, position.Y);

			// Touchpad direction
			if (position.Size() > 0.5f)
			{
				key = GetTouchpadDirectionKey(position, hand);
				if (key.IsValid())
				{
					if (TouchpadDirection[i].IsValid() && key != TouchpadDirection[i])
					{
						SendControllerButtonEvent(MessageHandler, source, TouchpadDirection[i], HMDInputPressState::Released);
					}
					SendControllerButtonEvent(MessageHandler, source, key, HMDInputPressState::Pressed);
				}
				TouchpadDirection[i] = key;
			}
			else
			{
				if (TouchpadDirection[i].IsValid())
				{
					SendControllerButtonEvent(MessageHandler, source, TouchpadDirection[i], HMDInputPressState::Released);
				}
				TouchpadDirection[i] = EKeys::Invalid;
			}
		}
	}

	void FWindowsMixedRealitySpatialInput::SendButtonEvents(uint32 source)
	{
		HMDInputPressState pressState;
		FKey key;

		for (int i = 0; i < 2; i++)
		{
			HMDHand hand = (HMDHand)i;

			// Select
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::Select);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Trigger_Click :
					EKeys::MixedReality_Right_Trigger_Click;

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}

			// Grasp
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::Grasp);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Grip_Click :
					EKeys::MixedReality_Right_Grip_Click;

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}

			// Menu
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::Menu);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Menu_Click :
					EKeys::MixedReality_Right_Menu_Click;

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}

			// Thumbstick press
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::Thumbstick);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Thumbstick_Click :
					EKeys::MixedReality_Right_Thumbstick_Click;

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}

			// Touchpad press
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::Touchpad);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Trackpad_Click :
					EKeys::MixedReality_Right_Trackpad_Click;

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}

			// Touchpad touch
			pressState = FWindowsMixedRealityStatics::GetPressState(hand, HMDInputControllerButtons::TouchpadIsTouched);
			if (pressState != HMDInputPressState::NotApplicable)
			{
				key = (hand == HMDHand::Left) ?
					EKeys::MixedReality_Left_Trackpad_Touch :
					EKeys::MixedReality_Right_Trackpad_Touch;

				if (key == EKeys::MixedReality_Left_Trackpad_Touch)
				{
					isLeftTouchpadTouched = pressState == HMDInputPressState::Pressed;
				}
				else if (key == EKeys::MixedReality_Right_Trackpad_Touch)
				{
					isRightTouchpadTouched = pressState == HMDInputPressState::Pressed;
				}

				SendControllerButtonEvent(MessageHandler, source, key, pressState);
			}
		}
	}
#endif

	void FWindowsMixedRealitySpatialInput::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		MessageHandler = InMessageHandler;
	}

	bool FWindowsMixedRealitySpatialInput::Exec(UWorld * InWorld, const TCHAR * Cmd, FOutputDevice & Ar)
	{
		if (FParse::Command(&Cmd, TEXT("windowsmr.CaptureGesture")))
		{
			uint32 LocalCapturingSet = 0;
			FString Arg;
			while (FParse::Token(Cmd, Arg, false))
			{
				if (Arg.Equals(TEXT("Tap"), ESearchCase::IgnoreCase))
				{
					LocalCapturingSet |= (uint32)EGestureType::TapGesture;
				}
				else if (Arg.Equals(TEXT("Hold"), ESearchCase::IgnoreCase))
				{
					LocalCapturingSet |= (uint32)EGestureType::HoldGesture;
				}
				else if (Arg.Equals(TEXT("Manipulation"), ESearchCase::IgnoreCase))
				{
					LocalCapturingSet |= (uint32)EGestureType::ManipulationGesture;
				}
				else if (Arg.Equals(TEXT("Navigation"), ESearchCase::IgnoreCase))
				{
					LocalCapturingSet |= (uint32)EGestureType::NavigationGesture;
				}
				else if (Arg.Equals(TEXT("NavigationRails"), ESearchCase::IgnoreCase))
				{
					LocalCapturingSet |= (uint32)EGestureType::NavigationRailsGesture;
				}
			}

			return CaptureGestures(LocalCapturingSet);
		}

		return false;
	}

	bool FWindowsMixedRealitySpatialInput::CaptureGestures(uint32 capturingSet)
	{
#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		CapturingSet = capturingSet;

		FString errorMsg;
		if (!UpdateGestureCallbacks(errorMsg))
		{
			UE_LOG(LogCore, Error, TEXT("%s, Gesture capturing disabled"), *errorMsg);
			CapturingSet = 0;
			GestureRecognizer->Reset();
			return false;
		}

		return true;
#else
		UE_LOG(LogCore, Warning, TEXT("WindowsMixedReality Gesture capturing not supported on this platform or windows sdk version.  Gestures will not be detected."));
		return false;
#endif
	}


	bool FWindowsMixedRealitySpatialInput::UpdateGestureCallbacks(FString& errorMsg)
	{
#if WITH_WINDOWS_MIXED_REALITY
#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		using std::placeholders::_1;
		using std::placeholders::_2;
		using std::placeholders::_3;

		GestureRecognizer->Reset();

		if (CapturingSet & (uint32)EGestureType::TapGesture)
		{
			if (!GestureRecognizer->SubscribeTap(std::bind(&FWindowsMixedRealitySpatialInput::TapCallback, this, _1, _2, _3)))
			{
				errorMsg = (TEXT("WindowsMixedRealitySpatialInput couldn't subscribe to Tap event"));
				return false;
			}
		}
		if (CapturingSet & (uint32)EGestureType::HoldGesture)
		{
			if (!GestureRecognizer->SubscribeHold(std::bind(&FWindowsMixedRealitySpatialInput::HoldCallback, this, _1, _2, _3)))
			{
				errorMsg = (TEXT("WindowsMixedRealitySpatialInput couldn't subscribe to Hold event"));
				return false;
			}
		}
		if (CapturingSet & (uint32)EGestureType::ManipulationGesture)
		{
			check(!(CapturingSet & (uint32)EGestureType::NavigationGesture || CapturingSet & (uint32)EGestureType::NavigationRailsGesture));

			if (!GestureRecognizer->SubscribeManipulation(std::bind(&FWindowsMixedRealitySpatialInput::ManipulationCallback, this, _1, _2, _3)))
			{
				errorMsg = (TEXT("WindowsMixedRealitySpatialInput couldn't subscribe to Manipulation event"));
				return false;
			}
		}
		if (CapturingSet & (uint32)EGestureType::NavigationGesture)
		{
			check(!(CapturingSet & (uint32)EGestureType::ManipulationGesture || CapturingSet & (uint32)EGestureType::NavigationRailsGesture));

			unsigned int Axes = 0;
			if (CapturingSet & (uint32)EGestureType::NavigationGestureX)
			{
				Axes |= GestureRecognizerInterop::NavigationY;
			}
			if (CapturingSet & (uint32)EGestureType::NavigationGestureY)
			{
				Axes |= GestureRecognizerInterop::NavigationZ;
			}
			if (CapturingSet & (uint32)EGestureType::NavigationGestureZ)
			{
				Axes |= GestureRecognizerInterop::NavigationX;
			}
			if (Axes == 0)
			{
				UE_LOG(LogCore, Warning, TEXT("CaptureGestures is set to capture Navigation, but no axis.  This will work, but it's wierd enough that it is probably a mistake."));
			}

			if (!GestureRecognizer->SubscribeNavigation(std::bind(&FWindowsMixedRealitySpatialInput::NavigationCallback, this, _1, _2, _3),
				Axes))
			{
				errorMsg = (TEXT("WindowsMixedRealitySpatialInput couldn't subscribe to Navigation event"));
				return false;
			}
		}
		if (CapturingSet & (uint32)EGestureType::NavigationRailsGesture)
		{
			check(!(CapturingSet & (uint32)EGestureType::NavigationGesture || CapturingSet & (uint32)EGestureType::ManipulationGesture));

			// Convert unreal axis to WMR axes
			unsigned int Axes = 0;
			if (CapturingSet & (uint32)EGestureType::NavigationGestureX)
			{
				Axes |= GestureRecognizerInterop::NavigationRailsY;
			}
			if (CapturingSet & (uint32)EGestureType::NavigationGestureY)
			{
				Axes |= GestureRecognizerInterop::NavigationRailsZ;
			}
			if (CapturingSet & (uint32)EGestureType::NavigationGestureZ)
			{
				Axes |= GestureRecognizerInterop::NavigationRailsX;
			}
			if (Axes == 0)
			{
				UE_LOG(LogCore, Warning, TEXT("CaptureGestures is set to capture NavigationRails, but no axis.  This will work, but it's wierd enough that it is probably a mistake."));
			}

			if (!GestureRecognizer->SubscribeNavigation(std::bind(&FWindowsMixedRealitySpatialInput::NavigationCallback, this, _1, _2, _3),
				Axes))
			{
				errorMsg = (TEXT("WindowsMixedRealitySpatialInput couldn't subscribe to NavigationRails event"));
				return false;
			}
		}
#else // SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		UE_LOG(LogCore, Warning, TEXT("WindowsMixedReality CaptureGesture called, but the current platform or interop sdk version does not support gestures."));
		errorMsg = (TEXT("WindowsMixedReality CaptureGesture called, but the current platform or interop sdk version does not support gestures."));
		return false;
#endif // SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
#endif // WITH_WINDOWS_MIXED_REALITY
		return true;
	}


#if WITH_WINDOWS_MIXED_REALITY
	// Note, these callbacks come in from a microsoft created thread.  We need to queue the events and dispatch from the game thread.

	void FWindowsMixedRealitySpatialInput::TapCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Tap& desc)
	{
		if (stage == GestureStage::Completed)
		{
			if (desc.Count == 1)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::TapGesture, HMDInputPressState::Released);
				EnqueueControllerButtonEvent(0, desc.Hand == HMDHand::Left ? FSpatialInputKeys::LeftTapGesture : FSpatialInputKeys::RightTapGesture, HMDInputPressState::Released);
			}
			else if (desc.Count == 2)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::DoubleTapGesture, HMDInputPressState::Released);
				EnqueueControllerButtonEvent(0, desc.Hand == HMDHand::Left ? FSpatialInputKeys::LeftDoubleTapGesture : FSpatialInputKeys::RightDoubleTapGesture, HMDInputPressState::Released);
			}
		}
	}

	void FWindowsMixedRealitySpatialInput::HoldCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Hold& desc)
	{
		if (stage == GestureStage::Started)
		{
			EnqueueControllerButtonEvent(0, FSpatialInputKeys::HoldGesture, HMDInputPressState::Pressed);
			EnqueueControllerButtonEvent(0, desc.Hand == HMDHand::Left ? FSpatialInputKeys::LeftHoldGesture : FSpatialInputKeys::RightHoldGesture, HMDInputPressState::Pressed);
		}
		else if (stage == GestureStage::Completed || stage == GestureStage::Canceled)
		{
			EnqueueControllerButtonEvent(0, FSpatialInputKeys::HoldGesture, HMDInputPressState::Released);
			EnqueueControllerButtonEvent(0, desc.Hand == HMDHand::Left ? FSpatialInputKeys::LeftHoldGesture : FSpatialInputKeys::RightHoldGesture, HMDInputPressState::Released);
		}
	}

	void FWindowsMixedRealitySpatialInput::ManipulationCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Manipulation& desc)
	{
		FVector Delta = WMRUtility::FromMixedRealityVector(desc.Delta);

		if (desc.Hand == HMDHand::Left)
		{
			if (stage == GestureStage::Started)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::LeftManipulationGesture, HMDInputPressState::Pressed);
			}

			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftManipulationXGesture, Delta.X);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftManipulationYGesture, Delta.Y);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftManipulationZGesture, Delta.Z);

			if (stage == GestureStage::Completed || stage == GestureStage::Canceled)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::LeftManipulationGesture, HMDInputPressState::Released);
			}
		}
		else if (desc.Hand == HMDHand::Right)
		{
			if (stage == GestureStage::Started)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::RightManipulationGesture, HMDInputPressState::Pressed);
			}

			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightManipulationXGesture, Delta.X);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightManipulationYGesture, Delta.Y);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightManipulationZGesture, Delta.Z);

			if (stage == GestureStage::Completed || stage == GestureStage::Canceled)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::RightManipulationGesture, HMDInputPressState::Released);
			}
		}
	}

	void FWindowsMixedRealitySpatialInput::NavigationCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Navigation& desc)
	{
		FVector NormalizedOffset = WMRUtility::FromMixedRealityVector(desc.NormalizedOffset);

		if (desc.Hand == HMDHand::Left)
		{
			if (stage == GestureStage::Started)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::LeftNavigationGesture, HMDInputPressState::Pressed);
			}

			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftNavigationXGesture, NormalizedOffset.X);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftNavigationYGesture, NormalizedOffset.Y);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::LeftNavigationZGesture, NormalizedOffset.Z);

			if (stage == GestureStage::Completed || stage == GestureStage::Canceled)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::LeftNavigationGesture, HMDInputPressState::Released);
			}
		}
		else if (desc.Hand == HMDHand::Right)
		{
			if (stage == GestureStage::Started)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::RightNavigationGesture, HMDInputPressState::Pressed);
			}

			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightNavigationXGesture, NormalizedOffset.X);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightNavigationYGesture, NormalizedOffset.Y);
			EnqueueControllerAxisEvent(0, FSpatialInputKeys::RightNavigationZGesture, NormalizedOffset.Z);

			if (stage == GestureStage::Completed || stage == GestureStage::Canceled)
			{
				EnqueueControllerButtonEvent(0, FSpatialInputKeys::RightNavigationGesture, HMDInputPressState::Released);
			}
		}
	}
#endif

	void FWindowsMixedRealitySpatialInput::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		// Large channel type maps to amplitude. We are interested in amplitude.
		if ((ChannelType == FForceFeedbackChannelType::LEFT_LARGE) ||
			(ChannelType == FForceFeedbackChannelType::RIGHT_LARGE))
		{
			// SpatialInteractionController supports SimpleHapticsController. Amplitude is the value
			// we need to send. Set Frequency to 1.0f so that the amplitude is properly sent to the
			// controller.
			FHapticFeedbackValues hapticValues = FHapticFeedbackValues(1.0f, Value);
			EControllerHand controllerHand = (ChannelType == FForceFeedbackChannelType::LEFT_LARGE) ?
				EControllerHand::Left : EControllerHand::Right;

			SetHapticFeedbackValues(
				ControllerId,
				(int32)controllerHand,
				hapticValues);
		}
	}

	void FWindowsMixedRealitySpatialInput::SetChannelValues(int32 ControllerId, const FForceFeedbackValues & values)
	{
		FHapticFeedbackValues leftHaptics = FHapticFeedbackValues(
			values.LeftSmall,		// frequency
			values.LeftLarge);		// amplitude
		FHapticFeedbackValues rightHaptics = FHapticFeedbackValues(
			values.RightSmall,		// frequency
			values.RightLarge);		// amplitude

		SetHapticFeedbackValues(
			ControllerId,
			(int32)EControllerHand::Left,
			leftHaptics);

		SetHapticFeedbackValues(
			ControllerId,
			(int32)EControllerHand::Right,
			rightHaptics);
	}

	void FWindowsMixedRealitySpatialInput::SetHapticFeedbackValues(int32 ControllerId, int32 DeviceHand, const FHapticFeedbackValues & Values)
	{
		EControllerHand controllerHand = (EControllerHand)DeviceHand;
		if ((controllerHand != EControllerHand::Left) &&
			(controllerHand != EControllerHand::Right))
		{
			return;
		}

#if WITH_WINDOWS_MIXED_REALITY
		HMDHand hand = (HMDHand)DeviceHand;
		FWindowsMixedRealityStatics::SubmitHapticValue(hand, (Values.Frequency > 0.0f) ? Values.Amplitude : 0.0f);
#endif
	}

	void FWindowsMixedRealitySpatialInput::GetHapticFrequencyRange(float & MinFrequency, float & MaxFrequency) const
	{
		MinFrequency = 0.0f;
		MaxFrequency = 1.0f;
	}

	float FWindowsMixedRealitySpatialInput::GetHapticAmplitudeScale() const
	{
		return 1.0f;
	}

	FName FWindowsMixedRealitySpatialInput::GetMotionControllerDeviceTypeName() const
	{
		const static FName DeviceTypeName(TEXT(MotionControllerDeviceTypeName));
		return DeviceTypeName;
	}

	bool FWindowsMixedRealitySpatialInput::GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator & OutOrientation, FVector & OutPosition, float WorldToMetersScale) const
	{
#if WITH_WINDOWS_MIXED_REALITY
		HMDHand hand = (HMDHand)((int)DeviceHand);

		bool success = FWindowsMixedRealityStatics::GetControllerOrientationAndPosition(hand, OutOrientation, OutPosition);
		OutPosition *= WorldToMetersScale;

		return success;
#else
		return false;
#endif
	}

	ETrackingStatus FWindowsMixedRealitySpatialInput::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
	{
#if WITH_WINDOWS_MIXED_REALITY
		HMDHand hand = (HMDHand)((int)DeviceHand);
		HMDTrackingStatus trackingStatus = FWindowsMixedRealityStatics::GetControllerTrackingStatus(hand);

		return (ETrackingStatus)((int)trackingStatus);
#else
		return ETrackingStatus::NotTracked;
#endif
	}

	void FWindowsMixedRealitySpatialInput::RegisterKeys() noexcept
	{
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::TapGesture, LOCTEXT(TapGestureName, "Windows Spatial Input Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::DoubleTapGesture, LOCTEXT(DoubleTapGestureName, "Windows Spatial Input Double Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::HoldGesture, LOCTEXT(HoldGestureName, "Windows Spatial Input Hold Gesture"), FKeyDetails::GamepadKey));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftTapGesture, LOCTEXT(LeftTapGestureName, "Windows Spatial Input Left Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftDoubleTapGesture, LOCTEXT(LeftDoubleTapGestureName, "Windows Spatial Input Left Double Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftHoldGesture, LOCTEXT(LeftHoldGestureName, "Windows Spatial Input Left Hold Gesture"), FKeyDetails::GamepadKey));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightTapGesture, LOCTEXT(RightTapGestureName, "Windows Spatial Input Right Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightDoubleTapGesture, LOCTEXT(RightDoubleTapGestureName, "Windows Spatial Input Right Double Tap Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightHoldGesture, LOCTEXT(RightHoldGestureName, "Windows Spatial Input Right Hold Gesture"), FKeyDetails::GamepadKey));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftManipulationGesture, LOCTEXT(LeftManipulationGestureName, "Windows Spatial Input Left Manipulation Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftManipulationXGesture, LOCTEXT(LeftManipulationXGestureName, "Windows Spatial Input Left Manipulation X Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftManipulationYGesture, LOCTEXT(LeftManipulationYGestureName, "Windows Spatial Input Left Manipulation Y Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftManipulationZGesture, LOCTEXT(LeftManipulationZGestureName, "Windows Spatial Input Left Manipulation Z Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftNavigationGesture, LOCTEXT(LeftNavigationGestureName, "Windows Spatial Input Left Navigation Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftNavigationXGesture, LOCTEXT(LeftNavigationXGestureName, "Windows Spatial Input Left Navigation X Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftNavigationYGesture, LOCTEXT(LeftNavigationYGestureName, "Windows Spatial Input Left Navigation Y Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::LeftNavigationZGesture, LOCTEXT(LeftNavigationZGestureName, "Windows Spatial Input Left Navigation Z Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightManipulationGesture, LOCTEXT(RightManipulationGestureName, "Windows Spatial Input Right Manipulation Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightManipulationXGesture, LOCTEXT(RightManipulationXGestureName, "Windows Spatial Input Right Manipulation X Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightManipulationYGesture, LOCTEXT(RightManipulationYGestureName, "Windows Spatial Input Right Manipulation Y Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightManipulationZGesture, LOCTEXT(RightManipulationZGestureName, "Windows Spatial Input Right Manipulation Z Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));

		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightNavigationGesture, LOCTEXT(RightNavigationGestureName, "Windows Spatial Input Right Navigation Gesture"), FKeyDetails::GamepadKey));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightNavigationXGesture, LOCTEXT(RightNavigationXGestureName, "Windows Spatial Input Right Navigation X Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightNavigationYGesture, LOCTEXT(RightNavigationYGestureName, "Windows Spatial Input Right Navigation Y Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
		EKeys::AddKey(FKeyDetails(FSpatialInputKeys::RightNavigationZGesture, LOCTEXT(RightNavigationZGestureName, "Windows Spatial Input Right Navigation Z Gesture"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D));
	}

	void FWindowsMixedRealitySpatialInput::InitializeSpatialInput() noexcept
	{
		if (IsInitialized ||
			!FWindowsMixedRealityStatics::SupportsSpatialInput())
		{
			return;
		}

		IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);

#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		GestureRecognizer = MakeUnique<GestureRecognizerInterop>();
#endif

		IsInitialized = true;
	}

	void FWindowsMixedRealitySpatialInput::UninitializeSpatialInput() noexcept
	{
		if (!IsInitialized)
		{
			return;
		}

#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		GestureRecognizer = nullptr;
#endif

		IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
	}

	bool FWindowsMixedRealitySpatialInput::GetHandJointPosition(const FName MotionSource, const int32 jointIndex, FVector& OutPosition) const
	{
#if WITH_WINDOWS_MIXED_REALITY
		EControllerHand DeviceHand;
		if (GetHandEnumForSourceName(MotionSource, DeviceHand))
		{
			FRotator outRotator;
			float outRadius;
			return FWindowsMixedRealityStatics::GetHandJointOrientationAndPosition((HMDHand)DeviceHand, (HMDHandJoint)jointIndex, outRotator, OutPosition, outRadius);
		}
#endif
		return false;
	}
}

#undef LOCTEXT_NAMESPACE // "WindowsMixedRealitySpatialInput"
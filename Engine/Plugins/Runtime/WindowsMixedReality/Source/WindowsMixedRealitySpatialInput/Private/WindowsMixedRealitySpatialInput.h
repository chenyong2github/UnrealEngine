// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"

#include "IInputDevice.h"
#include "GenericPlatform/IInputInterface.h"
#include "IMotionController.h"
#include "IHapticDevice.h"
#include "InputCoreTypes.h"
#include "XRMotionControllerBase.h"
#include "XRGestureConfig.h"

#include "Features/IModularFeatures.h"

#include "WindowsSpatialInputDefinitions.h"
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityAvailability.h"

namespace WindowsMixedReality
{
	class FWindowsMixedRealitySpatialInput
		: public IInputDevice
		, public IHapticDevice
		, public FXRMotionControllerBase
	{
	public:
		// WindowsMixedRealitySpatialInput.cpp
		FWindowsMixedRealitySpatialInput(
			const TSharedRef< FGenericApplicationMessageHandler > & InMessageHandler);
		virtual ~FWindowsMixedRealitySpatialInput();

		// Inherited via IInputDevice
		virtual void Tick(float DeltaTime) override;
		virtual void SendControllerEvents() override;
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
		virtual bool Exec(UWorld * InWorld, const TCHAR * Cmd, FOutputDevice & Ar) override;
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues & values) override;

		// Inherited via IHapticDevice
		virtual IHapticDevice* GetHapticDevice() override
		{
			return this;
		}

		virtual void SetHapticFeedbackValues(int32 ControllerId, int32 DeviceHand, const FHapticFeedbackValues & Values) override;
		virtual void GetHapticFrequencyRange(float & MinFrequency, float & MaxFrequency) const override;
		virtual float GetHapticAmplitudeScale() const override;

		// Inherited via FXRMotionControllerBase
		virtual FName GetMotionControllerDeviceTypeName() const override;
		virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator & OutOrientation, FVector & OutPosition, float WorldToMetersScale) const override;
		virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override;
		virtual bool GetHandJointPosition(const FName MotionSource, const int32 jointIndex, FVector& OutPosition) const override;

		bool CaptureGestures(uint32 capturingSet);
		static void RegisterKeys() noexcept;

	private:
		void InitializeSpatialInput() noexcept;
		void UninitializeSpatialInput() noexcept;

#if WITH_WINDOWS_MIXED_REALITY
		void SendButtonEvents(uint32 source);
		void SendAxisEvents(uint32 source);
#endif

#if WITH_WINDOWS_MIXED_REALITY
#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
		TUniquePtr<GestureRecognizerInterop> GestureRecognizer;
		uint32 CapturingSet = 0;
#endif
		bool UpdateGestureCallbacks(FString& errorMsg);
		void TapCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Tap& desc);
		void HoldCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Hold& desc);
		void ManipulationCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Manipulation& desc);
		void NavigationCallback(GestureStage stage, SourceKind kind, const GestureRecognizerInterop::Navigation& desc);
		void EnqueueControllerButtonEvent(uint32 controllerId, FKey button, HMDInputPressState pressState) noexcept;
		void EnqueueControllerAxisEvent(uint32 controllerId, FKey axis, double axisPosition) noexcept;
		void SendQueuedButtonAndAxisEvents();
		struct FEnqueuedControllerEvent
		{
			FEnqueuedControllerEvent(uint32 controllerId, FKey button, HMDInputPressState pressState)
				: bIsAxis(false)
				, ControllerId(controllerId)
				, Key(button)
				, PressState(pressState)
			{
			};
			FEnqueuedControllerEvent(uint32 controllerId, FKey axis, double axisPosition)
				: bIsAxis(true)
				, ControllerId(controllerId)
				, Key(axis)
				, AxisPosition(axisPosition)
			{
			};

			bool bIsAxis;
			uint32 ControllerId;
			FKey Key;
			HMDInputPressState PressState;
			double AxisPosition;
		};
		TArray<FEnqueuedControllerEvent> EnqueuedControllerEventBuffers[2];
		FCriticalSection EnqueuedContollerEventBufferWriteIndexMutex;
		uint32 EnqueuedContollerEventBufferWriteIndex = 0;

#endif

		bool isLeftTouchpadTouched = false;
		bool isRightTouchpadTouched = false;
		FKey ThumbstickDirection[2] = { EKeys::Invalid, EKeys::Invalid };
		FKey TouchpadDirection[2] = { EKeys::Invalid, EKeys::Invalid };

		FThreadSafeBool IsInitialized = false;

		// Unreal message handler.
		TSharedPtr< FGenericApplicationMessageHandler > MessageHandler;
	};
}
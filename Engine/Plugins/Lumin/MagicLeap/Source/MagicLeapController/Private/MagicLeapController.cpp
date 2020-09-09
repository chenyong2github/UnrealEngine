// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapController.h"
#include "IMagicLeapPlugin.h"
#include "MagicLeapMath.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Engine/Engine.h"
#include "MagicLeapControllerKeys.h"
#include "Framework/Application/SlateApplication.h"
#include "Async/Async.h"
#include "TouchpadGesturesComponent.h"
#include "AssetData.h"
#include "MagicLeap/Private/MagicLeapHMD.h"
#if PLATFORM_LUMIN
#include "Lumin/LuminApplication.h"
#endif //PLATFORM_LUMIN

#define LOCTEXT_NAMESPACE "MagicLeapController"

#include "MagicLeapControllerMappings.h"

#if WITH_MLSDK

static_assert(MLInput_MaxControllerTouchpadTouches == FMagicLeapControllerState::kMaxTouches, "Mismatch in max touch constants");
// Controller Mapper
FMagicLeapController::FControllerMapper::FControllerMapper()
{
	// These mappings tell us which entry in the InputControllerState array is providing data for
	// which motion source.
	MotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::Control0, -1);
	MotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::Control1, -1);
	MotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::MobileApp, -1);
	InputControllerIndexToMotionSource[0] = FMagicLeapMotionSourceNames::Unknown;
	InputControllerIndexToMotionSource[1] = FMagicLeapMotionSourceNames::Unknown;

	DefaultInputControllerIndexToHand[0] = EControllerHand::Right;
	DefaultInputControllerIndexToHand[1] = EControllerHand::Left;
}

void FMagicLeapController::FControllerMapper::UpdateMotionSourceInputIndexPairing(const MLInputControllerState ControllerState[MLInput_MaxControllers])
{
	// Determine which entry in the ControllerState array is providing data for which motion source.
	// This is kind of messy, and is the result of an old mandate to allow MLMA to substitute for a control.
	// Once the platform API is adjusted to treat these as separate devices this will be much cleaner.
	TMap<FName, int32> NewMotionSourceToInputControllerIndex;
	FName NewInputControllerIndexToMotionSource[MLInput_MaxControllers];

	NewMotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::Control0, -1);
	NewMotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::Control1, -1);
	NewMotionSourceToInputControllerIndex.Add(FMagicLeapMotionSourceNames::MobileApp, -1);
	NewInputControllerIndexToMotionSource[0] = FMagicLeapMotionSourceNames::Unknown;
	NewInputControllerIndexToMotionSource[1] = FMagicLeapMotionSourceNames::Unknown;

	for (int i = 0; i < MLInput_MaxControllers; ++i)
	{
		switch (ControllerState[i].type)
		{
		case MLInputControllerType_MobileApp:
		{
			NewMotionSourceToInputControllerIndex[FMagicLeapMotionSourceNames::MobileApp] = i;
			NewInputControllerIndexToMotionSource[i] = FMagicLeapMotionSourceNames::MobileApp;
			break;
		}
		case MLInputControllerType_Device:
		{
			if (ControllerState[i].hardware_index == 0)
			{
				NewMotionSourceToInputControllerIndex[FMagicLeapMotionSourceNames::Control0] = i;
				NewInputControllerIndexToMotionSource[i] = FMagicLeapMotionSourceNames::Control0;
			}
			else
			{
				NewMotionSourceToInputControllerIndex[FMagicLeapMotionSourceNames::Control1] = i;
				NewInputControllerIndexToMotionSource[i] = FMagicLeapMotionSourceNames::Control1;
			}
			break;
		}
		default:
			break;
		}
	}

	// Only do the guarded copy if anything changed
	if (FMemory::Memcmp(InputControllerIndexToMotionSource,
		NewInputControllerIndexToMotionSource, sizeof(NewInputControllerIndexToMotionSource)) != 0)
	{
		FScopeLock Lock(&CriticalSection);

		FMemory::Memcpy(InputControllerIndexToMotionSource,
			NewInputControllerIndexToMotionSource, sizeof(NewInputControllerIndexToMotionSource));
		MotionSourceToInputControllerIndex = NewMotionSourceToInputControllerIndex;
	}
}

void FMagicLeapController::FControllerMapper::MapHandToMotionSource(EControllerHand Hand, FName MotionSource)
{
	if (Hand == EControllerHand::Right || Hand == EControllerHand::Left)
	{
		FScopeLock Lock(&CriticalSection);

		if (MotionSource == FMagicLeapMotionSourceNames::Control0 ||
			MotionSource == FMagicLeapMotionSourceNames::Control1 ||
			MotionSource == FMagicLeapMotionSourceNames::MobileApp)
		{
			// Make sure to not allow multiple motion sources to point to the same hand
			auto ExistingMapping = HandToMotionSource.Find(Hand);
			if (ExistingMapping != nullptr)
			{
				*ExistingMapping = MotionSource;
			}
			else
			{
				HandToMotionSource.Add(Hand, MotionSource);
			}
			MotionSourceToHand.FindOrAdd(MotionSource) = Hand;
		}
		else
		{
			// Our module will not map non-ML devices
			auto ExistingMapping = HandToMotionSource.Find(Hand);
			if (ExistingMapping != nullptr)
			{
				MotionSourceToHand.Remove(*ExistingMapping);
				HandToMotionSource.Remove(Hand);
			}
		}
	}
}

FName FMagicLeapController::FControllerMapper::GetMotionSourceForHand(EControllerHand Hand) const
{
	FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));

	if (HandToMotionSource.Num() == 0)
	{
		if (Hand == DefaultInputControllerIndexToHand[0])
			return InputControllerIndexToMotionSource[0];
		return InputControllerIndexToMotionSource[1];
	}
	else
	{
		const auto& MotionSource = HandToMotionSource.Find(Hand);
		if (MotionSource != nullptr)
		{
			return *MotionSource;
		}
	}
	return FMagicLeapMotionSourceNames::Unknown;
}

EControllerHand FMagicLeapController::FControllerMapper::GetHandForMotionSource(FName MotionSource) const
{
	auto ControllerHand = EControllerHand::ControllerHand_Count;

	// Legacy hand motion sources
	if (FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, ControllerHand))
	{
		// Only left and right are allowed
		if (ControllerHand != EControllerHand::Right && ControllerHand != EControllerHand::Left)
		{
			ControllerHand = EControllerHand::ControllerHand_Count;
		}
	}
	else
	{
		FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));

		if (HandToMotionSource.Num() == 0)
		{
			if (InputControllerIndexToMotionSource[0] == MotionSource)
			{
				ControllerHand = EControllerHand::Right;
			}
			else if (InputControllerIndexToMotionSource[1] == MotionSource)
			{
				ControllerHand = EControllerHand::Left;
			}
		}
		else
		{
			const auto& Hand = MotionSourceToHand.Find(MotionSource);
			if (Hand != nullptr)
			{
				ControllerHand = *Hand;
			}
		}
	}
	return ControllerHand;
}

FName FMagicLeapController::FControllerMapper::GetMotionSourceForInputControllerIndex(uint8 controller_id) const
{
	if (controller_id < MLInput_MaxControllers)
	{
		FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));
		return InputControllerIndexToMotionSource[controller_id];
	}
	return FMagicLeapMotionSourceNames::Unknown;
}

uint8 FMagicLeapController::FControllerMapper::GetInputControllerIndexForMotionSource(FName MotionSource) const
{
	auto ControllerHand = EControllerHand::ControllerHand_Count;

	// Legacy hand motion sources
	if (FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, ControllerHand))
	{
		if (ControllerHand == EControllerHand::Right || ControllerHand == EControllerHand::Left)
		{
			return GetInputControllerIndexForHand(ControllerHand);
		}
	}
	else
	{
		FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));
		if (InputControllerIndexToMotionSource[0] == MotionSource)
			return 0;
		if (InputControllerIndexToMotionSource[1] == MotionSource)
			return 1;
	}
	return 0xFF;
}

EControllerHand FMagicLeapController::FControllerMapper::GetHandForInputControllerIndex(uint8 controller_id) const
{
	if (controller_id < MLInput_MaxControllers)
	{
		if (HandToMotionSource.Num() == 0)
		{
			return DefaultInputControllerIndexToHand[controller_id];
		}
		else
		{
			FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));

			const auto& Hand = MotionSourceToHand.Find(InputControllerIndexToMotionSource[controller_id]);
			if (Hand != nullptr)
			{
				return *Hand;
			}

		}
	}
	return EControllerHand::ControllerHand_Count;
}

uint8 FMagicLeapController::FControllerMapper::GetInputControllerIndexForHand(EControllerHand Hand) const
{
	if (HandToMotionSource.Num() == 0)
	{
		if (Hand == DefaultInputControllerIndexToHand[0])
		{
			return 0;
		}
		return 1;
	}
	else
	{
		FScopeLock Lock(const_cast<FCriticalSection*>(&CriticalSection));

		const auto& MotionSource = HandToMotionSource.Find(Hand);
		if (MotionSource != nullptr)
		{
			return MotionSourceToInputControllerIndex[*MotionSource];
		}
	}
	return 0xFF;
}

EMagicLeapControllerType FMagicLeapController::FControllerMapper::MotionSourceToControllerType(FName MotionSource)
{
	auto MLSourceToControllerType = [](FName InMotionSource)
	{
		if (InMotionSource == FMagicLeapMotionSourceNames::Control0 ||
			InMotionSource == FMagicLeapMotionSourceNames::Control1)
		{
			return EMagicLeapControllerType::Device;
		}
		if (InMotionSource == FMagicLeapMotionSourceNames::MobileApp)
		{
			return EMagicLeapControllerType::MobileApp;
		}
		return EMagicLeapControllerType::None;
	};

	// First just see if it's one of ours and can be easily mapped
	auto ControllerType = MLSourceToControllerType(MotionSource);
	if (ControllerType == EMagicLeapControllerType::None)
	{
		// If not, see if it's a hand mapping
		auto ControllerHand = EControllerHand::ControllerHand_Count;
		if (FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, ControllerHand) &&
			(ControllerHand == EControllerHand::Right || ControllerHand == EControllerHand::Left))
		{
			TSharedPtr<FMagicLeapController> controller =
				StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
			if (controller.IsValid())
			{
				auto TheMotionSource = controller->
					ControllerMapper.GetMotionSourceForHand(ControllerHand);
				ControllerType = MotionSourceToControllerType(TheMotionSource);
			}
		}
	}
	return ControllerType;
}

void FMagicLeapController::FControllerMapper::SwapHands()
{
	Swap(DefaultInputControllerIndexToHand[0], DefaultInputControllerIndexToHand[1]);
}
#endif //WITH_MLSDK

FMagicLeapController::FMagicLeapController(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
	, DeviceIndex(0)  // Input Controller Index for Unreal is hardcoded to 0. Ideally it should be incremented for each registered InputDevice.
#if WITH_MLSDK
	, InputTracker(ML_INVALID_HANDLE)
	, ControllerTracker(ML_INVALID_HANDLE)
	, ControllerDof(MLInputControllerDof_6)
	, TrackingMode(EMagicLeapControllerTrackingMode::CoordinateFrameUID)
#endif //WITH_MLSDK
	, bIsInputStateValid(false)
	, TriggerKeyIsConsideredPressed(80.0f)
	, TriggerKeyIsConsideredReleased(20.0f)
{
	// Hack call to empty function to force the automation tests to compile in
	MagicLeapTestReferenceFunction();

#if WITH_MLSDK
	InitializeInputCallbacks();

	// Current and previous frame of engine-mapped controller data
	CurrMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::Control0);
	CurrMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::Control1);
	CurrMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::MobileApp);
	PrevMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::Control0);
	PrevMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::Control1);
	PrevMotionSourceControllerState.Add(FMagicLeapMotionSourceNames::MobileApp);
#endif //WITH_MLSDK

	// Register "MotionController" modular feature manually
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	AddKeys();

	// We're implicitly requiring that the MagicLeapPlugin has been loaded and
	// initialized at this point.
	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
}

FMagicLeapController::~FMagicLeapController()
{
	// Normally, the MagicLeapPlugin will be around during unload,
	// but it isn't an assumption that we should make.
	if (IMagicLeapPlugin::IsAvailable())
	{
		IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);
	}

	DestroyEntityTracker();

	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

#if WITH_MLSDK
FName GetKeyNameFromGesture(const MLInputControllerTouchpadGesture* Gesture)
{
	FName KeyName = NAME_None;

	switch(Gesture->type)
	{
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_Tap:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Tap_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_ForceTapDown:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_ForceTapDown_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_ForceTapUp:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_ForceTapUp_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_ForceDwell:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_ForceDwell_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_SecondForceDown:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_SecondForceDown_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_LongHold:
			KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_LongHold_Name;
			break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_RadialScroll:
		{
			switch(Gesture->direction)
			{
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Clockwise:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_RadialScroll_Clockwise_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_CounterClockwise:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_RadialScroll_CounterClockwise_Name;
					break;
			}
		}
		break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_Swipe:
		{
			switch(Gesture->direction)
			{
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Up:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Swipe_Up_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Down:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Swipe_Down_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Right:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Swipe_Right_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Left:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Swipe_Left_Name;
					break;
			}
		}
		break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_Scroll:
		{
			switch(Gesture->direction)
			{
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Up:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Scroll_Up_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Down:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Scroll_Down_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Right:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Scroll_Right_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Left:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Scroll_Left_Name;
					break;
			}
		}
		break;
		case MLInputControllerTouchpadGestureType::MLInputControllerTouchpadGestureType_Pinch:
		{
			switch(Gesture->direction)
			{
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_In:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Pinch_In_Name;
					break;
				case MLInputControllerTouchpadGestureDirection::MLInputControllerTouchpadGestureDirection_Out:
					KeyName = FMagicLeapControllerKeyNames::TouchpadGesture_Pinch_Out_Name;
					break;
			}
		}
		break;
	}

	return KeyName;
}
#endif // WITH_MLSDK

void FMagicLeapController::InitializeInputCallbacks()
{
#if WITH_MLSDK
	FMemory::Memset(&InputControllerCallbacks, 0, sizeof(InputControllerCallbacks));

	InputControllerCallbacks.on_touchpad_gesture_start = [](uint8 controller_id, const MLInputControllerTouchpadGesture *touchpad_gesture, void *data)
	{
		auto Controller = reinterpret_cast<FMagicLeapController*>(data);
		if (Controller && controller_id < MLInput_MaxControllers)
		{
			const auto& Hand = Controller->ControllerMapper.GetHandForInputControllerIndex(controller_id);
			if (Hand != EControllerHand::ControllerHand_Count)
			{
				FMagicLeapTouchpadGesture Gesture_DEPRECATED = MLToUnrealTouchpadGesture_DEPRECATED(Hand, FMagicLeapMotionSourceNames::Unknown, *touchpad_gesture);
				for (IMagicLeapTouchpadGestures* Receiver : Controller->TouchpadGestureReceivers)
				{
					Receiver->OnTouchpadGestureStartCallback(Gesture_DEPRECATED);
				}
			}
		}
	};

	InputControllerCallbacks.on_touchpad_gesture_continue = [](uint8 controller_id, const MLInputControllerTouchpadGesture *touchpad_gesture, void *data)
	{
		auto Controller = reinterpret_cast<FMagicLeapController*>(data);
		if (Controller && controller_id < MLInput_MaxControllers)
		{
			const auto& Hand = Controller->ControllerMapper.GetHandForInputControllerIndex(controller_id);
			if (Hand != EControllerHand::ControllerHand_Count)
			{
				FMagicLeapTouchpadGesture Gesture_DEPRECATED = MLToUnrealTouchpadGesture_DEPRECATED(Hand, FMagicLeapMotionSourceNames::Unknown, *touchpad_gesture);
				for (IMagicLeapTouchpadGestures* Receiver : Controller->TouchpadGestureReceivers)
				{
					Receiver->OnTouchpadGestureContinueCallback(Gesture_DEPRECATED);
				}
			}
		}
	};

	InputControllerCallbacks.on_touchpad_gesture_end = [](uint8 controller_id, const MLInputControllerTouchpadGesture *touchpad_gesture, void *data)
	{
		auto Controller = reinterpret_cast<FMagicLeapController*>(data);
		if (Controller && controller_id < MLInput_MaxControllers)
		{
			const auto& Hand = Controller->ControllerMapper.GetHandForInputControllerIndex(controller_id);
			if (Hand != EControllerHand::ControllerHand_Count)
			{
				FMagicLeapTouchpadGesture Gesture_DEPRECATED = MLToUnrealTouchpadGesture_DEPRECATED(Hand, FMagicLeapMotionSourceNames::Unknown, *touchpad_gesture);
				for (IMagicLeapTouchpadGestures* Receiver : Controller->TouchpadGestureReceivers)
				{
					Receiver->OnTouchpadGestureEndCallback(Gesture_DEPRECATED);
				}
			}
		}
	};

	InputControllerCallbacks.on_button_down = [](uint8_t controller_id, MLInputControllerButton button, void *data)
	{
		auto Controller = reinterpret_cast<FMagicLeapController*>(data);
		if (Controller && controller_id < MLInput_MaxControllers)
		{
			const auto ControllerHand = Controller->ControllerMapper.GetHandForInputControllerIndex(controller_id);
			if (ControllerHand != EControllerHand::ControllerHand_Count)
			{
				Controller->EnqueueButton(ControllerHand, button, true);
			}
		}
	};

	InputControllerCallbacks.on_button_up = [](uint8_t controller_id, MLInputControllerButton button, void *data)
	{
		auto Controller = reinterpret_cast<FMagicLeapController*>(data);
		if (Controller && controller_id < MLInput_MaxControllers)
		{
			const auto ControllerHand = Controller->ControllerMapper.GetHandForInputControllerIndex(controller_id);
			if (ControllerHand != EControllerHand::ControllerHand_Count)
			{
				Controller->EnqueueButton(ControllerHand, button, false);
			}
		}
	};
#endif //WITH_MLSDK
}

#if WITH_MLSDK
void FMagicLeapController::EnqueueButton(EControllerHand ControllerHand, MLInputControllerButton Button, bool bIsPressed)
{
	auto CurrentButton = MLToUnrealButton(ControllerHand, Button);
	if (!CurrentButton.IsNone())
	{
		PendingButtonEvents.Enqueue(MakeTuple(CurrentButton, bIsPressed));
	}
}
#endif //WITH_MLSDK

void FMagicLeapController::Tick(float DeltaTime)
{
	UpdateTrackerData();
}

void FMagicLeapController::SendControllerEvents()
{
#if WITH_MLSDK
	if (bIsInputStateValid && MessageHandler.IsValid())
	{
		MagicLeap::EnableInput EnableInputFromHMD;
		// fixes unreferenced parameter error for Windows package builds.
		(void)EnableInputFromHMD;

		SendControllerEventsForHand(EControllerHand::Right);
		SendControllerEventsForHand(EControllerHand::Left);

		while (!PendingButtonEvents.IsEmpty())
		{
			TPair<FName, bool> ButtonEvent;

			PendingButtonEvents.Dequeue(ButtonEvent);

			if (ButtonEvent.Value)
			{
				MessageHandler->OnControllerButtonPressed(ButtonEvent.Key, DeviceIndex, false);
			}
			else
			{
				MessageHandler->OnControllerButtonReleased(ButtonEvent.Key, DeviceIndex, false);
			}
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapController::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

bool FMagicLeapController::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

bool FMagicLeapController::IsGamepadAttached() const
{
#if WITH_MLSDK
	return CurrMotionSourceControllerState[FMagicLeapMotionSourceNames::Control0].bIsConnected ||
		CurrMotionSourceControllerState[FMagicLeapMotionSourceNames::Control1].bIsConnected ||
		CurrMotionSourceControllerState[FMagicLeapMotionSourceNames::MobileApp].bIsConnected;
#endif //WITH_MLSDK
	return false;
}

void FMagicLeapController::ReadConfigParams()
{
#if WITH_MLSDK
	// Pull hand-mapping preferences from config file. If there are none, the default (legacy)
	// mapping of device 0 to right and device 1 to left will persist.
	const UEnum* ControllerHandEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EControllerHand"));
	TArray<FString> ControllerHands;
	GConfig->GetArray(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"),
		TEXT("ControllerHands"), ControllerHands, GEngineIni);
	for (auto ControllerHand : ControllerHands)
	{
		// Remove the parentheses
		ControllerHand.RemoveAt(0);
		ControllerHand.RemoveAt(ControllerHand.Len() - 1);

		// Get the mapping
		TArray<FString> SingleMapping;
		ControllerHand.ParseIntoArray(SingleMapping, TEXT("="), true);
		if (SingleMapping.Num() == 2)
		{
			auto HandIndex = ControllerHandEnum->GetValueByNameString(SingleMapping[0]);
			if (HandIndex != INDEX_NONE)
			{
				ControllerMapper.MapHandToMotionSource(static_cast<EControllerHand>(HandIndex), FName(*SingleMapping[1]));
			}
			else
			{
				UE_LOG(LogMagicLeapController, Error,
					TEXT("Invalid hand enum %s specified in ControllerHands array."),
					*SingleMapping[0]);
			}
		}
	}

	// Pull trigger thresholds from config file
	float FloatValueReceived = 0.0f;
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"),
		TEXT("TriggerKeyIsConsideredPressed"), FloatValueReceived, GInputIni);
	TriggerKeyIsConsideredPressed = FloatValueReceived;

	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"),
		TEXT("TriggerKeyIsConsideredReleased"), FloatValueReceived, GInputIni);
	TriggerKeyIsConsideredReleased = FloatValueReceived;

	// Pull tracking preferences from config file
	FString ConfigString;
	GConfig->GetString(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"),
		TEXT("ControllerTrackingType"), ConfigString, GEngineIni);
	if (ConfigString.Len() > 0)
	{
		const UEnum* TrackingTypeEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ETrackingStatus"));

		auto TrackingTypeIndex = TrackingTypeEnum->GetValueByNameString(ConfigString);
		if (TrackingTypeIndex != INDEX_NONE)
		{
			switch (static_cast<ETrackingStatus>(TrackingTypeIndex))
			{
			case ETrackingStatus::NotTracked:
				ControllerDof = MLInputControllerDof_None;
				break;
			case ETrackingStatus::InertialOnly:
				ControllerDof = MLInputControllerDof_3;
				break;
			case ETrackingStatus::Tracked:
			default:
				ControllerDof = MLInputControllerDof_6;
				break;
			}
		}
		else
		{
			UE_LOG(LogMagicLeapController, Error,
				TEXT("Invalid ControllerTrackingType %s specified."),
				*ConfigString);
		}
	}

	const static UEnum* TrackingModeEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerTrackingMode"));
	GConfig->GetString(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"),
		TEXT("ControllerTrackingMode"), ConfigString, GEngineIni);
	if (ConfigString.Len() > 0)
	{
		TrackingMode = static_cast<EMagicLeapControllerTrackingMode>(TrackingModeEnum->GetValueByNameString(ConfigString));
	}
#endif //WITH_MLSDK
}

void FMagicLeapController::CreateEntityTracker()
{
#if WITH_MLSDK
	MLResult Result = MLResult_Ok;

	// Must be done at Enable b/c we need packages to load to read enums
	ReadConfigParams();

	FMemory::Memset(&InputControllerState, 0, sizeof(InputControllerState));

	// Attempt to create the Controller Tracker, regardless of chosen mode,
	// so we can switch on the fly
	MLControllerConfiguration ControllerConfig = { false };

	switch (ControllerDof)
	{
	case MLInputControllerDof_3:
		ControllerConfig.enable_imu3dof = true;
		break;
	case MLInputControllerDof_6:
		ControllerConfig.enable_fused6dof = true;
		break;
	case MLInputControllerDof_None:
		break;
	}

	Result = MLControllerCreateEx(&ControllerConfig, &ControllerTracker);

	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapController, Error,
			TEXT("MLControllerCreateEx failed with error %s."),
			UTF8_TO_TCHAR(MLGetResultString(Result)));
		ControllerTracker = ML_INVALID_HANDLE;

		// Revert to input
		TrackingMode = EMagicLeapControllerTrackingMode::InputService;
	}

	InputTracker = static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice())->InputTracker;

	// Set controller button/touchpad callbacks on valid input tracker
	if (MLHandleIsValid(InputTracker))
	{
		Result = MLInputSetControllerCallbacks(InputTracker, &InputControllerCallbacks, this);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLInputSetControllerCallbacks failed with error %s."),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}

	// Poll to get startup status
	UpdateTrackerData();

#endif //WITH_MLSDK
}

void FMagicLeapController::DestroyEntityTracker()
{
#if WITH_MLSDK
	InputTracker = ML_INVALID_HANDLE;

	if (MLHandleIsValid(ControllerTracker))
	{
		MLResult Result = MLControllerDestroy(ControllerTracker);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLControllerDestroy failed with error %s!"),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}
	ControllerTracker = ML_INVALID_HANDLE;

	bIsInputStateValid = false;
#endif //WITH_MLSDK
}

bool FMagicLeapController::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
#if WITH_MLSDK
	if (ControllerIndex == DeviceIndex)
	{
		const auto& ControllerState = CurrMotionSourceControllerState.Find(MotionSource);
		if (ControllerState != nullptr)
		{
			OutPosition = ControllerState->Transform.GetLocation();
			OutOrientation = ControllerState->Transform.GetRotation().Rotator();
			return true;
		}
	}
#endif //WITH_MLSDK
	return FXRMotionControllerBase::GetControllerOrientationAndPosition(ControllerIndex, MotionSource, OutOrientation, OutPosition, WorldToMetersScale);
}

bool FMagicLeapController::GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
#if WITH_MLSDK
	// Do not call our function with a remap as that can enter an infinite loop due to remapping in FXRMotionControlerBase
	if (ControllerIndex == DeviceIndex)
	{
		auto MotionSource = ControllerMapper.GetMotionSourceForHand(DeviceHand);
		if (MotionSource != FMagicLeapMotionSourceNames::Unknown)
		{
			return GetControllerOrientationAndPosition(ControllerIndex,
				MotionSource, OutOrientation, OutPosition, WorldToMetersScale);
		}
	}
#endif //WITH_MLSDK
	return false;
}

ETrackingStatus FMagicLeapController::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
{
#if WITH_MLSDK
	if (ControllerIndex == DeviceIndex)
	{
		const auto& ControllerState = CurrMotionSourceControllerState.Find(MotionSource);
		if (ControllerState != nullptr)
		{
			return ControllerState->TrackingStatus;
		}
	}
#endif //WITH_MLSDK
	return FXRMotionControllerBase::GetControllerTrackingStatus(ControllerIndex, MotionSource);
}

ETrackingStatus FMagicLeapController::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
{
#if WITH_MLSDK
	return GetControllerTrackingStatus(ControllerIndex, ControllerMapper.GetMotionSourceForHand(DeviceHand));
#endif //WITH_MLSDK
	return ETrackingStatus::NotTracked;
}

FName FMagicLeapController::GetMotionControllerDeviceTypeName() const
{
	const static FName DefaultName(TEXT("MagicLeapController"));
	return DefaultName;
}

void FMagicLeapController::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
	SourcesOut.Add(FMagicLeapMotionSourceNames::Control0);
	SourcesOut.Add(FMagicLeapMotionSourceNames::Control1);
	SourcesOut.Add(FMagicLeapMotionSourceNames::MobileApp);
}

void FMagicLeapController::UpdateControllerStateFromInputTracker(const IMagicLeapPlugin& MLPlugin, FName MotionSource)
{
#if WITH_MLSDK
	checkf(CurrMotionSourceControllerState.Contains(MotionSource),
		TEXT("UpdateControllerStateFromInputTracker was asked for non-ML motion source!"));
	checkf(PrevMotionSourceControllerState.Contains(MotionSource), TEXT("Unpossible"));

	auto& CurrControllerState = CurrMotionSourceControllerState[MotionSource];
	auto& PrevControllerState = PrevMotionSourceControllerState[MotionSource];

	// Advance frame
	PrevControllerState = CurrControllerState;

	// Get platform input state for this motion source.
	int InputStateIndex = ControllerMapper.GetInputControllerIndexForMotionSource(MotionSource);
	if (InputStateIndex >= 0 && InputStateIndex < MLInput_MaxControllers)
	{
		const auto& InputState = InputControllerState[InputStateIndex];

		// TODO: connect/disconnect events?
		CurrControllerState.bIsConnected = InputState.is_connected;

		// Touch activity, coordinates, and force
		for (auto TouchIndex = 0; TouchIndex < MLInput_MaxControllerTouchpadTouches; ++ TouchIndex)
		{
			CurrControllerState.bTouchActive[TouchIndex] = InputState.is_touch_active[TouchIndex];

			CurrControllerState.TouchPosAndForce[TouchIndex].
				Set(InputState.touch_pos_and_force[TouchIndex].x,
					InputState.touch_pos_and_force[TouchIndex].y,
					InputState.touch_pos_and_force[TouchIndex].z);
		}

		// Analog trigger
		CurrControllerState.TriggerAnalog = InputState.trigger_normalized;

		// Dof
		switch (InputState.dof)
		{
			case MLInputControllerDof_3:
			{
				CurrControllerState.TrackingStatus = ETrackingStatus::InertialOnly;
				CurrControllerState.Transform.SetLocation(FVector::ZeroVector);
				CurrControllerState.Transform.SetRotation(MagicLeap::ToFQuat(InputState.orientation));
				break;
			}
			case MLInputControllerDof_6:
			{
				CurrControllerState.TrackingStatus = ETrackingStatus::Tracked;
				CurrControllerState.Transform.SetLocation(MagicLeap::ToFVector(InputState.position,
					MLPlugin.GetWorldToMetersScale()));
				CurrControllerState.Transform.SetRotation(MagicLeap::ToFQuat(InputState.orientation));
				break;
			}
			default:
			{
				CurrControllerState.TrackingStatus = ETrackingStatus::NotTracked;
				CurrControllerState.Transform.SetIdentity();
				break;
			}
		}

		// Fixup transform
		if (CurrControllerState.Transform.ContainsNaN())
		{
			UE_LOG(LogMagicLeapController, Error, TEXT("Transform for input state index %d has NaNs."), InputStateIndex);
			CurrControllerState.TrackingStatus = ETrackingStatus::NotTracked;
			CurrControllerState.Transform.SetIdentity();
		}
		else if (!CurrControllerState.Transform.GetRotation().IsNormalized())
		{
			FQuat rotation = CurrControllerState.Transform.GetRotation();
			rotation.Normalize();
			CurrControllerState.Transform.SetRotation(rotation);
		}

		// Generated button events
		auto Hand = ControllerMapper.GetHandForMotionSource(MotionSource);
		if (Hand != EControllerHand::ControllerHand_Count)
		{
			// Touch0 activate/deactivate
			if (CurrControllerState.bTouchActive[0] && !PrevControllerState.bTouchActive[0])
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTouchToUnrealTrackpadButton(Hand), true));
			}
			else if (PrevControllerState.bTouchActive[0] && !CurrControllerState.bTouchActive[0])
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTouchToUnrealTrackpadButton(Hand), false));
			}

			// Touch1 activate/deactivate
			if (CurrControllerState.bTouchActive[1] && !PrevControllerState.bTouchActive[1])
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTouchToUnrealTouch1Button(Hand), true));
			}
			else if (PrevControllerState.bTouchActive[1] && !CurrControllerState.bTouchActive[1])
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTouchToUnrealTouch1Button(Hand), false));
			}

			// Convert trigger value to trigger press/release events
			const bool IsTriggerKeyPressing = (CurrControllerState.TriggerAnalog >
				TriggerKeyIsConsideredPressed) && !PrevControllerState.bTriggerKeyPressing;
			const bool IsTriggerKeyReleasing = (CurrControllerState.TriggerAnalog <
				TriggerKeyIsConsideredReleased) && PrevControllerState.bTriggerKeyPressing;

			if (IsTriggerKeyPressing)
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTriggerToUnrealTriggerKey(Hand), true));
				CurrControllerState.bTriggerKeyPressing = true;
			}
			else if (IsTriggerKeyReleasing)
			{
				PendingButtonEvents.Enqueue(MakeTuple(MLTriggerToUnrealTriggerKey(Hand), false));
				CurrControllerState.bTriggerKeyPressing = false;
			}
		}

	}
	else
	{
		CurrControllerState = { };
	}
#endif //WITH_MLSDK
}

void FMagicLeapController::UpdateControllerStateFromControllerTracker(const IMagicLeapPlugin& MLPlugin, FName MotionSource)
{
#if WITH_MLSDK
	// Index of the stream we're reading
	int32 StreamIndex = -1;

	switch (ControllerDof)
	{
	case MLInputControllerDof_3:
		StreamIndex = MLControllerMode_Imu3Dof;
		break;
	case MLInputControllerDof_6:
		StreamIndex = MLControllerMode_Fused6Dof;
		break;
	default:
		break;
	}

	if (StreamIndex != -1)
	{
		checkf(MotionSource == FMagicLeapMotionSourceNames::Control0 || MotionSource == FMagicLeapMotionSourceNames::Control1,
			TEXT("UpdateControllerStateFromControllerTracker was asked for non-control motion source!"));
		auto& ControllerState = CurrMotionSourceControllerState[MotionSource];

		// Hardware index of control
		int32 ControlIndex = 0;
		if (MotionSource == FMagicLeapMotionSourceNames::Control1)
		{
			ControlIndex = 1;
		}

		const auto& ControllerStream = ControllerSystemState.controller_state[ControlIndex].stream[StreamIndex];

		if (ControllerStream.is_active)
		{
			if (StreamIndex == MLControllerMode_Imu3Dof)
			{
				ControllerState.TrackingStatus = ETrackingStatus::InertialOnly;
			}
			else
			{
				ControllerState.TrackingStatus = ETrackingStatus::Tracked;
			}

			EMagicLeapTransformFailReason FailReason = EMagicLeapTransformFailReason::None;
			if (!MLPlugin.GetTransform(ControllerStream.
				coord_frame_controller, ControllerState.Transform, FailReason))
			{
				UE_LOG(LogMagicLeapController, Error,
					TEXT("UpdateControllerStateFromControllerTracker: MLPlugin."
						"GetTransform returned false, fail reason = %d."),
					static_cast<uint32>(FailReason));
			}
		}
		else
		{
			ControllerState.TrackingStatus = ETrackingStatus::NotTracked;
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapController::UpdateTrackerData()
{
#if WITH_MLSDK
	if (!IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
	{
		return;
	}

	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();

	// First pull data from input tracker. Note that this is not conditional based on the tracking
	// type because we also need to get buttons, touchpad, etc.
	if (MLHandleIsValid(InputTracker))
	{
		MLResult Result = MLInputGetControllerState(InputTracker, InputControllerState);

		if (MLResult_Ok == Result)
		{
			bIsInputStateValid = true;

			ControllerMapper.UpdateMotionSourceInputIndexPairing(InputControllerState);
			UpdateControllerStateFromInputTracker(MLPlugin, FMagicLeapMotionSourceNames::Control0);
			UpdateControllerStateFromInputTracker(MLPlugin, FMagicLeapMotionSourceNames::Control1);
			UpdateControllerStateFromInputTracker(MLPlugin, FMagicLeapMotionSourceNames::MobileApp);
		}
		else
		{
			bIsInputStateValid = false;

			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLInputGetControllerState failed with error %s."),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}

	// If mode is set to CFUID tracking overwrite the input system Dof data
	if (TrackingMode == EMagicLeapControllerTrackingMode::CoordinateFrameUID)
	{
		// We need to have valid input state in order to do this, because we need to be sure
		// what we are polling is a physical control
		if (bIsInputStateValid && MLHandleIsValid(ControllerTracker))
		{
			MLResult Result = MLControllerGetState(ControllerTracker, &ControllerSystemState);

			if (MLResult_Ok == Result)
			{
				UpdateControllerStateFromControllerTracker(MLPlugin, FMagicLeapMotionSourceNames::Control0);
				UpdateControllerStateFromControllerTracker(MLPlugin, FMagicLeapMotionSourceNames::Control1);
			}
			else
			{
				UE_LOG(LogMagicLeapController, Error,
					TEXT("MLControllerGetState failed with error %s."),
					UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		}
	}
#endif //WITH_MLSDK
}

bool FMagicLeapController::SetControllerTrackingMode(EMagicLeapControllerTrackingMode InTrackingMode)
{
#if WITH_MLSDK
	if (IsGamepadAttached())
	{
		TrackingMode = InTrackingMode;
	}
	else
	{
		UE_LOG(LogMagicLeapController, Error, TEXT("Haptic controller not attached"));
	}
#endif //WITH_MLSDK

	return false;
}

EMagicLeapControllerTrackingMode FMagicLeapController::GetControllerTrackingMode()
{
#if WITH_MLSDK
	if (IsGamepadAttached())
	{
		return TrackingMode;
	}
#endif //WITH_MLSDK

	return EMagicLeapControllerTrackingMode::InputService;
}

void FMagicLeapController::RegisterTouchpadGestureReceiver(IMagicLeapTouchpadGestures* Receiver)
{
	if (Receiver != nullptr)
	{
		TouchpadGestureReceivers.Add(Receiver);
	}
}

void FMagicLeapController::UnregisterTouchpadGestureReceiver(IMagicLeapTouchpadGestures* Receiver)
{
	TouchpadGestureReceivers.Remove(Receiver);
}

void FMagicLeapController::AddKeys()
{
	EKeys::AddMenuCategoryDisplayInfo("MagicLeap", LOCTEXT("MagicLeapSubCategory", "Magic Leap"), TEXT("GraphEditor.PadEvent_16x"));

	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::MotionController_Left_Thumbstick_Z, LOCTEXT("MotionController_Left_Thumbstick_Z", "MotionController (L) Thumbstick Z"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::Deprecated));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_HomeButton, LOCTEXT("MagicLeap_Left_HomeButton", "ML (L) Home Button"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Bumper, LOCTEXT("MagicLeap_Left_Bumper", "ML (L) Bumper"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trigger, LOCTEXT("MagicLeap_Left_Trigger", "ML (L) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trigger_Axis, LOCTEXT("MagicLeap_Left_Trigger_Axis", "ML (L) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trackpad_X, LOCTEXT("MagicLeap_Left_Trackpad_X", "ML (L) Trackpad X"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trackpad_Y, LOCTEXT("MagicLeap_Left_Trackpad_Y", "ML (L) Trackpad Y"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trackpad_Force, LOCTEXT("MagicLeap_Left_Trackpad_Force", "ML (L) Trackpad Force"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Trackpad_Touch, LOCTEXT("MagicLeap_Left_Trackpad_Touch", "ML (L) Trackpad Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Touch1_X, LOCTEXT("MagicLeap_Left_Touch1_X", "ML (L) Touch1 X"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Touch1_Y, LOCTEXT("MagicLeap_Left_Touch1_Y", "ML (L) Touch1 Y"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Touch1_Force, LOCTEXT("MagicLeap_Left_Touch1_Force", "ML (L) Touch1 Force"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Left_Touch1_Touch, LOCTEXT("MagicLeap_Left_Touch1_Touch", "ML (L) Touch1 Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));

	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::MotionController_Right_Thumbstick_Z, LOCTEXT("MotionController_Right_Thumbstick_Z", "MotionController (R) Thumbstick Z"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::Deprecated));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_HomeButton, LOCTEXT("MagicLeap_Right_HomeButton", "ML (R) Home Button"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Bumper, LOCTEXT("MagicLeap_Right_Bumper", "ML (R) Bumper"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trigger, LOCTEXT("MagicLeap_Right_Trigger", "ML (R) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trigger_Axis, LOCTEXT("MagicLeap_Right_Trigger_Axis", "ML (R) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trackpad_X, LOCTEXT("MagicLeap_Right_Trackpad_X", "ML (R) Trackpad X"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trackpad_Y, LOCTEXT("MagicLeap_Right_Trackpad_Y", "ML (R) Trackpad Y"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trackpad_Force, LOCTEXT("MagicLeap_Right_Trackpad_Force", "ML (R) Trackpad Force"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Trackpad_Touch, LOCTEXT("MagicLeap_Right_Trackpad_Touch", "ML (R) Trackpad Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Touch1_X, LOCTEXT("MagicLeap_Right_Touch1_X", "ML (R) Touch1 X"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Touch1_Y, LOCTEXT("MagicLeap_Right_Touch1_Y", "ML (R) Touch1 Y"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Touch1_Force, LOCTEXT("MagicLeap_Right_Touch1_Force", "ML (R) Touch1 Force"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
	EKeys::AddKey(FKeyDetails(FMagicLeapKeys::Right_Touch1_Touch, LOCTEXT("MagicLeap_Right_Touch1_Touch", "ML (R) Touch1 Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "MagicLeap"));
}

bool FMagicLeapController::PlayLEDPattern(FName MotionSource, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
#if WITH_MLSDK
	if (IsGamepadAttached())
	{
		auto InputControllerIndex = ControllerMapper.GetInputControllerIndexForMotionSource(MotionSource);
		if (InputControllerIndex != 0xFF)
		{
			MLResult Result = MLInputStartControllerFeedbackPatternLED(InputTracker,
				InputControllerIndex,
				UnrealToMLPatternLED(LEDPattern),
				UnrealToMLColorLED(LEDColor),
				static_cast<uint32>(DurationInSec * 1000));

			if (MLResult_Ok == Result)
			{
				return true;
			}

			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLInputStartControllerFeedbackPatternLED failed with error %s"),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
		else
		{
			UE_LOG(LogMagicLeapController, Error, TEXT("PlayLEDPattern requested on non-ML controller"));
		}
	}
	else
	{
		UE_LOG(LogMagicLeapController, Error, TEXT("PlayLEDPattern: controller not attached"));
	}
#endif //WITH_MLSDK

	return false;
}

bool FMagicLeapController::PlayLEDEffect(FName MotionSource, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
#if WITH_MLSDK
	if (IsGamepadAttached())
	{
		auto InputControllerIndex = ControllerMapper.GetInputControllerIndexForMotionSource(MotionSource);
		if (InputControllerIndex != 0xFF)
		{
			MLResult Result = MLInputStartControllerFeedbackPatternEffectLED(InputTracker,
				InputControllerIndex,
				UnrealToMLEffectLED(LEDEffect),
				UnrealToMLSpeedLED(LEDSpeed),
				UnrealToMLPatternLED(LEDPattern),
				UnrealToMLColorLED(LEDColor),
				static_cast<uint32>(DurationInSec * 1000));

			if (MLResult_Ok == Result)
			{
				return true;
			}

			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLInputStartControllerFeedbackPatternEffectLED failed with error %s"),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
		else
		{
			UE_LOG(LogMagicLeapController, Error, TEXT("PlayLEDEffect requested on non-ML controller"));
		}
	}
	else
	{
		UE_LOG(LogMagicLeapController, Error, TEXT("PlayLEDEffect: controller not attached"));
	}
#endif //WITH_MLSDK

	return false;
}

bool FMagicLeapController::PlayHapticPattern(FName MotionSource, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity)
{
#if WITH_MLSDK
	if (IsGamepadAttached())
	{
		auto InputControllerIndex = ControllerMapper.GetInputControllerIndexForMotionSource(MotionSource);
		if (InputControllerIndex != 0xFF)
		{
			MLResult Result = MLInputStartControllerFeedbackPatternVibe(InputTracker,
				InputControllerIndex,
				UnrealToMLPatternVibe(HapticPattern),
				UnrealToMLHapticIntensity(Intensity));

			if (MLResult_Ok == Result)
			{
				return true;
			}

			UE_LOG(LogMagicLeapController, Error,
				TEXT("MLInputStartControllerFeedbackPatternVibe failed with error %s"),
				UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
		else
		{
			UE_LOG(LogMagicLeapController, Error, TEXT("PlayHapticPattern requested on non-ML controller"));
		}
	}
	else
	{
		UE_LOG(LogMagicLeapController, Error, TEXT("PlayHapticPattern: controller not attached"));
	}
#endif //WITH_MLSDK

	return false;
}

bool FMagicLeapController::IsMLControllerConnected(FName MotionSource) const
{
#if WITH_MLSDK
	const FMagicLeapControllerState* ControllerState = CurrMotionSourceControllerState.Find(MotionSource);
	return (ControllerState != nullptr) ? ControllerState->bIsConnected : false;
#else
	return false;
#endif //WITH_MLSDK
}

void FMagicLeapController::SendControllerEventsForHand(EControllerHand Hand)
{
#if WITH_MLSDK
	const auto& MotionSource = ControllerMapper.GetMotionSourceForHand(Hand);

	const auto& CurrControllerState = CurrMotionSourceControllerState.Find(MotionSource);
	const auto& PrevControllerState = PrevMotionSourceControllerState.Find(MotionSource);

	if (CurrControllerState != nullptr && CurrControllerState->bIsConnected)
	{
		checkf(PrevControllerState != nullptr, TEXT("Unpossible"));

		MagicLeap::EnableInput EnableInputFromHMD;
		// fixes unreferenced parameter error for Windows package builds.
		(void)EnableInputFromHMD;

		// Analog touch coords
		// Touch 0 maps to ML Trackpad
		// Touch 1 maps to ML Touch1
		if (CurrControllerState->bTouchActive[0])
		{
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 0),
				DeviceIndex, CurrControllerState->TouchPosAndForce[0].X);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 1),
				DeviceIndex, CurrControllerState->TouchPosAndForce[0].Y);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 2),
				DeviceIndex, CurrControllerState->TouchPosAndForce[0].Z);
		}
		else
		{
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 0), DeviceIndex, 0.0f);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 1), DeviceIndex, 0.0f);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTrackpadAxis(Hand, 2), DeviceIndex, 0.0f);
		}

		// Analog touch coords
		// Touch 0 maps to Motion Controller Thumbstick for hand
		// Touch 1 is currently not available (we have nothing to map it to)
		if (CurrControllerState->bTouchActive[1])
		{
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 0),
				DeviceIndex, CurrControllerState->TouchPosAndForce[1].X);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 1),
				DeviceIndex, CurrControllerState->TouchPosAndForce[1].Y);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 2),
				DeviceIndex, CurrControllerState->TouchPosAndForce[1].Z);
}
		else
		{
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 0), DeviceIndex, 0.0f);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 1), DeviceIndex, 0.0f);
			MessageHandler->OnControllerAnalog(MLTouchToUnrealTouch1Axis(Hand, 2), DeviceIndex, 0.0f);
		}

		// Analog trigger
		if (CurrControllerState->TriggerAnalog != PrevControllerState->TriggerAnalog)
		{
			MessageHandler->OnControllerAnalog(MLTriggerToUnrealTriggerAxis(Hand),
				DeviceIndex, CurrControllerState->TriggerAnalog);
		}
	}
#endif //WITH_MLSDK
}

EMagicLeapControllerType FMagicLeapController::GetMLControllerType(EControllerHand Hand) const
{
#if WITH_MLSDK
	const auto& MotionSource = ControllerMapper.GetMotionSourceForHand(Hand);
	if (MotionSource == FMagicLeapMotionSourceNames::Control0)
	{
		return EMagicLeapControllerType::Device;
	}
	if (MotionSource == FMagicLeapMotionSourceNames::Control1)
	{
		return EMagicLeapControllerType::Device;
	}
	if (MotionSource == FMagicLeapMotionSourceNames::MobileApp)
	{
		return EMagicLeapControllerType::MobileApp;
	}
#endif //WITH_MLSDK
	return EMagicLeapControllerType::None;
}

bool FMagicLeapController::PlayControllerLED(EControllerHand Hand, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
#if WITH_MLSDK
	return PlayLEDPattern(ControllerMapper.GetMotionSourceForHand(Hand), LEDPattern, LEDColor, DurationInSec);
#endif //WITH_MLSDK
	return false;
}

bool FMagicLeapController::PlayControllerLEDEffect(EControllerHand Hand, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
#if WITH_MLSDK
	return PlayLEDEffect(ControllerMapper.GetMotionSourceForHand(Hand), LEDEffect, LEDSpeed, LEDPattern, LEDColor, DurationInSec);
#endif //WITH_MLSDK

	return false;
}

bool FMagicLeapController::PlayControllerHapticFeedback(EControllerHand Hand, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity)
{
#if WITH_MLSDK
	return PlayHapticPattern(ControllerMapper.GetMotionSourceForHand(Hand), HapticPattern, Intensity);
#endif //WITH_MLSDK

	return false;
}

#undef LOCTEXT_NAMESPACE

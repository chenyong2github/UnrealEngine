// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapTabletPlugin.h"
#include "IMagicLeapPlugin.h"
#include "Engine/Engine.h"
#include "MagicLeapHandle.h"
#include "MagicLeapMath.h"

DEFINE_LOG_CATEGORY(LogMagicLeapTablet);

FMagicLeapTabletPlugin::FMagicLeapTabletPlugin()
#if WITH_MLSDK
: InputTracker(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{
	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
}

FMagicLeapTabletPlugin::~FMagicLeapTabletPlugin()
{
	if (IMagicLeapPlugin::IsAvailable())
	{
		IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);
	}
}

void FMagicLeapTabletPlugin::CreateEntityTracker()
{
#if WITH_MLSDK
	MLInputConfiguration InputConfig = { { MLInputControllerDof_6, MLInputControllerDof_6 } };
	MLResult Result = MLInputCreate(&InputConfig, &InputTracker);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapTablet, Error, TEXT("MLInputCreate failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return;
	}

	MLInputTabletDeviceCallbacksInit(&Callbacks);
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapTabletPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::DestroyEntityTracker()
{
#if WITH_MLSDK
	if (MLHandleIsValid(InputTracker))
	{
		MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, nullptr, nullptr);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		Result = MLInputDestroy(InputTracker);
		InputTracker = ML_INVALID_HANDLE;
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputDestroy failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
	}

	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
#endif // WITH_MLSDK
}

bool FMagicLeapTabletPlugin::Tick(float DeltaTime)
{
	FPendingCallbackData PendingCallbackData;
	while (PendingCallbacks.Dequeue(PendingCallbackData))
	{
		switch (PendingCallbackData.Type)
		{
		case ECallbackType::Connect: OnConnected.Broadcast(PendingCallbackData.DeviceId); break;
		case ECallbackType::Disconnect: OnDisconnected.Broadcast(PendingCallbackData.DeviceId); break;
		case ECallbackType::PenTouch: 
		{
#if WITH_MLSDK
			MLInputTabletDeviceStatesList TabletDeviceStates;
			MLInputTabletDeviceStatesListInit(&TabletDeviceStates);
			MLResult Result = MLInputGetTabletDeviceStates(InputTracker, PendingCallbackData.DeviceId, &TabletDeviceStates);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeapTablet, Error, TEXT("MLInputGetTabletDeviceStates failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
			else if (TabletDeviceStates.count > 0 && TabletDeviceStates.data[TabletDeviceStates.count - 1].valid_fields_flag & MLInputTabletDeviceStateMask_HasToolType)
			{
				PendingCallbackData.DeviceState.ToolType = MLToUnrealDeviceToolType(TabletDeviceStates.data[TabletDeviceStates.count - 1].tool_type);
			}
			Result = MLInputReleaseTabletDeviceStates(InputTracker, &TabletDeviceStates);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputReleaseTabletDeviceStates failed with error '%s'!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
			OnPenTouch.Broadcast(PendingCallbackData.DeviceId, PendingCallbackData.DeviceState);
		}
		break;
		case ECallbackType::RingTouch: OnRingTouch.Broadcast(PendingCallbackData.DeviceId, PendingCallbackData.RingTouchValue); break;
		case ECallbackType::ButtonDown: OnButtonDown.Broadcast(PendingCallbackData.DeviceId, PendingCallbackData.Button); break;
		case ECallbackType::ButtonUp: OnButtonUp.Broadcast(PendingCallbackData.DeviceId, PendingCallbackData.Button); break;
		}
	}

	return true;
}

void FMagicLeapTabletPlugin::SetConnectedDelegate(const FMagicLeapTabletOnConnectedDelegateMulti& ConnectedDelegate)
{
	OnConnected = ConnectedDelegate;
#if WITH_MLSDK
	Callbacks.on_connect = OnConnectEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return;
	}

	MLInputConnectedDevicesList ConnectedDevices;
	MLInputConnectedDevicesListInit(&ConnectedDevices);

	Result = MLInputGetConnectedDevices(InputTracker, &ConnectedDevices);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapTablet, Error, TEXT("MLInputGetConnectedDevices failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return;
	}

	for (uint32_t iConnectedTablet = 0; iConnectedTablet < ConnectedDevices.connected_tablet_device_count; ++iConnectedTablet)
	{
		ConnectedDelegate.Broadcast(ConnectedDevices.connected_tablet_device_ids[iConnectedTablet]);
	}

	Result = MLInputReleaseConnectedDevicesList(InputTracker, &ConnectedDevices);
	UE_CLOG(Result == MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputReleaseConnectedDevicesList failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::SetDisconnectedDelegate(const FMagicLeapTabletOnDisconnectedDelegateMulti& DisconnectedDelegate)
{
	OnDisconnected = DisconnectedDelegate;
#if WITH_MLSDK
	Callbacks.on_disconnect = OnDisconnectEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::SetPenTouchDelegate(const FMagicLeapTabletOnPenTouchDelegateMulti& PenTouchDelegate)
{
	OnPenTouch = PenTouchDelegate;
#if WITH_MLSDK
	Callbacks.on_pen_touch_event = OnPenTouchEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::SetRingTouchDelegate(const FMagicLeapTabletOnRingTouchDelegateMulti& RingTouchDelegate)
{
	OnRingTouch = RingTouchDelegate;
#if WITH_MLSDK
	Callbacks.on_touch_ring_event = OnRingTouchEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::SetButtonDownDelegate(const FMagicLeapTabletOnButtonDownDelegateMulti& ButtonDownDelegate)
{
	OnButtonDown = ButtonDownDelegate;
#if WITH_MLSDK
	Callbacks.on_button_down = OnButtonDownEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::SetButtonUpDelegate(const FMagicLeapTabletOnButtonUpDelegateMulti& ButtonUpDelegate)
{
	OnButtonUp = ButtonUpDelegate;
#if WITH_MLSDK
	Callbacks.on_button_up = OnButtonUpEvent;
	MLResult Result = MLInputSetTabletDeviceCallbacks(InputTracker, &Callbacks, this);
	UE_CLOG(Result != MLResult_Ok, LogMagicLeapTablet, Error, TEXT("MLInputSetTabletDeviceCallbacks failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapTabletPlugin::OnConnectEvent(uint8_t TabletDeviceId, void* Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::Connect;
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

void FMagicLeapTabletPlugin::OnDisconnectEvent(uint8_t TabletDeviceId, void* Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::Disconnect;
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

#if WITH_MLSDK
void FMagicLeapTabletPlugin::OnPenTouchEvent(uint8_t TabletDeviceId, const MLInputTabletDeviceState* TabletDeviceState, void* Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::PenTouch;
	MLToUnrealTabletDeviceState(TabletDeviceState, PendingCallbackData.DeviceState);
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

void FMagicLeapTabletPlugin::OnRingTouchEvent(uint8_t TabletDeviceId, int32_t RingTouchValue, uint64_t Timestamp, void *Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::RingTouch;
	PendingCallbackData.RingTouchValue = RingTouchValue;
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

void FMagicLeapTabletPlugin::OnButtonDownEvent(uint8_t TabletDeviceId, MLInputTabletDeviceButton Button, uint64_t Timestamp, void* Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::ButtonDown;
	PendingCallbackData.Button = MLToUnrealInputTabletDeviceButton(Button);
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

void FMagicLeapTabletPlugin::OnButtonUpEvent(uint8_t TabletDeviceId, MLInputTabletDeviceButton Button, uint64_t Timestamp, void* Data)
{
	FPendingCallbackData PendingCallbackData;
	PendingCallbackData.DeviceId = TabletDeviceId;
	PendingCallbackData.Type = ECallbackType::ButtonUp;
	PendingCallbackData.Button = MLToUnrealInputTabletDeviceButton(Button);
	static_cast<FMagicLeapTabletPlugin*>(Data)->PendingCallbacks.Enqueue(PendingCallbackData);
}

EMagicLeapInputTabletDeviceType FMagicLeapTabletPlugin::MLToUnrealInputTabletDeviceType(MLInputTabletDeviceType InDeviceType)
{
	EMagicLeapInputTabletDeviceType Type = EMagicLeapInputTabletDeviceType::Unknown;

	switch (InDeviceType)
	{
	case MLInputTabletDeviceType_Unknown: Type = EMagicLeapInputTabletDeviceType::Unknown; break;
	case MLInputTabletDeviceType_Wacom: Type = EMagicLeapInputTabletDeviceType::Wacom; break;
	case MLInputTabletDeviceType_Ensure32Bits:
	default: UE_LOG(LogMagicLeapTablet, Error, TEXT("Unexpected type encountered!"));
	}

	return Type;
}

EMagicLeapInputTabletDeviceToolType FMagicLeapTabletPlugin::MLToUnrealDeviceToolType(MLInputTabletDeviceToolType InputDeviceToolType)
{
	EMagicLeapInputTabletDeviceToolType ToolType = EMagicLeapInputTabletDeviceToolType::Unknown;

	switch (InputDeviceToolType)
	{
	case MLInputTabletDeviceToolType_Unknown: ToolType = EMagicLeapInputTabletDeviceToolType::Unknown; break;
	case MLInputTabletDeviceToolType_Pen: ToolType = EMagicLeapInputTabletDeviceToolType::Pen; break;
	case MLInputTabletDeviceToolType_Eraser: ToolType = EMagicLeapInputTabletDeviceToolType::Eraser; break;
	case MLInputTabletDeviceTootlType_Ensure32Bits:
	default: UE_LOG(LogMagicLeapTablet, Error, TEXT("Unexpected type encountered!"));
	}

	return ToolType;
}

EMagicLeapInputTabletDeviceButton FMagicLeapTabletPlugin::MLToUnrealInputTabletDeviceButton(MLInputTabletDeviceButton InMLButton)
{
	EMagicLeapInputTabletDeviceButton UnrealButton = EMagicLeapInputTabletDeviceButton::Unknown;

	switch (InMLButton)
	{
	case MLInputTabletDeviceButton_Unknown: UnrealButton = EMagicLeapInputTabletDeviceButton::Unknown; break;
	case MLInputTabletDeviceButton_Button1: UnrealButton = EMagicLeapInputTabletDeviceButton::Button1; break;
	case MLInputTabletDeviceButton_Button2: UnrealButton = EMagicLeapInputTabletDeviceButton::Button2; break;
	case MLInputTabletDeviceButton_Button3: UnrealButton = EMagicLeapInputTabletDeviceButton::Button3; break;
	case MLInputTabletDeviceButton_Button4: UnrealButton = EMagicLeapInputTabletDeviceButton::Button4; break;
	case MLInputTabletDeviceButton_Button5: UnrealButton = EMagicLeapInputTabletDeviceButton::Button5; break;
	case MLInputTabletDeviceButton_Button6: UnrealButton = EMagicLeapInputTabletDeviceButton::Button6; break;
	case MLInputTabletDeviceButton_Button7: UnrealButton = EMagicLeapInputTabletDeviceButton::Button7; break;
	case MLInputTabletDeviceButton_Button8: UnrealButton = EMagicLeapInputTabletDeviceButton::Button8; break;
	case MLInputTabletDeviceButton_Button9: UnrealButton = EMagicLeapInputTabletDeviceButton::Button9; break;
	case MLInputTabletDeviceButton_Button10: UnrealButton = EMagicLeapInputTabletDeviceButton::Button10; break;
	case MLInputTabletDeviceButton_Button11: UnrealButton = EMagicLeapInputTabletDeviceButton::Button11; break;
	case MLInputTabletDeviceButton_Button12: UnrealButton = EMagicLeapInputTabletDeviceButton::Button12; break;
	case MLInputTabletDeviceButton_Count:
	case MLInputTabletDeviceButton_Ensure32Bits:
	default: UE_LOG(LogMagicLeapTablet, Error, TEXT("Unexpected type encountered!"));
	}

	return UnrealButton;
}

void FMagicLeapTabletPlugin::MLToUnrealTabletDeviceState(const MLInputTabletDeviceState* InMLInputTabletDeviceState, FMagicLeapInputTabletDeviceState& OutUnrealTabletDeviceState)
{
	OutUnrealTabletDeviceState.Version = InMLInputTabletDeviceState->version;
	OutUnrealTabletDeviceState.Type = MLToUnrealInputTabletDeviceType(InMLInputTabletDeviceState->type);
	OutUnrealTabletDeviceState.ToolType = MLToUnrealDeviceToolType(InMLInputTabletDeviceState->tool_type);
	OutUnrealTabletDeviceState.PenTouchPosAndForce = 
	{
		InMLInputTabletDeviceState->pen_touch_pos_and_force.x,
		InMLInputTabletDeviceState->pen_touch_pos_and_force.y,
		InMLInputTabletDeviceState->pen_touch_pos_and_force.z
	};
	OutUnrealTabletDeviceState.AdditionalPenTouchData =
	{
		InMLInputTabletDeviceState->additional_pen_touch_data[0],
		InMLInputTabletDeviceState->additional_pen_touch_data[1],
		InMLInputTabletDeviceState->additional_pen_touch_data[2]
	};
	OutUnrealTabletDeviceState.bIsPenTouchActive = InMLInputTabletDeviceState->is_pen_touch_active;
	OutUnrealTabletDeviceState.bIsConnected = InMLInputTabletDeviceState->is_connected;
	OutUnrealTabletDeviceState.PenDistance = InMLInputTabletDeviceState->pen_distance;
	// Converting from nanoseconds to microseconds by dividing by 1000
	OutUnrealTabletDeviceState.Timestamp = FTimespan::FromMicroseconds(InMLInputTabletDeviceState->timestamp / 1000.0f);
	OutUnrealTabletDeviceState.ValidFieldsFlag = InMLInputTabletDeviceState->valid_fields_flag;
}
#endif // WITH_MLSDK

IMPLEMENT_MODULE(FMagicLeapTabletPlugin, MagicLeapTablet);

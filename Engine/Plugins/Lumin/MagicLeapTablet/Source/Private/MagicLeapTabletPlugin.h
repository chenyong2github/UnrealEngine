// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IMagicLeapTabletPlugin.h"
#include "IMagicLeapTrackerEntity.h"
#include "Lumin/CAPIShims/LuminAPITablet.h"
#include "Containers/Queue.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapTablet, Verbose, All);

class FMagicLeapTabletPlugin : public IMagicLeapTabletPlugin, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapTabletPlugin();
	virtual ~FMagicLeapTabletPlugin();

	void CreateEntityTracker() override;
	void DestroyEntityTracker() override;
	bool Tick(float DeltaTime);

	void SetConnectedDelegate(const FMagicLeapTabletOnConnectedDelegateMulti& ConnectedDelegate);
	void SetDisconnectedDelegate(const FMagicLeapTabletOnDisconnectedDelegateMulti& DisconnectedDelegate);
	void SetPenTouchDelegate(const FMagicLeapTabletOnPenTouchDelegateMulti& PenTouchDelegate);
	void SetRingTouchDelegate(const FMagicLeapTabletOnRingTouchDelegateMulti& RingTouchDelegate);
	void SetButtonDownDelegate(const FMagicLeapTabletOnButtonDownDelegateMulti& ButtonDownDelegate);
	void SetButtonUpDelegate(const FMagicLeapTabletOnButtonUpDelegateMulti& ButtonDownDelegate);

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	FMagicLeapTabletOnConnectedDelegateMulti OnConnected;
	FMagicLeapTabletOnDisconnectedDelegateMulti OnDisconnected;
	FMagicLeapTabletOnPenTouchDelegateMulti OnPenTouch;
	FMagicLeapTabletOnRingTouchDelegateMulti OnRingTouch;
	FMagicLeapTabletOnButtonDownDelegateMulti OnButtonDown;
	FMagicLeapTabletOnButtonUpDelegateMulti OnButtonUp;
	enum ECallbackType
	{
		Connect,
		Disconnect,
		PenTouch,
		RingTouch,
		ButtonDown,
		ButtonUp
	};
	struct FPendingCallbackData
	{
		uint8 DeviceId;
		ECallbackType Type;
		FMagicLeapInputTabletDeviceState DeviceState;
		int32_t RingTouchValue;
		EMagicLeapInputTabletDeviceButton Button;
	};
	TQueue<FPendingCallbackData, EQueueMode::Spsc> PendingCallbacks;
#if WITH_MLSDK
	MLHandle InputTracker;
	MLInputTabletDeviceCallbacks Callbacks;
#endif // WITH_MLSDK

	static void OnConnectEvent(uint8_t TabletDeviceId, void* Data);
	static void OnDisconnectEvent(uint8_t TabletDeviceId, void* Data);
#if WITH_MLSDK
	static void OnPenTouchEvent(uint8_t TabletDeviceId, const MLInputTabletDeviceState* TabletDeviceState, void* Data);
	static void OnRingTouchEvent(uint8_t TabletDeviceId, int32_t RingTouchValue, uint64_t Timestamp, void *Data);
	static void OnButtonDownEvent(uint8_t TabletDeviceId, MLInputTabletDeviceButton Button, uint64_t Timestamp, void* Data);
	static void OnButtonUpEvent(uint8_t TabletDeviceId, MLInputTabletDeviceButton Button, uint64_t Timestamp, void* Data);

	static EMagicLeapInputTabletDeviceType MLToUnrealInputTabletDeviceType(MLInputTabletDeviceType InDeviceType);
	static EMagicLeapInputTabletDeviceToolType MLToUnrealDeviceToolType(MLInputTabletDeviceToolType InputDeviceToolType);
	static EMagicLeapInputTabletDeviceButton MLToUnrealInputTabletDeviceButton(MLInputTabletDeviceButton InMLButton);
	static void MLToUnrealTabletDeviceState(const MLInputTabletDeviceState* InMLInputTabletDeviceState, FMagicLeapInputTabletDeviceState& OutUnrealTabletDeviceState);
#endif // WITH_MLSDK
};

inline FMagicLeapTabletPlugin& GetMagicLeapTabletPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapTabletPlugin>("MagicLeapTablet");
}

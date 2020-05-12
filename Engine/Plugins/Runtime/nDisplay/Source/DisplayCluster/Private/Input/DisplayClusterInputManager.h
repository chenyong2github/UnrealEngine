// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/IPDisplayClusterInputManager.h"
#include "Devices/DisplayClusterInputDeviceTraits.h"
#include "Network/DisplayClusterMessage.h"

class IDisplayClusterInputDevice;
struct FDisplayClusterVrpnAnalogChannelData;
struct FDisplayClusterVrpnButtonChannelData;
struct FDisplayClusterVrpnKeyboardChannelData;
struct FDisplayClusterVrpnTrackerChannelData;


/**
 * Input manager. Implements everything related to VR input devices (VRPN, etc.)
 */
class FDisplayClusterInputManager
	: public IPDisplayClusterInputManager
{
public:
	FDisplayClusterInputManager();
	virtual ~FDisplayClusterInputManager();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(const FString& ConfigPath, const FString& NodeId) override;
	virtual void EndSession() override;
	virtual bool StartScene(UWorld* InWorld) override;
	virtual void EndScene() override;
	virtual void StartFrame(uint64 FrameNum) override;
	virtual void PreTick(float DeltaSeconds);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device API
	virtual const IDisplayClusterInputDevice* GetDevice(EDisplayClusterInputDeviceType DeviceType, const FString& DeviceID) const override;

	// Device amount
	virtual uint32 GetAxisDeviceAmount()     const override;
	virtual uint32 GetButtonDeviceAmount()   const override;
	virtual uint32 GetKeyboardDeviceAmount() const override;
	virtual uint32 GetTrackerDeviceAmount()  const override;

	// Device IDs
	virtual void GetAxisDeviceIds    (TArray<FString>& DeviceIDs) const override;
	virtual void GetButtonDeviceIds  (TArray<FString>& DeviceIDs) const override;
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const override;
	virtual void GetTrackerDeviceIds (TArray<FString>& DeviceIDs) const override;

	// Axes data access
	virtual bool GetAxis(const FString& DeviceID, const uint8 Channel, float& Value) const override;

	// Button data access
	virtual bool GetButtonState   (const FString& DeviceID, const uint8 Channel, bool& CurrentState)    const override;
	virtual bool IsButtonPressed  (const FString& DeviceID, const uint8 Channel, bool& IsPressedCurrently)  const override;
	virtual bool IsButtonReleased (const FString& DeviceID, const uint8 Channel, bool& IsReleasedCurrently) const override;
	virtual bool WasButtonPressed (const FString& DeviceID, const uint8 Channel, bool& WasPressed)  const override;
	virtual bool WasButtonReleased(const FString& DeviceID, const uint8 Channel, bool& WasReleased) const override;

	// Keyboard data access
	virtual bool GetKeyboardState   (const FString& DeviceID, const uint8 Channel, bool& CurrentState)    const override;
	virtual bool IsKeyboardPressed  (const FString& DeviceID, const uint8 Channel, bool& IsPressedCurrently)  const override;
	virtual bool IsKeyboardReleased (const FString& DeviceID, const uint8 Channel, bool& IsReleasedCurrently) const override;
	virtual bool WasKeyboardPressed (const FString& DeviceID, const uint8 Channel, bool& WasPressed)  const override;
	virtual bool WasKeyboardReleased(const FString& DeviceID, const uint8 Channel, bool& WasReleased) const override;

	// Tracking data access
	virtual bool GetTrackerLocation(const FString& DeviceID, const uint8 Channel, FVector& Location) const override;
	virtual bool GetTrackerQuat(const FString& DeviceID, const uint8 Channel, FQuat& Rotation) const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterInputManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void Update() override;

	virtual void ExportInputData(FDisplayClusterMessage::DataType& InputData) const override;
	virtual void ImportInputData(const FDisplayClusterMessage::DataType& InputData) override;

private:
	typedef TUniquePtr<IDisplayClusterInputDevice>    TDevice;
	typedef TMap<FString, TDevice>         TDeviceClassMap;
	typedef TMap<int, TDeviceClassMap>     TDeviceMap;

	bool InitDevices();
	void ReleaseDevices();
	void UpdateInputDataCache();

	// Device data
	bool GetAxisData    (const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnAnalogChannelData& Data) const;
	bool GetButtonData  (const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnButtonChannelData& Data) const;
	bool GetKeyboardData(const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnKeyboardChannelData& Data) const;
	bool GetTrackerData (const FString& DeviceID, const uint8 Channel, FDisplayClusterVrpnTrackerChannelData& Data) const;

private:
	// Input devices
	TDeviceMap Devices;
	// Current config path
	FString ConfigPath;
	// Current cluster node ID
	FString ClusterNodeId;
	// Current world
	UWorld* CurrentWorld;

	mutable FCriticalSection InternalsSyncScope;

private:
	template<int DevTypeID>
	uint32 GetDeviceAmount_impl() const;

	template<int DevTypeID>
	void GetDeviceIds_impl(TArray<FString>& IDs) const;

	template<int DevTypeID>
	bool GetChannelData_impl(const FString& DeviceID, const uint8 Channel, typename display_cluster_input_device_traits<DevTypeID>::dev_channel_data_type& data) const;

private:
	static constexpr auto SerializationDeviceTypeNameDelimiter = TEXT(" ");
};

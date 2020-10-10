// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Devices/VRPN/Tracker/DisplayClusterVrpnTrackerInputDataHolder.h"

#include "DisplayClusterConfigurationTypes.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "vrpn/vrpn_Tracker.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif


/**
 * VRPN tracker device implementation
 */
class FDisplayClusterVrpnTrackerInputDevice
	: public FDisplayClusterVrpnTrackerInputDataHolder
{
public:
	FDisplayClusterVrpnTrackerInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceTracker* CfgDevice);
	virtual ~FDisplayClusterVrpnTrackerInputDevice();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void Update() override;
	virtual void PostUpdate() override;
	virtual bool Initialize() override;

protected:
	// Per-channel dirty state
	TMap<int32, bool> DirtyMap;

	// Transform form tracker space to DisplayCluster space
	void TransformCoordinates(FDisplayClusterVrpnTrackerChannelData& Data) const;


private:
	// Tracker origin
	FVector  OriginLoc  = FVector::ZeroVector;
	FRotator OriginRot  = FRotator::ZeroRotator;
	FQuat    OriginQuat = FQuat::Identity;

private:
	// Coordinate system conversion
	enum AxisMapType { X = 0, NX, Y, NY, Z, NZ, W, NW };

	// Internal conversion helpers
	AxisMapType ConvertToInternalMappingType(EDisplayClusterConfigurationTrackerMapping From) const;
	AxisMapType ComputeAxisW(const AxisMapType Front, const AxisMapType Right, const AxisMapType Up) const;
	FVector  GetMappedLocation(const FVector& Loc, const AxisMapType Front, const AxisMapType Right, const AxisMapType Up) const;
	FQuat    GetMappedQuat(const FQuat& Quat, const AxisMapType Front, const AxisMapType Right, const AxisMapType Up, const AxisMapType AxisW) const;

	// Tracker space to DisplayCluster space axis mapping
	AxisMapType AxisFront;
	AxisMapType AxisRight;
	AxisMapType AxisUp;
	AxisMapType AxisW;

private:
	// Data update handler
	static void VRPN_CALLBACK HandleTrackerDevice(void *UserData, vrpn_TRACKERCB const TrackerData);

private:
	// The device (PIMPL)
	TUniquePtr<vrpn_Tracker_Remote> DevImpl;
};

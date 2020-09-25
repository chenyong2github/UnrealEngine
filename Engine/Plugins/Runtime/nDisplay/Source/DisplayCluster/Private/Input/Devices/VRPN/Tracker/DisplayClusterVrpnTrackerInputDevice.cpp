// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Tracker/DisplayClusterVrpnTrackerInputDevice.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterVrpnTrackerInputDevice::FDisplayClusterVrpnTrackerInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceTracker* CfgDevice)
	: FDisplayClusterVrpnTrackerInputDataHolder(DeviceId, CfgDevice)
{
	// Location & Rotation
	OriginLoc = CfgDevice->OriginLocation;
	OriginRot = CfgDevice->OriginRotation;
	OriginQuat = OriginRot.Quaternion();

	// Coordinate system mapping
	AxisFront = ConvertToInternalMappingType(CfgDevice->Front);
	AxisRight = ConvertToInternalMappingType(CfgDevice->Right);
	AxisUp    = ConvertToInternalMappingType(CfgDevice->Up);
	AxisW = ComputeAxisW(AxisFront, AxisRight, AxisUp);
}

FDisplayClusterVrpnTrackerInputDevice::~FDisplayClusterVrpnTrackerInputDevice()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterVrpnTrackerInputDevice::Update()
{
	if (DevImpl)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("Updating device: %s"), *GetId());
		DevImpl->mainloop();
	}
}

void FDisplayClusterVrpnTrackerInputDevice::PostUpdate()
{
	// Perform coordinates conversion
	for (auto it = DeviceData.CreateIterator(); it; ++it)
	{
		if (DirtyMap.Contains(it->Key))
		{
			// Convert data from updated channels only
			if (DirtyMap[it->Key] == true)
			{
				TransformCoordinates(it->Value);
				DirtyMap[it->Key] = false;
			}
		}
	}
}

bool FDisplayClusterVrpnTrackerInputDevice::Initialize()
{
	// Instantiate device implementation
	DevImpl.Reset(new vrpn_Tracker_Remote(TCHAR_TO_UTF8(*Address)));

	// Register update handler
	if (DevImpl->register_change_handler(this, &FDisplayClusterVrpnTrackerInputDevice::HandleTrackerDevice) != 0)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("%s - couldn't register VRPN change handler"), *ToString());
		return false;
	}

	// Base initialization
	return FDisplayClusterVrpnTrackerInputDataHolder::Initialize();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterVrpnTrackerInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
	// Location
	float LocGetX(const FVector& Loc)  { return  Loc.X; }
	float LocGetNX(const FVector& Loc) { return -Loc.X; }

	float LocGetY(const FVector& Loc)  { return  Loc.Y; }
	float LocGetNY(const FVector& Loc) { return -Loc.Y; }

	float LocGetZ(const FVector& Loc)  { return  Loc.Z; }
	float LocGetNZ(const FVector& Loc) { return -Loc.Z; }

	// Rotation
	float RotGetX(const FQuat& Quat)  { return  Quat.X; }
	float RotGetNX(const FQuat& Quat) { return -Quat.X; }

	float RotGetY(const FQuat& Quat)  { return  Quat.Y; }
	float RotGetNY(const FQuat& Quat) { return -Quat.Y; }

	float RotGetZ(const FQuat& Quat)  { return  Quat.Z; }
	float RotGetNZ(const FQuat& Quat) { return -Quat.Z; }

	float RotGetW(const FQuat& Quat)  { return  Quat.W; }
	float RotGetNW(const FQuat& Quat) { return -Quat.W; }

	typedef float(*TLocGetter)(const FVector& Loc);
	typedef float(*TRotGetter)(const FQuat&   Rot);
}

FDisplayClusterVrpnTrackerInputDevice::AxisMapType FDisplayClusterVrpnTrackerInputDevice::ConvertToInternalMappingType(EDisplayClusterConfigurationTrackerMapping From) const
{
	switch (From)
	{
	case EDisplayClusterConfigurationTrackerMapping::X:
		return AxisMapType::X;
	case EDisplayClusterConfigurationTrackerMapping::NX:
		return AxisMapType::NX;
	case EDisplayClusterConfigurationTrackerMapping::Y:
		return AxisMapType::Y;
	case EDisplayClusterConfigurationTrackerMapping::NY:
		return AxisMapType::NY;
	case EDisplayClusterConfigurationTrackerMapping::Z:
		return AxisMapType::Z;
	case EDisplayClusterConfigurationTrackerMapping::NZ:
		return AxisMapType::NZ;
	default:
		UE_LOG(LogDisplayClusterInputVRPN, Warning, TEXT("There is something unexpected in ConvertToInternalMappingType function. Looks like some mapping enum has been changed."));
	}

	// Should never be here
	return AxisMapType::X;
}

FDisplayClusterVrpnTrackerInputDevice::AxisMapType FDisplayClusterVrpnTrackerInputDevice::ComputeAxisW(const AxisMapType Front, const AxisMapType Right, const AxisMapType Up) const
{
	int Det = 1;

	if (Front == AxisMapType::NX || Front == AxisMapType::NY || Front == AxisMapType::NZ)
		Det *= -1;

	if (Right == AxisMapType::NX || Right == AxisMapType::NY || Right == AxisMapType::NZ)
		Det *= -1;

	if (Up == AxisMapType::NX || Up == AxisMapType::NY || Up == AxisMapType::NZ)
		Det *= -1;

	return (Det < 0) ? AxisMapType::NW : AxisMapType::W;
}

FVector FDisplayClusterVrpnTrackerInputDevice::GetMappedLocation(const FVector& Loc, const AxisMapType Front, const AxisMapType Right, const AxisMapType Up) const
{
	static TLocGetter funcs[] = { &LocGetX, &LocGetNX, &LocGetY, &LocGetNY, &LocGetZ, &LocGetNZ };
	return FVector(funcs[Front](Loc), funcs[Right](Loc), funcs[Up](Loc));
}

FQuat FDisplayClusterVrpnTrackerInputDevice::GetMappedQuat(const FQuat& Quat, const AxisMapType Front, const AxisMapType Right, const AxisMapType Up, const AxisMapType InAxisW) const
{
	static TRotGetter funcs[] = { &RotGetX, &RotGetNX, &RotGetY, &RotGetNY, &RotGetZ, &RotGetNZ, &RotGetW, &RotGetNW };
	return FQuat(funcs[Front](Quat), funcs[Right](Quat), funcs[Up](Quat), -Quat.W);// funcs[axisW](quat));
}

void FDisplayClusterVrpnTrackerInputDevice::TransformCoordinates(FDisplayClusterVrpnTrackerChannelData &Data) const
{
	UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("TransformCoordinates old: <loc:%s> <quat:%s>"), *Data.TrackerLoc.ToString(), *Data.TrackerQuat.ToString());

	// Transform location
	Data.TrackerLoc = OriginLoc + GetMappedLocation(Data.TrackerLoc, AxisFront, AxisRight, AxisUp);

	// Transform rotation
	Data.TrackerQuat = OriginQuat * Data.TrackerQuat;
	Data.TrackerQuat = GetMappedQuat(Data.TrackerQuat, AxisFront, AxisRight, AxisUp, AxisW);

	UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("TransformCoordinates new: <loc:%s> <quat:%s>"), *Data.TrackerLoc.ToString(), *Data.TrackerQuat.ToString());
}

void VRPN_CALLBACK FDisplayClusterVrpnTrackerInputDevice::HandleTrackerDevice(void *UserData, vrpn_TRACKERCB const TrackerData)
{
	auto Dev = reinterpret_cast<FDisplayClusterVrpnTrackerInputDevice*>(UserData);
	
	const FVector Loc (TrackerData.pos[0],  TrackerData.pos[1],  TrackerData.pos[2]);
	const FQuat   Quat(TrackerData.quat[0], TrackerData.quat[1], TrackerData.quat[2], TrackerData.quat[3]);

	const FDisplayClusterVrpnTrackerChannelData data{ Loc, Quat };
	auto Item = &Dev->DeviceData.Add(TrackerData.sensor, data);

	Dev->DirtyMap.Add(static_cast<int32>(TrackerData.sensor), true);

	UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("Tracker %s:%d {loc %s} {rot %s}"), *Dev->GetId(), TrackerData.sensor, *Item->TrackerLoc.ToString(), *Item->TrackerQuat.ToString());
}

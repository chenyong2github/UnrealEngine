// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinModule.h"
#include "MagicLeapMath.h"
#include "MagicLeapCFUID.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Lumin/CAPIShims/LuminAPIPassableWorld.h"
#include "Lumin/CAPIShims/LuminAPIPersistentCoordinateFrames.h"

DEFINE_LOG_CATEGORY(LogMagicLeapARPin);

UMagicLeapARPinSaveGame::UMagicLeapARPinSaveGame()
: PinnedID()
, ComponentWorldTransform(FTransform::Identity)
, PinTransform(FTransform::Identity)
{}

#if WITH_MLSDK
EMagicLeapPassableWorldError MLToUnrealPassableWorldError(MLResult result)
{
	switch (result)
	{
		case MLResult_Ok: return EMagicLeapPassableWorldError::None;
		case MLPassableWorldResult_LowMapQuality: return EMagicLeapPassableWorldError::LowMapQuality;
		case MLPassableWorldResult_UnableToLocalize: return EMagicLeapPassableWorldError::UnableToLocalize;
		case MLPassableWorldResult_ServerUnavailable: return EMagicLeapPassableWorldError::Unavailable;
		case MLResult_PrivilegeDenied: return EMagicLeapPassableWorldError::PrivilegeDenied;
		case MLResult_InvalidParam: return EMagicLeapPassableWorldError::InvalidParam;
		case MLResult_UnspecifiedFailure: return EMagicLeapPassableWorldError::UnspecifiedFailure;
	}
	return EMagicLeapPassableWorldError::UnspecifiedFailure;
}
#endif //WITH_MLSDK

FMagicLeapARPinModule::FMagicLeapARPinModule()
	: MagicLeap::IAppEventHandler({ EMagicLeapPrivilege::PwFoundObjRead })
	, bWasTrackerValidOnPause(false)
	, bCreateTracker(false)
#if WITH_MLSDK
	, Tracker(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
{}

EMagicLeapPassableWorldError FMagicLeapARPinModule::CreateTracker()
{
	EMagicLeapPassableWorldError ErrorCode = EMagicLeapPassableWorldError::Unavailable;
#if WITH_MLSDK
	if (!MLHandleIsValid(Tracker))
	{
		MagicLeap::EPrivilegeState PrivilegeState = GetPrivilegeStatus(EMagicLeapPrivilege::PwFoundObjRead, false);
		if (PrivilegeState == MagicLeap::EPrivilegeState::Granted)
		{
			// TODO: add retires like in Image tracker if error is LowMapQuality, UnableToLocalize, ServerUnavailable or PrivilegeDenied.
			// Retrying for PrivilegeDenied would only make sense when MLPrivilege runtime api is functional.
			MLResult Result = MLPersistentCoordinateFrameTrackerCreate(&Tracker);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPin, Error, TEXT("Failed to create persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			ErrorCode = MLToUnrealPassableWorldError(Result);
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Denied)
		{
			UE_LOG(LogMagicLeapARPin, Error, TEXT("Failed to initialize persistent coordinate frame tracker due to lack of privilege!"));
			ErrorCode = EMagicLeapPassableWorldError::PrivilegeDenied;
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Pending)
		{
			ErrorCode = EMagicLeapPassableWorldError::PrivilegeRequestPending;
			bCreateTracker = true;
		}
	}
	else
	{
		ErrorCode = EMagicLeapPassableWorldError::None;
	}
#endif //WITH_MLSDK
	return ErrorCode;
}

EMagicLeapPassableWorldError FMagicLeapARPinModule::DestroyTracker()
{
	EMagicLeapPassableWorldError ErrorCode = EMagicLeapPassableWorldError::Unavailable;
#if WITH_MLSDK
	if (MLHandleIsValid(Tracker))
	{
		MLResult Result = MLPersistentCoordinateFrameTrackerDestroy(Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPin, Error, TEXT("Failed to destroy persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
		Tracker = ML_INVALID_HANDLE;
		ErrorCode = MLToUnrealPassableWorldError(Result);
		bCreateTracker = false;
	}
#endif //WITH_MLSDK
	return ErrorCode;
}

bool FMagicLeapARPinModule::IsTrackerValid() const
{
#if WITH_MLSDK
	return MLHandleIsValid(Tracker);
#else
	return false;
#endif //WITH_MLSDK
}

EMagicLeapPassableWorldError FMagicLeapARPinModule::GetNumAvailableARPins(int32& Count)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
		MagicLeap::EPrivilegeState PrivilegeState = GetPrivilegeStatus(EMagicLeapPrivilege::PwFoundObjRead, false);
		if (PrivilegeState == MagicLeap::EPrivilegeState::Granted)
		{
#if WITH_MLSDK
			uint32 NumPersistentFrames = 0;
			MLResult Result = MLPersistentCoordinateFrameGetCount(Tracker, &NumPersistentFrames);
			if (MLResult_Ok == Result)
			{
				Count = static_cast<int32>(NumPersistentFrames);
			}
			else
			{
				UE_LOG(LogMagicLeapARPin, Error, TEXT("MLPersistentCoordinateFrameGetCount failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
				Count = 0;
			}
			ErrorReturn = MLToUnrealPassableWorldError(Result);
#endif // WITH_MLSDK
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Pending)
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeRequestPending;
		}
		else
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeDenied;
		}
	}
	return ErrorReturn;
}

EMagicLeapPassableWorldError FMagicLeapARPinModule::GetAvailableARPins(int32 NumRequested, TArray<FGuid>& CoordinateFrames)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
		MagicLeap::EPrivilegeState PrivilegeState = GetPrivilegeStatus(EMagicLeapPrivilege::PwFoundObjRead, false);
		if (PrivilegeState == MagicLeap::EPrivilegeState::Granted)
		{
			if (NumRequested > 0)
			{
#if WITH_MLSDK
				CoordinateFrames.Reset();
				CoordinateFrames.AddZeroed(NumRequested);
				MLCoordinateFrameUID* ArrayDataPointer = reinterpret_cast<MLCoordinateFrameUID*>(CoordinateFrames.GetData());
				MLResult QueryResult = MLPersistentCoordinateFrameGetAllEx(Tracker, NumRequested, ArrayDataPointer);
				if (MLResult_Ok != QueryResult)
				{
					UE_LOG(LogMagicLeapARPin, Error, TEXT("MLPersistentCoordinateFrameGetAllEx failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(QueryResult)));
				}
				ErrorReturn = MLToUnrealPassableWorldError(QueryResult);
#endif // WITH_MLSDK
			}
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Pending)
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeRequestPending;
		}
		else
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeDenied;
		}
	}
	return ErrorReturn;
}

EMagicLeapPassableWorldError FMagicLeapARPinModule::GetClosestARPin(const FVector& SearchPoint, FGuid& PinID)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
		MagicLeap::EPrivilegeState PrivilegeState = GetPrivilegeStatus(EMagicLeapPrivilege::PwFoundObjRead, false);
		if (PrivilegeState == MagicLeap::EPrivilegeState::Granted)
		{
			const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
			if (MLPlugin.IsMagicLeapHMDValid())
			{
				const float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
				const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
#if WITH_MLSDK
				MLVec3f Target = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(SearchPoint), WorldToMetersScale);
				MLResult Result = MLPersistentCoordinateFrameGetClosest(Tracker, &Target, reinterpret_cast<MLCoordinateFrameUID*>(&PinID));
				UE_CLOG(MLResult_Ok != Result, LogMagicLeapARPin, Error, TEXT("MLPersistentCoordinateFrameGetClosest failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
				ErrorReturn = MLToUnrealPassableWorldError(Result);
#endif // WITH_MLSDK
			}
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Pending)
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeRequestPending;
		}
		else
		{
			ErrorReturn = EMagicLeapPassableWorldError::PrivilegeDenied;
		}
	}

	return ErrorReturn;
}

bool FMagicLeapARPinModule::GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();

	if (!MLPlugin.IsMagicLeapHMDValid())
	{
		return false;
	}

	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		EMagicLeapTransformFailReason FailReason = EMagicLeapTransformFailReason::None;
		FTransform Pose = FTransform::Identity;
		if (MLPlugin.GetTransform(PinID, Pose, FailReason))
		{
			const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
			Pose = Pose * TrackingToWorld;
			Position = Pose.GetLocation();
			Orientation = Pose.Rotator();
			PinFoundInEnvironment = true;
			return true;
		}
		PinFoundInEnvironment = (FailReason != EMagicLeapTransformFailReason::PoseNotFound);
	}
	return false;
}

void FMagicLeapARPinModule::OnAppShutDown()
{
	DestroyTracker();
}

void FMagicLeapARPinModule::OnAppTick()
{
	if (!IsTrackerValid() && bCreateTracker)
	{
		MagicLeap::EPrivilegeState PrivilegeState = GetPrivilegeStatus(EMagicLeapPrivilege::PwFoundObjRead, false);
		if (PrivilegeState == MagicLeap::EPrivilegeState::Granted)
		{
#if WITH_MLSDK
			// TODO: add retires like in Image tracker if error is LowMapQuality, UnableToLocalize, ServerUnavailable or PrivilegeDenied.
			// Retrying for PrivilegeDenied would only make sense when MLPrivilege runtime api is functional.
			MLResult Result = MLPersistentCoordinateFrameTrackerCreate(&Tracker);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPin, Error, TEXT("Failed to create persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
#endif // WITH_MLSDK
			bCreateTracker = false;
		}
		else if (PrivilegeState == MagicLeap::EPrivilegeState::Denied)
		{
			UE_LOG(LogMagicLeapARPin, Error, TEXT("Failed to initialize persistent coordinate frame tracker due to lack of privilege!"));
			bCreateTracker = false;
		}
	}
}

void FMagicLeapARPinModule::OnAppPause()
{
#if WITH_MLSDK
	bWasTrackerValidOnPause = MLHandleIsValid(Tracker);
#endif // WITH_MLSDK
	// need to destroy the tracker here in case privileges are removed while the app is dormant.
	DestroyTracker();
}

void FMagicLeapARPinModule::OnAppResume()
{
	if (bWasTrackerValidOnPause)
	{
		CreateTracker();
	}
}

IMPLEMENT_MODULE(FMagicLeapARPinModule, MagicLeapARPin);

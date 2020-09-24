// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinImplModule.h"
#include "MagicLeapMath.h"
#include "MagicLeapCFUID.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Lumin/CAPIShims/LuminAPIPassableWorld.h"
#include "Lumin/CAPIShims/LuminAPIPersistentCoordinateFrames.h"

DEFINE_LOG_CATEGORY(LogMagicLeapARPinImpl);

constexpr double kPrisonSentenceSec = 5.0;

#if WITH_MLSDK
EMagicLeapPassableWorldError MLToUnrealPassableWorldError(MLResult result)
{
	switch (result)
	{
		case MLResult_Ok: return EMagicLeapPassableWorldError::None;
		case MLPassableWorldResult_LowMapQuality: return EMagicLeapPassableWorldError::LowMapQuality;
		case MLPassableWorldResult_UnableToLocalize: return EMagicLeapPassableWorldError::UnableToLocalize;
		case MLPassableWorldResult_ServerUnavailable: return EMagicLeapPassableWorldError::Unavailable;
		case MLPassableWorldResult_SharedWorldNotEnabled: return EMagicLeapPassableWorldError::SharedWorldNotEnabled;
		case MLPassableWorldResult_NotFound: return EMagicLeapPassableWorldError::PinNotFound;
		case MLResult_PrivilegeDenied: return EMagicLeapPassableWorldError::PrivilegeDenied;
		case MLResult_InvalidParam: return EMagicLeapPassableWorldError::InvalidParam;
		case MLResult_UnspecifiedFailure: return EMagicLeapPassableWorldError::UnspecifiedFailure;
	}
	return EMagicLeapPassableWorldError::UnspecifiedFailure;
}

EMagicLeapARPinType MLToUnrealPinType(MLPersistentCoordinateFrameType PinType)
{
	switch (PinType)
	{
		case MLPersistentCoordinateFrameType_SingleUserSingleSession: return EMagicLeapARPinType::SingleUserSingleSession;
		case MLPersistentCoordinateFrameType_SingleUserMultiSession: return EMagicLeapARPinType::SingleUserMultiSession;
		case MLPersistentCoordinateFrameType_MultiUserMultiSession: return EMagicLeapARPinType::MultiUserMultiSession;
	}

	return EMagicLeapARPinType::SingleUserSingleSession;
}
#endif //WITH_MLSDK

FMagicLeapARPinImplModule::FMagicLeapARPinImplModule()
	: MagicLeap::IAppEventHandler()
	, bCreateTracker(false)
	, bPerceptionEnabled(false)
	, Settings(nullptr)
	, PreviousTime(FPlatformTime::Seconds())
	, bHasCompletedPrisonTime(false)
#if WITH_MLSDK
	, Tracker(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
{
	// Register "MagicLeapARPinFeature" modular feature manually
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
}

FMagicLeapARPinImplModule::~FMagicLeapARPinImplModule()
{
	IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::CreateTracker()
{
	EMagicLeapPassableWorldError ErrorCode = EMagicLeapPassableWorldError::Unavailable;
#if WITH_MLSDK
	if (!MLHandleIsValid(Tracker))
	{
		if (bPerceptionEnabled)
		{
			// TODO: add retries like in Image tracker if error is LowMapQuality, UnableToLocalize, ServerUnavailable or PrivilegeDenied.
			// Retrying for PrivilegeDenied would only make sense when MLPrivilege runtime api is functional.
			MLResult Result = MLPersistentCoordinateFrameTrackerCreate(&Tracker);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPinImpl, Error, TEXT("Failed to create persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			ErrorCode = MLToUnrealPassableWorldError(Result);
		}
		else
		{
			ErrorCode = EMagicLeapPassableWorldError::StartupPending;
			bCreateTracker = true;
		}
	}
	else
	{
		ErrorCode = EMagicLeapPassableWorldError::None;
	}
#endif //WITH_MLSDK

	if (Settings == nullptr)
	{
		Settings = GetDefault<UMagicLeapARPinSettings>();
	}

	return ErrorCode;
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::DestroyTracker()
{
	EMagicLeapPassableWorldError ErrorCode = EMagicLeapPassableWorldError::Unavailable;
#if WITH_MLSDK
	if (MLHandleIsValid(Tracker))
	{
		MLResult Result = MLPersistentCoordinateFrameTrackerDestroy(Tracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPinImpl, Error, TEXT("Failed to destroy persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
		Tracker = ML_INVALID_HANDLE;
		ErrorCode = MLToUnrealPassableWorldError(Result);

		Settings = nullptr;
		OldPinsAndStates.Empty();
		PendingAdded.Empty();
		PendingUpdated.Empty();
		PendingDeleted.Empty();

		PreviousTime = 0.0;
	}

	bCreateTracker = false;

#endif //WITH_MLSDK

	return ErrorCode;
}

bool FMagicLeapARPinImplModule::IsTrackerValid() const
{
#if WITH_MLSDK
	return MLHandleIsValid(Tracker);
#else
	return false;
#endif //WITH_MLSDK
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::GetNumAvailableARPins(int32& Count)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
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
			UE_LOG(LogMagicLeapARPinImpl, Error, TEXT("MLPersistentCoordinateFrameGetCount failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			Count = 0;
		}
		ErrorReturn = MLToUnrealPassableWorldError(Result);
#endif // WITH_MLSDK
	}
	return ErrorReturn;
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::GetAvailableARPins(int32 NumRequested, TArray<FGuid>& PinCoordinateFrames)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;

		int32 NumAvailable = 0;
		GetNumAvailableARPins(NumAvailable);

		// clamp to max possible to avoid unnecesarry data allocation when we do CoordinateFrames.AddZeroed(NumRequested)
		if (NumRequested < 0 || NumRequested > NumAvailable)
		{
			NumRequested = NumAvailable;
		}

		if (NumRequested > 0)
		{
#if WITH_MLSDK
			PinCoordinateFrames.Reset(NumRequested);
			PinCoordinateFrames.AddZeroed(NumRequested);
			MLCoordinateFrameUID* ArrayDataPointer = reinterpret_cast<MLCoordinateFrameUID*>(PinCoordinateFrames.GetData());
			MLResult QueryResult = MLPersistentCoordinateFrameGetAllEx(Tracker, NumRequested, ArrayDataPointer);
			if (MLResult_Ok != QueryResult)
			{
				UE_LOG(LogMagicLeapARPinImpl, Error, TEXT("MLPersistentCoordinateFrameGetAllEx failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(QueryResult)));
			}
			ErrorReturn = MLToUnrealPassableWorldError(QueryResult);
#endif // WITH_MLSDK
		}
	}
	return ErrorReturn;
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::GetClosestARPin(const FVector& SearchPoint, FGuid& PinID)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
		const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
		if (MLPlugin.IsMagicLeapHMDValid())
		{
			const float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
			const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
#if WITH_MLSDK
			MLVec3f Target = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(SearchPoint), WorldToMetersScale);
			MLResult Result = MLPersistentCoordinateFrameGetClosest(Tracker, &Target, reinterpret_cast<MLCoordinateFrameUID*>(&PinID));
			UE_CLOG(MLResult_Ok != Result, LogMagicLeapARPinImpl, Error, TEXT("MLPersistentCoordinateFrameGetClosest failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			ErrorReturn = MLToUnrealPassableWorldError(Result);
#endif // WITH_MLSDK
		}
	}

	return ErrorReturn;
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::QueryARPins(const FMagicLeapARPinQuery& Query, TArray<FGuid>& Pins)
{
	EMagicLeapPassableWorldError ErrorReturn = CreateTracker();
	if (ErrorReturn == EMagicLeapPassableWorldError::None)
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
		const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
		if (MLPlugin.IsMagicLeapHMDValid())
		{
			const float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
			const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
#if WITH_MLSDK
			MLPersistentCoordinateFramesQueryFilter QueryFilter;
			MLPersistentCoordinateQueryFilterInit(&QueryFilter);

			int32 NumAvailable = 0;
			GetNumAvailableARPins(NumAvailable);

			int32 MaxResults = Query.MaxResults;
			// clamp to max possible to avoid unnecesarry data allocation when we do CoordinateFrames.AddZeroed(NumRequested)
			if (MaxResults < 0 || MaxResults > NumAvailable)
			{
				MaxResults = NumAvailable;
			}


			QueryFilter.target_point = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(Query.TargetPoint), WorldToMetersScale);
			QueryFilter.max_results = MaxResults;
			QueryFilter.radius_m = Query.Radius / WorldToMetersScale;
			QueryFilter.sorted = Query.bSorted;
			QueryFilter.types_mask = 0;
			for (const EMagicLeapARPinType PinType : Query.Types)
			{
				#define MASK_CASE(x) case EMagicLeapARPinType::x: { QueryFilter.types_mask |= MLPersistentCoordinateFrameType_##x; break; }
				switch (PinType)
				{
					MASK_CASE(SingleUserSingleSession)
					MASK_CASE(SingleUserMultiSession)
					MASK_CASE(MultiUserMultiSession)
				}
			}

			Pins.Reset(MaxResults);
			Pins.AddZeroed(MaxResults);
			MLCoordinateFrameUID* ArrayDataPointer = reinterpret_cast<MLCoordinateFrameUID*>(Pins.GetData());
			uint32 FoundNum = 0;

			MLResult Result = MLPersistentCoordinateFrameQuery(Tracker, &QueryFilter, ArrayDataPointer, &FoundNum);
			UE_CLOG(MLResult_Ok != Result, LogMagicLeapARPinImpl, Error, TEXT("MLPersistentCoordinateFrameQuery failed with error %s"), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			ErrorReturn = MLToUnrealPassableWorldError(Result);

			// shrink the array
			Pins.RemoveAt(FoundNum, MaxResults - FoundNum, false);
			if (FoundNum == 0 && ErrorReturn == EMagicLeapPassableWorldError::None)
			{
				ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
			}
#endif // WITH_MLSDK
		}
	}

	return ErrorReturn;
}

bool FMagicLeapARPinImplModule::GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
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
			Position = Pose.GetLocation();
			Orientation = Pose.Rotator();
			PinFoundInEnvironment = true;
			return true;
		}
		PinFoundInEnvironment = (FailReason != EMagicLeapTransformFailReason::PoseNotFound);
	}
	return false;
}


bool FMagicLeapARPinImplModule::GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	const bool bResult = GetARPinPositionAndOrientation_TrackingSpace(PinID, Position, Orientation, PinFoundInEnvironment);
	if (bResult)
	{
		const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
		Position = TrackingToWorld.TransformPosition(Position);
		Orientation = TrackingToWorld.TransformRotation(Orientation.Quaternion()).Rotator();
	}

	return bResult;
}

EMagicLeapPassableWorldError FMagicLeapARPinImplModule::GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State)
{
	EMagicLeapPassableWorldError ErrorReturn(EMagicLeapPassableWorldError::Unavailable);

#if WITH_MLSDK
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
	if (!MLPlugin.IsMagicLeapHMDValid())
	{
		ErrorReturn = EMagicLeapPassableWorldError::Unavailable;
	}
	else
	{
		ErrorReturn = CreateTracker();
		if (ErrorReturn == EMagicLeapPassableWorldError::None)
		{
			MLPersistentCoordinateFramesFrameState OutState;
			MLPersistentCoordinateFramesFrameStateInit(&OutState);
			MLResult Result = MLPersistentCoordinateFramesGetFrameState(Tracker,
				reinterpret_cast<const MLCoordinateFrameUID*>(&PinID), &OutState);
			if (Result == MLResult_Ok)
			{
				State.Confidence = OutState.confidence;
				State.RotationError = OutState.rotation_err_deg;
				State.TranslationError = OutState.translation_err_m * MLPlugin.GetWorldToMetersScale();
				State.ValidRadius = OutState.valid_radius_m * MLPlugin.GetWorldToMetersScale();
				State.PinType = MLToUnrealPinType(OutState.type);
			}
			else
			{
				UE_LOG(LogMagicLeapARPinImpl, Error, TEXT("MLPersistentCoordinateFramesGetFrameState failed with error %s"),
					UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
			}
			ErrorReturn = MLToUnrealPassableWorldError(Result);
		}
	}
#endif //WITH_MLSDK
	return ErrorReturn;
}

void FMagicLeapARPinImplModule::OnAppShutDown()
{
	DestroyTracker();
}

void FMagicLeapARPinImplModule::OnAppTick()
{
	if (!IsTrackerValid() && bCreateTracker)
	{
		if (bPerceptionEnabled)
		{
#if WITH_MLSDK
			// TODO: add retries like in Image tracker if error is LowMapQuality, UnableToLocalize, ServerUnavailable or PrivilegeDenied.
			// Retrying for PrivilegeDenied would only make sense when MLPrivilege runtime api is functional.
			MLResult Result = MLPersistentCoordinateFrameTrackerCreate(&Tracker);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapARPinImpl, Error, TEXT("Failed to create persistent coordinate frame tracker with error %s."), UTF8_TO_TCHAR(MLPersistentCoordinateFrameGetResultString(Result)));
#endif // WITH_MLSDK
		}
		else
		{
			// Tick until Perception is enabled
			return;
		}
	}

	if (IsTrackerValid())
	{
		bCreateTracker = false;

		// temporary, until we figure out why requesting PCF state too soon on app launch causes all PCFs to be cleared in ml_perception_client
		if (!bHasCompletedPrisonTime)
		{
			const double CurrentPrisonTime = FPlatformTime::Seconds();
			if (CurrentPrisonTime - PreviousTime > kPrisonSentenceSec)
			{
				bHasCompletedPrisonTime = true;
			}
			else
			{
				return;
			}
		}

		const double CurrentTime = FPlatformTime::Seconds();
		if (Settings != nullptr && (CurrentTime - PreviousTime) > static_cast<double>(Settings->UpdateCheckFrequency))
		{
			PreviousTime = CurrentTime;

			TArray<FGuid> CurrentARPins;
			if (QueryARPins(GlobalFilter, CurrentARPins) == EMagicLeapPassableWorldError::None)
			{
				for (const auto& PinAndState : OldPinsAndStates)
				{
					// Check what has been deleted
					if (CurrentARPins.Find(PinAndState.Key) == INDEX_NONE)
					{
						PendingDeleted.Add(PinAndState.Key);
					}
				}

				for (const FGuid& DeletedPinID : PendingDeleted)
				{
					OldPinsAndStates.Remove(DeletedPinID);
				}

				for (const FGuid& PinID : CurrentARPins)
				{
					FMagicLeapARPinState NewPinState;
					if (GetARPinState(PinID, NewPinState) == EMagicLeapPassableWorldError::None)
					{
						FMagicLeapARPinState* OldPinState = OldPinsAndStates.Find(PinID);
						if (OldPinState == nullptr)
						{
							// Added
							PendingAdded.Add(PinID);
							OldPinsAndStates.Add(PinID, NewPinState);
						}
						else
						{
							// check delta thresholds for updates
							const FMagicLeapARPinState& DeltaThresholds = Settings->OnUpdatedEventTriggerDelta;

							if (IsDeltaGreaterThanThreshold(OldPinState->Confidence, NewPinState.Confidence, DeltaThresholds.Confidence)
								|| IsDeltaGreaterThanThreshold(OldPinState->ValidRadius, NewPinState.ValidRadius, DeltaThresholds.ValidRadius)
								|| IsDeltaGreaterThanThreshold(OldPinState->RotationError, NewPinState.RotationError, DeltaThresholds.RotationError)
								|| IsDeltaGreaterThanThreshold(OldPinState->TranslationError, NewPinState.TranslationError, DeltaThresholds.TranslationError)
								|| OldPinState->PinType != NewPinState.PinType
							)
							{
								PendingUpdated.Add(PinID);
								*OldPinState = NewPinState;
							}
						}
					}
				}

				if (PendingAdded.Num() > 0 || PendingUpdated.Num() > 0 || PendingDeleted.Num() > 0)
				{
					BroadcastOnMagicLeapARPinUpdatedEvent(PendingAdded, PendingUpdated, PendingDeleted);
					OnMagicLeapARPinUpdatedMulti.Broadcast(PendingAdded, PendingUpdated, PendingDeleted);

					PendingAdded.Empty();
					PendingUpdated.Empty();
					PendingDeleted.Empty();
				}
			}
		}
	}
}

void FMagicLeapARPinImplModule::CreateEntityTracker()
{
	bPerceptionEnabled = true;
}

void FMagicLeapARPinImplModule::DestroyEntityTracker()
{
	bPerceptionEnabled = false;
	DestroyTracker();
}

bool FMagicLeapARPinImplModule::IsDeltaGreaterThanThreshold(float OldState, float NewState, float Threshold) const
{
	return FMath::Abs(OldState - NewState) > Threshold;
}

IMPLEMENT_MODULE(FMagicLeapARPinImplModule, MagicLeapARPinImpl);

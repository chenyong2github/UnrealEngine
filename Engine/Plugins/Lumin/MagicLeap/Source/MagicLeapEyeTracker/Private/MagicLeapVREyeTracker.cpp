// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapVREyeTracker.h"
#include "IEyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "IMagicLeapVREyeTracker.h"
#include "MagicLeapEyeTrackerTypes.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "IMagicLeapPlugin.h"
#include "UnrealEngine.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IMagicLeapPlugin.h"
#include "Lumin/CAPIShims/LuminAPIEyeTracking.h"
#include "MagicLeapCFUID.h"

#if WITH_MLSDK
EMagicLeapEyeTrackingCalibrationStatus MLToUnrealEyeCalibrationStatus(MLEyeTrackingCalibrationStatus InStatus)
{
	switch (InStatus)
	{
	case MLEyeTrackingCalibrationStatus_None:
		return EMagicLeapEyeTrackingCalibrationStatus::None;
	case MLEyeTrackingCalibrationStatus_Bad:
		return EMagicLeapEyeTrackingCalibrationStatus::Bad;
	case MLEyeTrackingCalibrationStatus_Good:
		return EMagicLeapEyeTrackingCalibrationStatus::Good;
	}
	return EMagicLeapEyeTrackingCalibrationStatus::None;
}
#endif // WITH_MLSDK

FMagicLeapVREyeTracker::FMagicLeapVREyeTracker()
: EyeTrackingStatus(EMagicLeapEyeTrackingStatus::NotConnected)
, bReadyToInit(false)
, bInitialized(false)
#if WITH_MLSDK
, EyeTrackingHandle(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
{
	IMagicLeapPlugin::Get().RegisterMagicLeapTrackerEntity(this);
	SetDefaultDataValues();
}

FMagicLeapVREyeTracker::~FMagicLeapVREyeTracker()
{
	DestroyEntityTracker();
	IMagicLeapPlugin::Get().UnregisterMagicLeapTrackerEntity(this);
}

void FMagicLeapVREyeTracker::DestroyEntityTracker()
{
#if WITH_MLSDK
	if (EyeTrackingHandle != ML_INVALID_HANDLE)
	{
		if (MLEyeTrackingDestroy(EyeTrackingHandle) != MLResult_Ok)
		{
			UE_LOG(LogCore, Warning, TEXT("   MLEyeTrackingDestroy failure"));
		}
		EyeTrackingHandle = ML_INVALID_HANDLE;
	}
#endif //WITH_MLSDK
	bInitialized = false;
}

void FMagicLeapVREyeTracker::SetDefaultDataValues()
{
	FMemory::Memzero(UnfilteredEyeTrackingData);
#if WITH_MLSDK
	FMemory::Memzero(EyeTrackingStaticData);
#endif //WITH_MLSDK
}


void FMagicLeapVREyeTracker::SetActivePlayerController(APlayerController* NewActivePlayerController)
{
	if (NewActivePlayerController && NewActivePlayerController->IsValidLowLevel() && NewActivePlayerController != ActivePlayerController)
	{
		ActivePlayerController = NewActivePlayerController;
	}
}

bool FMagicLeapVREyeTracker::Tick(float DeltaTime)
{
	bool bSuccess = true;
#if WITH_MLSDK
	//assume we're in a bad state
	UnfilteredEyeTrackingData.bIsStable = false;

	if (MLHandleIsValid(EyeTrackingHandle))
	{
		//check stat first to make sure everything is valid
		MLEyeTrackingState TempTrackingState;
		FMemory::Memzero(TempTrackingState);
		bSuccess = bSuccess && (MLEyeTrackingGetState(EyeTrackingHandle, &TempTrackingState) == MLResult_Ok);

		//make sure this is valid eye tracking data!
		if (bSuccess
			&& (TempTrackingState.error == MLEyeTrackingError_None) 
			&& (TempTrackingState.fixation_confidence > 0.0f)
			&& (TempTrackingState.left_center_confidence > 0.0f)
			&& (TempTrackingState.right_center_confidence > 0.0f))
		{
			EyeTrackingStatus = EMagicLeapEyeTrackingStatus::UserPresentAndWatchingWindow;
			EyeCalibrationStatus = MLToUnrealEyeCalibrationStatus(TempTrackingState.calibration_status);

			UnfilteredEyeTrackingData.bIsStable = true;
			FDateTime Now = FDateTime::UtcNow();
			UnfilteredEyeTrackingData.TimeStamp = Now;

			IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::Get();
			if (MLPlugin.IsPerceptionEnabled())
			{
				const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);

				//harvest the 3 transforms
				EMagicLeapTransformFailReason FailReason;
				FTransform FixationTransform;
				if (MLPlugin.GetTransform(EyeTrackingStaticData.fixation, FixationTransform, FailReason))
				{
					FixationTransform = FixationTransform * TrackingToWorld;
					//get focal point
					UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint = FixationTransform.GetTranslation();
				}

				bool bLeftTransformValid = false;
				FTransform LeftCenterTransform;
				if (MLPlugin.GetTransform(EyeTrackingStaticData.left_center, LeftCenterTransform, FailReason))
				{
					LeftCenterTransform = LeftCenterTransform * TrackingToWorld;
					bLeftTransformValid = true;
				}

				bool bRightTransformValid = false;
				FTransform RightCenterTransform;
				if (MLPlugin.GetTransform(EyeTrackingStaticData.right_center, RightCenterTransform, FailReason))
				{
					RightCenterTransform = RightCenterTransform * TrackingToWorld;
					bRightTransformValid = true;
				}

				if (bLeftTransformValid && bRightTransformValid)
				{
					//average the left and right eye
					UnfilteredEyeTrackingData.AverageGazeOrigin = (LeftCenterTransform.GetLocation() + RightCenterTransform.GetLocation()) * .5f;
					//get the gaze vector (Point-Eye)
					UnfilteredEyeTrackingData.AverageGazeRay = (UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint - UnfilteredEyeTrackingData.AverageGazeOrigin);
					UnfilteredEyeTrackingData.AverageGazeRay.Normalize();

					UnfilteredEyeTrackingData.LeftOriginPoint = LeftCenterTransform.GetLocation();
					UnfilteredEyeTrackingData.RightOriginPoint = RightCenterTransform.GetLocation();
				}

				UnfilteredEyeTrackingData.Confidence = TempTrackingState.fixation_confidence;

				UnfilteredEyeTrackingData.bLeftBlink = TempTrackingState.left_blink;
				UnfilteredEyeTrackingData.bRightBlink = TempTrackingState.right_blink;

				UnfilteredEyeTrackingData.FixationDepthIsUncomfortable = TempTrackingState.fixation_depth_is_uncomfortable;
				UnfilteredEyeTrackingData.FixationDepthViolationHasOccurred = TempTrackingState.fixation_depth_violation_has_occurred;
				UnfilteredEyeTrackingData.RemainingTimeAtUncomfortableDepth = TempTrackingState.remaining_time_at_uncomfortable_depth;

				//UE_LOG(LogCore, Warning, TEXT("   SUCCESS -> "));
				//UE_LOG(LogCore, Warning, TEXT("        Gaze Origin %f, %f, %f"), UnfilteredEyeTrackingData.AverageGazeOrigin.X, UnfilteredEyeTrackingData.AverageGazeOrigin.Y, UnfilteredEyeTrackingData.AverageGazeOrigin.Z);
				//UE_LOG(LogCore, Warning, TEXT("        Converge    %f, %f, %f"), UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint.X, UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint.Y, UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint.Z);
				//UE_LOG(LogCore, Warning, TEXT("        Gaze Ray    %f, %f, %f"), UnfilteredEyeTrackingData.AverageGazeRay.X, UnfilteredEyeTrackingData.AverageGazeRay.Y, UnfilteredEyeTrackingData.AverageGazeRay.Z);
			}
		}
		else
		{
			EyeTrackingStatus = EMagicLeapEyeTrackingStatus::UserNotPresent;
		}
	}
	else
	{
		if (!bInitialized && IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
		{
			if (IMagicLeapPlugin::Get().IsPerceptionEnabled())
			{
				//keep trying until we are successful in creating one
				MLResult CreateResult = MLResult_UnspecifiedFailure;
				CreateResult = MLEyeTrackingCreate(&EyeTrackingHandle);
				bInitialized = CreateResult == MLResult_Ok && MLHandleIsValid(EyeTrackingHandle);
				if (bInitialized)
				{
					//UE_LOG(LogCore, Log, TEXT("   VR Eye Tracker Created"));
					// Needs to be called only once.
					if (MLEyeTrackingGetStaticData(EyeTrackingHandle, &EyeTrackingStaticData) != MLResult_Ok)
					{
						UE_LOG(LogCore, Warning, TEXT("   Unable to get Eye Tracker Static Data"));
					}
				}
			}
		}
	}
#endif //WITH_MLSDK
	return true;
}

void FMagicLeapVREyeTracker::DrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	DrawDebugSphere(HUD->GetWorld(), UnfilteredEyeTrackingData.WorldAverageGazeConvergencePoint, 20.0f, 16, UnfilteredEyeTrackingData.bIsStable ? FColor::Green : FColor::Red);
}

const FMagicLeapVREyeTrackingData& FMagicLeapVREyeTracker::GetVREyeTrackingData()
{
	bReadyToInit = true;
	return UnfilteredEyeTrackingData;
}

EMagicLeapEyeTrackingStatus FMagicLeapVREyeTracker::GetEyeTrackingStatus()
{
	bReadyToInit = true;
	return EyeTrackingStatus;
}

EMagicLeapEyeTrackingCalibrationStatus FMagicLeapVREyeTracker::GetCalibrationStatus() const
{
	return EyeCalibrationStatus;
}
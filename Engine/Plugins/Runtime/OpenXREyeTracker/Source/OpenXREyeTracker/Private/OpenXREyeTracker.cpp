// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXREyeTracker.h"
#include "IXRTrackingSystem.h"
#include "OpenXRCore.h"
#include "UObject/UObjectIterator.h"
#include "IOpenXRExtensionPlugin.h"
#include "Modules/ModuleManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#endif

IMPLEMENT_MODULE(FOpenXREyeTrackerModule, OpenXREyeTracker);

static TAutoConsoleVariable<int32> CVarEnableOpenXREyetrackingDebug(TEXT("OpenXR.debug.EnableEyetrackingDebug"), 1, TEXT("0 - Eyetracking debug visualizations are disabled. 1 - Eyetracking debug visualizations are enabled."));

FOpenXREyeTracker::FOpenXREyeTracker()
{
	RegisterOpenXRExtensionModularFeature();
}

FOpenXREyeTracker::~FOpenXREyeTracker()
{
	Destroy();
}

void FOpenXREyeTracker::Destroy()
{
}

bool FOpenXREyeTracker::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	TArray<XrApiLayerProperties> Properties;
	uint32_t Count = 0;
	XR_ENSURE(xrEnumerateApiLayerProperties(0, &Count, nullptr));
	Properties.SetNum(Count);
	for (auto& Prop : Properties)
	{
		Prop = XrApiLayerProperties{ XR_TYPE_API_LAYER_PROPERTIES };
	}
	XR_ENSURE(xrEnumerateApiLayerProperties(Count, &Count, Properties.GetData()));

	// Some API layers can crash the loader when enabled, if they're present we shouldn't enable the extension
	for (const XrApiLayerProperties& Layer : Properties)
	{
		if (FCStringAnsi::Strstr(Layer.layerName, "XR_APILAYER_VIVE_eye_tracking") &&
			Layer.layerVersion <= 1)
		{
			return false;
		}
	}

	OutExtensions.Add("XR_EXT_eye_gaze_interaction");
	return true;
}

bool FOpenXREyeTracker::GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics)
{
	OutKeyPrefix = "EyeTracker";
	OutHasHaptics = false;
	return xrStringToPath(InInstance, "/interaction_profiles/ext/eye_gaze_interaction", &OutPath) == XR_SUCCESS;
}

void FOpenXREyeTracker::AddActions(XrInstance Instance, TFunction<XrAction(XrActionType InActionType, const FName& InName, const TArray<XrPath>& InSubactionPaths)> AddAction)
{
	TArray<XrPath> SubactionPaths;

	EyeTrackerAction = AddAction(XR_ACTION_TYPE_POSE_INPUT, "eye_tracker", SubactionPaths);
	if (EyeTrackerAction == XR_NULL_HANDLE)
	{
		return;
	}

	// Create suggested bindings
	XrPath EyeGazeInteractionProfilePath = XR_NULL_PATH;
	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/ext/eye_gaze_interaction", &EyeGazeInteractionProfilePath));

	XrPath GazePosePath = XR_NULL_PATH;
	XR_ENSURE(xrStringToPath(Instance, "/user/eyes_ext/input/gaze_ext/pose", &GazePosePath));

	XrActionSuggestedBinding Bindings;
	Bindings.action = EyeTrackerAction;
	Bindings.binding = GazePosePath;

	XrInteractionProfileSuggestedBinding SuggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	SuggestedBindings.interactionProfile = EyeGazeInteractionProfilePath;
	SuggestedBindings.suggestedBindings = &Bindings;
	SuggestedBindings.countSuggestedBindings = 1;
	XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &SuggestedBindings));

}

const void* FOpenXREyeTracker::OnBeginSession(XrSession InSession, const void* InNext)
{
	static FName SystemName(TEXT("OpenXR"));
	if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
	{
		XRTrackingSystem = GEngine->XRSystem.Get();
	}

	XrActionSpaceCreateInfo CreateActionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
	CreateActionSpaceInfo.action = EyeTrackerAction;
	CreateActionSpaceInfo.poseInActionSpace = ToXrPose(FTransform::Identity);
	XR_ENSURE(xrCreateActionSpace(InSession, &CreateActionSpaceInfo, &GazeActionSpace));

	SyncInfo.countActiveActionSets = 0;
	SyncInfo.activeActionSets = XR_NULL_HANDLE;

	bSessionStarted = true;

	return InNext;
}


void FOpenXREyeTracker::PostSyncActions(XrSession InSession)
{
	XrActionStateGetInfo GetActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	GetActionStateInfo.action = EyeTrackerAction;
	XR_ENSURE(xrGetActionStatePose(InSession, &GetActionStateInfo, &ActionStatePose));
}

void FOpenXREyeTracker::UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
{
	if (ActionStatePose.isActive) {
		XR_ENSURE(xrLocateSpace(GazeActionSpace, TrackingSpace, DisplayTime, &EyeTrackerSpaceLocation));
	}
}

bool FOpenXREyeTracker::GetEyeTrackerGazeData(FEyeTrackerGazeData& OutGazeData) const
{
	if (!bSessionStarted)
	{
		OutGazeData = FEyeTrackerGazeData();
		return false;
	}

	const XrSpaceLocationFlags ValidFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	const XrSpaceLocationFlags TrackedFlags = XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

	if ((EyeTrackerSpaceLocation.locationFlags & ValidFlags) != ValidFlags)
	{
		// Either Orientation or position are invalid
		OutGazeData = FEyeTrackerGazeData();
		return false;
	}
	else if ((EyeTrackerSpaceLocation.locationFlags & TrackedFlags) != TrackedFlags)
	{
		// Orientation and/or position are old or an estimate of some kind, confidence is low.
		OutGazeData.ConfidenceValue = 0.0f;
	}
	else
	{
		// Both orientation and position are fully tracked now, confidence is high.
		OutGazeData.ConfidenceValue = 1.0f;
	}

	const float WorldToMetersScale = XRTrackingSystem->GetWorldToMetersScale();
	const XrPosef& Pose = EyeTrackerSpaceLocation.pose;
	const FTransform EyeTrackerTransform = ToFTransform(Pose, WorldToMetersScale);

	const FTransform& TrackingToWoldTransform = XRTrackingSystem->GetTrackingToWorldTransform();
	const FTransform EyeTransform = EyeTrackerTransform * TrackingToWoldTransform;

	OutGazeData.GazeDirection = EyeTransform.TransformVector(FVector::ForwardVector);
	OutGazeData.GazeOrigin = EyeTransform.GetLocation();
	OutGazeData.FixationPoint = FVector::ZeroVector; //not supported

	return true;
}

bool FOpenXREyeTracker::GetEyeTrackerStereoGazeData(FEyeTrackerStereoGazeData& OutStereoGazeData) const
{
	OutStereoGazeData = FEyeTrackerStereoGazeData();
	return false;
}

EEyeTrackerStatus FOpenXREyeTracker::GetEyeTrackerStatus() const
{
	if (!bSessionStarted)
	{
		return EEyeTrackerStatus::NotConnected;
	}

	const XrSpaceLocationFlags ValidFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	const XrSpaceLocationFlags TrackedFlags = XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

	if ((EyeTrackerSpaceLocation.locationFlags & ValidFlags) != ValidFlags)
	{
		return EEyeTrackerStatus::NotTracking;
	}

	if ((EyeTrackerSpaceLocation.locationFlags & TrackedFlags) != TrackedFlags)
	{
		return EEyeTrackerStatus::NotTracking;
	}
	else
	{
		return EEyeTrackerStatus::Tracking;
	}
}

bool FOpenXREyeTracker::IsStereoGazeDataAvailable() const
{
	return false;
}

void FOpenXREyeTracker::DrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	if (!bSessionStarted)
	{
		return;
	}

	const XrSpaceLocationFlags ValidFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	const XrSpaceLocationFlags TrackedFlags = XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

	if ((EyeTrackerSpaceLocation.locationFlags & ValidFlags) != ValidFlags)
	{
		return;
	}

	FColor DrawColor = FColor::Yellow;
	if ((EyeTrackerSpaceLocation.locationFlags & TrackedFlags) == TrackedFlags)
	{
		DrawColor = FColor::Green;
	}

	const float WorldToMetersScale = XRTrackingSystem->GetWorldToMetersScale();
	const XrPosef& Pose = EyeTrackerSpaceLocation.pose;
	FTransform EyeTrackerTransform = ToFTransform(Pose, WorldToMetersScale);

	FVector GazeDirection = EyeTrackerTransform.TransformVector(FVector::ForwardVector);
	FVector GazeOrigin = EyeTrackerTransform.GetLocation();
	FVector DebugPos = GazeOrigin + (GazeDirection * 100.0f);
	DrawDebugSphere(HUD->GetWorld(), DebugPos, 20.0f, 16, DrawColor);
}

/************************************************************************/
/* FOpenXREyeTrackerModule                                           */
/************************************************************************/

FOpenXREyeTrackerModule::FOpenXREyeTrackerModule()
{
}

void FOpenXREyeTrackerModule::StartupModule()
{
	IEyeTrackerModule::StartupModule();
	EyeTracker = TSharedPtr<FOpenXREyeTracker, ESPMode::ThreadSafe>(new FOpenXREyeTracker());
	OnDrawDebugHandle = AHUD::OnShowDebugInfo.AddRaw(this, &FOpenXREyeTrackerModule::OnDrawDebug);
}

void FOpenXREyeTrackerModule::ShutdownModule()
{
	AHUD::OnShowDebugInfo.Remove(OnDrawDebugHandle);
}

TSharedPtr<class IEyeTracker, ESPMode::ThreadSafe> FOpenXREyeTrackerModule::CreateEyeTracker()
{
	return EyeTracker;
}

void FOpenXREyeTrackerModule::OnDrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	if (CVarEnableOpenXREyetrackingDebug.GetValueOnGameThread())
	{
		if (EyeTracker.IsValid())
		{
			EyeTracker->DrawDebug(HUD, Canvas, DisplayInfo, YL, YPos);
		}
	}
}

bool FOpenXREyeTrackerModule::IsEyeTrackerConnected() const
{
	if (EyeTracker.IsValid())
	{
		EEyeTrackerStatus Status = EyeTracker->GetEyeTrackerStatus();
		if ((Status != EEyeTrackerStatus::NotTracking) && (Status != EEyeTrackerStatus::NotConnected))
		{
			return true;
		}
	}

	return false;
}
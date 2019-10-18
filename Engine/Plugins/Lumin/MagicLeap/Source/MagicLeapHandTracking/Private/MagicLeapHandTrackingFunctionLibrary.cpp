// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHandTrackingFunctionLibrary.h"
#include "IMagicLeapHandTrackingPlugin.h"
#include "MagicLeapHandTracking.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/World.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"

bool GetHandTrackingData(EControllerHand Hand, const FMagicLeapHandTracking::FHandState*& HandTrackingData)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());

	if (HandTracking.IsValid() && HandTracking->IsHandTrackingStateValid())
	{
		switch (Hand)
		{
		case EControllerHand::Left:
		{
			HandTrackingData = &HandTracking->GetLeftHandState();
		}
		break;

		case EControllerHand::Right:
		{
			HandTrackingData = &HandTracking->GetRightHandState();
		}
		break;

		case EControllerHand::AnyHand:
		{
			HandTrackingData = &HandTracking->GetLeftHandState();
			if (!HandTrackingData->IsValid() ||
				(HandTracking->GetRightHandState().IsValid() &&
				(HandTracking->GetRightHandState().GestureConfidence > HandTracking->GetLeftHandState().GestureConfidence)))
			{
				HandTrackingData = &HandTracking->GetRightHandState();
			}
		}
		break;

		default:
		{
			UE_LOG(LogMagicLeapHandTracking, Error, TEXT("Hand %d is not supported"), static_cast<int32>(Hand));
		}
		}
	}

	return HandTrackingData && HandTrackingData->IsValid();
}

bool UMagicLeapHandTrackingFunctionLibrary::GetHandCenter(EControllerHand Hand, FTransform& HandCenter)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		HandCenter = HandTrackingData->HandCenter.Transform;
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetHandIndexFingerTip(EControllerHand Hand, EMagicLeapGestureTransformSpace TransformSpace, FTransform& Pointer)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		switch (TransformSpace)
		{
		case EMagicLeapGestureTransformSpace::Tracking:
		{
			Pointer = HandTrackingData->IndexFinger.Tip.Transform;
			break;
		}
		case EMagicLeapGestureTransformSpace::World:
		{
			const FTransform TrackingToWorldTransform = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
			Pointer = HandTrackingData->IndexFinger.Tip.Transform * TrackingToWorldTransform;
			break;
		}
		case EMagicLeapGestureTransformSpace::Hand:
		{
			Pointer = HandTrackingData->IndexFinger.Tip.Transform * HandTrackingData->HandCenter.Transform.Inverse();
			break;
		}
		default:
			check(false);
			return false;
		}
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetHandThumbTip(EControllerHand Hand, EMagicLeapGestureTransformSpace TransformSpace, FTransform& Secondary)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		switch (TransformSpace)
		{
		case EMagicLeapGestureTransformSpace::Tracking:
		{
			Secondary = HandTrackingData->Thumb.Tip.Transform;
			break;
		}
		case EMagicLeapGestureTransformSpace::World:
		{
			const FTransform TrackingToWorldTransform = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
			Secondary = HandTrackingData->Thumb.Tip.Transform * TrackingToWorldTransform;
			break;
		}
		case EMagicLeapGestureTransformSpace::Hand:
		{
			Secondary = HandTrackingData->Thumb.Tip.Transform * HandTrackingData->HandCenter.Transform.Inverse();
			break;
		}
		default:
			check(false);
			return false;
		}
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetHandCenterNormalized(EControllerHand Hand, FVector& HandCenterNormalized)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		HandCenterNormalized = HandTrackingData->HandCenterNormalized;
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetGestureKeypoints(EControllerHand Hand, TArray<FTransform>& Keypoints)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		Keypoints.SetNum(3);
		Keypoints[0] = HandTrackingData->HandCenter.Transform;
		Keypoints[1] = HandTrackingData->IndexFinger.Tip.Transform;
		Keypoints[2] = HandTrackingData->Thumb.Tip.Transform;
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetGestureKeypointTransform(EControllerHand Hand, EMagicLeapHandTrackingKeypoint Keypoint, EMagicLeapGestureTransformSpace TransformSpace, FTransform& OutTransform)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());

	if (HandTracking.IsValid() && HandTracking->IsHandTrackingStateValid())
	{
		FTransform KeyPointTransform;
		const bool bSuccess = HandTracking->GetKeypointTransform(Hand, Keypoint, KeyPointTransform);

		if (bSuccess)
		{
			switch (TransformSpace)
			{
			case EMagicLeapGestureTransformSpace::Tracking:
			{
				OutTransform = KeyPointTransform;
				return true;
			}
			case EMagicLeapGestureTransformSpace::World:
			{
				const FTransform TrackingToWorldTransform = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
				OutTransform = KeyPointTransform * TrackingToWorldTransform;
				return true;
			}
			case EMagicLeapGestureTransformSpace::Hand:
			{
				FTransform HandTransform;
				const bool bSuccess2 = HandTracking->GetKeypointTransform(Hand, EMagicLeapHandTrackingKeypoint::Hand_Center, HandTransform);
				if (bSuccess2)
				{
					OutTransform = KeyPointTransform * HandTransform.Inverse();
					return true;
				}
				break;
			}
			default:
				check(false);
				return false;
			}
		}
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::SetConfiguration(const TArray<EMagicLeapHandTrackingGesture>& StaticGesturesToActivate, EMagicLeapHandTrackingKeypointFilterLevel KeypointsFilterLevel, EMagicLeapHandTrackingGestureFilterLevel GestureFilterLevel, bool bEnabled)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());
	return HandTracking.IsValid() && HandTracking->SetConfiguration(bEnabled, StaticGesturesToActivate, KeypointsFilterLevel, GestureFilterLevel);
}

bool UMagicLeapHandTrackingFunctionLibrary::GetConfiguration(TArray<EMagicLeapHandTrackingGesture>& ActiveStaticGestures, EMagicLeapHandTrackingKeypointFilterLevel& KeypointsFilterLevel, EMagicLeapHandTrackingGestureFilterLevel& GestureFilterLevel, bool& bEnabled)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());
	return HandTracking.IsValid() && HandTracking->GetConfiguration(bEnabled, ActiveStaticGestures, KeypointsFilterLevel, GestureFilterLevel);
}

void UMagicLeapHandTrackingFunctionLibrary::SetStaticGestureConfidenceThreshold(EMagicLeapHandTrackingGesture Gesture, float Confidence)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());
	if (HandTracking.IsValid())
	{
		HandTracking->SetGestureConfidenceThreshold(Gesture, Confidence);
	}
}

float UMagicLeapHandTrackingFunctionLibrary::GetStaticGestureConfidenceThreshold(EMagicLeapHandTrackingGesture Gesture)
{
	TSharedPtr<FMagicLeapHandTracking> HandTracking = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetInputDevice());
	return (HandTracking.IsValid()) ? HandTracking->GetGestureConfidenceThreshold(Gesture) : 0.0f;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetCurrentGestureConfidence(EControllerHand Hand, float& Confidence)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		Confidence = HandTrackingData->GestureConfidence;
		return true;
	}

	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetCurrentGesture(EControllerHand Hand, EMagicLeapHandTrackingGesture& Gesture)
{
	const FMagicLeapHandTracking::FHandState* HandTrackingData = nullptr;

	if (GetHandTrackingData(Hand, HandTrackingData))
	{
		Gesture = HandTrackingData->Gesture;
		return true;
	}

	Gesture = EMagicLeapHandTrackingGesture::NoHand;
	return false;
}

bool UMagicLeapHandTrackingFunctionLibrary::GetMagicLeapHandTrackingLiveLinkSource(FLiveLinkSourceHandle& SourceHandle)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		TSharedPtr<ILiveLinkSource> HandTrackingSource = StaticCastSharedPtr<FMagicLeapHandTracking>(IMagicLeapHandTrackingPlugin::Get().GetLiveLinkSource());
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient->AddSource(HandTrackingSource);
		SourceHandle.SetSourcePointer(HandTrackingSource);
		return true;
	}
	else
	{
		SourceHandle.SetSourcePointer(nullptr);
		return false;
	}
};

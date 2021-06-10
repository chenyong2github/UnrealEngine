// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensARSystem.h"

#include "HoloLensModule.h"
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityInteropUtility.h"


UWMRARPin* FHoloLensARSystem::WMRCreateNamedARPin(FName Name, const FTransform& WorldTransform)
{
	if (TrackingQuality == EARTrackingQuality::NotTracking)
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("Must have good tracking to create Named Pin"));
		return nullptr;
	}

	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

	// PinToWorld * AlignedTrackingToWorld(-1) * TrackingToAlignedTracking(-1) = PinToWorld * WorldToAlignedTracking * AlignedTrackingToTracking
	// The Worlds and AlignedTracking cancel out, and we get PinToTracking
	// But we must translate this logic into Unreal's transform API
	const FTransform& TrackingToAlignedTracking = ARSupportInterface->GetAlignmentTransform();
	const FTransform PinToTrackingTransform = WorldTransform.GetRelativeTransform(TrackingSystem->GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);

	FString WMRAnchorId = Name.ToString().ToLower();

	if (AnchorIdToPinMap.Contains(Name))
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("CreateWMRAnchorStoreARPin: Creation of Anchor %s failed because that anchorID is already in use!  No pin created."), *WMRAnchorId);
		return nullptr;
	}

	{
		bool bSuccess = WMRCreateAnchor(*WMRAnchorId, PinToTrackingTransform.GetLocation(), PinToTrackingTransform.GetRotation());
		if (!bSuccess)
		{
			UE_LOG(LogHoloLensAR, Warning, TEXT("CreateWMRAnchorStoreARPin: Creation of Anchor %s failed!  No anchor or pin created."), *WMRAnchorId);
			return nullptr;
		}
	}

	UWMRARPin* NewPin = NewObject<UWMRARPin>();
	NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, PinToTrackingTransform, nullptr, Name);

	AnchorIdToPinMap.Add(Name, NewPin);
	NewPin->SetAnchorId(Name);

	Pins.Add(NewPin);

	return NewPin;
}

UWMRARPin* FHoloLensARSystem::WMRCreateNamedARPinAroundAnchor(FName Name, FString AnchorId)
{
	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

	FTransform Transform;
	const bool bTracked = WMRGetAnchorTransform(*AnchorId.ToLower(), Transform);

	UWMRARPin* NewPin = NewObject<UWMRARPin>();
	NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, Transform, nullptr, Name);

	AnchorIdToPinMap.Add(Name, NewPin);
	NewPin->SetAnchorId(Name);

	Pins.Add(NewPin);

	NewPin->OnTrackingStateChanged(bTracked ? EARTrackingState::Tracking : EARTrackingState::NotTracking);

	return NewPin;
}

TArray<UWMRARPin*> FHoloLensARSystem::WMRLoadWMRAnchorStoreARPins()
{
	TArray<FName> AnchorIds;
	bool Success = WMRLoadAnchors([&AnchorIds](const wchar_t* SaveId, const wchar_t* AnchorId) { AnchorIds.Add(FName(AnchorId)); });

	TArray<UWMRARPin*> LoadedPins;
	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();
	for (FName& AnchorId : AnchorIds)
	{
		FTransform Transform;
		const bool bTracked = WMRGetAnchorTransform(*AnchorId.ToString().ToLower(), Transform);

		UWMRARPin* NewPin = NewObject<UWMRARPin>();
		NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, FTransform::Identity, nullptr, AnchorId);

		AnchorIdToPinMap.Add(AnchorId, NewPin);
		NewPin->SetAnchorId(AnchorId);
		NewPin->SetIsInAnchorStore(true);

		Pins.Add(NewPin);
		LoadedPins.Add(NewPin);

		NewPin->OnTrackingStateChanged(bTracked ? EARTrackingState::Tracking : EARTrackingState::NotTracking);
	}

	return LoadedPins;
}

bool FHoloLensARSystem::WMRSaveARPinToAnchorStore(UARPin* InPin)
{
	UWMRARPin* WMRPin = Cast<UWMRARPin>(InPin);
	check(WMRPin);
	
	if (WMRPin->GetIsInAnchorStore())
	{
		return true;
	}
	else
	{
		const FString& AnchorId = WMRPin->GetAnchorId();
		FString SaveId = AnchorId.ToLower();
		bool Saved = WMRSaveAnchor(*SaveId.ToLower(), *SaveId.ToLower());
		WMRPin->SetIsInAnchorStore(Saved);
		return Saved;
	}

}

void FHoloLensARSystem::WMRRemoveARPinFromAnchorStore(UARPin* InPin)
{
	UWMRARPin* WMRPin = Cast<UWMRARPin>(InPin);
	check(WMRPin);

	WMRPin->SetIsInAnchorStore(false);
	const FName& AnchorId = WMRPin->GetAnchorIdName();
	if (AnchorId.IsValid())
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("RemoveARPinFromAnchorStore: ARPin %s has already been removed as a runtime pin, which means its WMR anchorID is not longer valid and it cannot be removed from the store.  You must remove the pin from the anchor store *before* removing the runtime ARPin. RemoveAllARPinsFromWMRAnchorStore will also remove stored anchors that have been orphaned in this way."), *WMRPin->GetDebugName().ToString());
		return;
	}
	// Force save identifier to lowercase because FName case is not guaranteed to be the same across multiple UE4 sessions.
	WMRRemoveSavedAnchor(*AnchorId.ToString().ToLower());
}

// These functions operate in WMR Tracking Space but UE4 units (so we will deal with worldscale here).

bool FHoloLensARSystem::WMRCreateAnchor(const wchar_t* AnchorId, FVector InPosition, FQuat InRotationQuat)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		const float WorldScale = 1.0 / TrackingSystem->GetWorldToMetersScale();
		DirectX::XMFLOAT3 Position = WindowsMixedReality::WMRUtility::ToMixedRealityVector(InPosition * WorldScale);
		DirectX::XMFLOAT4 RotationQuat = WindowsMixedReality::WMRUtility::ToMixedRealityQuaternion(InRotationQuat);
		return WMRInterop->CreateAnchor(AnchorId, Position, RotationQuat);
	}
#endif

	return false;
}

void FHoloLensARSystem::WMRRemoveAnchor(const wchar_t* AnchorId)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		WMRInterop->RemoveAnchor(AnchorId);
	}
#endif
}

bool FHoloLensARSystem::WMRDoesAnchorExist(const wchar_t* AnchorId) const
{
#if WITH_WINDOWS_MIXED_REALITY
	return WMRInterop && WMRInterop->DoesAnchorExist(AnchorId);
#else
	return false;
#endif
}

bool FHoloLensARSystem::WMRGetAnchorTransform(const wchar_t* AnchorId, FTransform& Transform) const
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		DirectX::XMFLOAT3 scale;
		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 trans;
		if (WMRInterop->GetAnchorPose(AnchorId, scale, rot, trans))
		{
			const float WorldScale = TrackingSystem->GetWorldToMetersScale();
			FVector Translation = WindowsMixedReality::WMRUtility::FromMixedRealityVector(trans);
			Transform.SetLocation(Translation * WorldScale);
			Transform.SetRotation(WindowsMixedReality::WMRUtility::FromMixedRealityQuaternion(rot));
			Transform.SetScale3D(WindowsMixedReality::WMRUtility::FromMixedRealityScale(scale));
			return true;
		}
		else
		{
			return false;
		}
	}
#endif

	return false;
}

bool FHoloLensARSystem::WMRSaveAnchor(const wchar_t* saveId, const wchar_t* anchorId)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		return WMRInterop->SaveAnchor(saveId, anchorId);
	}
#endif

	return false;
}

void FHoloLensARSystem::WMRRemoveSavedAnchor(const wchar_t* anchorId)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		WMRInterop->RemoveSavedAnchor(anchorId);
	}
#endif
}

bool FHoloLensARSystem::WMRLoadAnchors(std::function<void(const wchar_t* saveId, const wchar_t* anchorId)> anchorIdWritingFunctionPointer)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		return WMRInterop->LoadAnchors(anchorIdWritingFunctionPointer);
	}
#endif

	return false;
}

void FHoloLensARSystem::WMRClearSavedAnchors()
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		WMRInterop->ClearSavedAnchors();
	}
#endif
}

void FHoloLensARSystem::OnSpawnARActor(AARActor* NewARActor, UARComponent* NewARComponent, FGuid NativeID)
{
	FTrackedGeometryGroup* TrackedGeometryGroup = TrackedGeometryGroups.Find(NativeID);
	if (TrackedGeometryGroup != nullptr)
	{
		//this should still be null
		check(TrackedGeometryGroup->ARActor == nullptr);
		check(TrackedGeometryGroup->ARComponent == nullptr);

		check(NewARActor);
		check(NewARComponent);

		TrackedGeometryGroup->ARActor = NewARActor;
		TrackedGeometryGroup->ARComponent = NewARComponent;

		//NOW, we can make the callbacks
		TrackedGeometryGroup->ARComponent->Update(TrackedGeometryGroup->TrackedGeometry);
		TriggerOnTrackableAddedDelegates(TrackedGeometryGroup->TrackedGeometry);
	}
	else
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("AR NativeID not found.  Make sure to set this on the ARComponent!"));
	}
}

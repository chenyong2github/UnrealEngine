// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HoloLensARSystem.h"

#include "HoloLensModule.h"
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityInteropUtility.h"


UWMRARPin* FHoloLensARSystem::CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform)
{
	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

	// PinToWorld * AlignedTrackingToWorld(-1) * TrackingToAlignedTracking(-1) = PinToWorld * WorldToAlignedTracking * AlignedTrackingToTracking
	// The Worlds and AlignedTracking cancel out, and we get PinToTracking
	// But we must translate this logic into Unreal's transform API
	const FTransform& TrackingToAlignedTracking = ARSupportInterface->GetAlignmentTransform();
	const FTransform PinToTrackingTransform = PinToWorldTransform.GetRelativeTransform(TrackingSystem->GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);

	FString WMRAnchorId = Name.ToString();

	if (AnchorIdToPinMap.Contains(WMRAnchorId))
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

	AnchorIdToPinMap.Add(WMRAnchorId, NewPin);
	NewPin->SetAnchorId(WMRAnchorId);

	Pins.Add(NewPin);

	return NewPin;
}

bool FHoloLensARSystem::PinComponentToARPin(USceneComponent* ComponentToPin, UWMRARPin* Pin)
{
	if (Pin == nullptr)
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("PinComponentToWMRAnchorStoreARPin: Pin was null.  Doing nothing."));
		return false;
	}
	if (ComponentToPin == nullptr)
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("PinComponentToWMRAnchorStoreARPin: Tried to pin null component to pin %s.  Doing nothing."), *Pin->GetDebugName().ToString());
		return false;
	}

	{
		if (UWMRARPin* FindResult = FindPinByComponent(ComponentToPin))
		{
			if (FindResult == Pin)
			{
				UE_LOG(LogHoloLensAR, Warning, TEXT("PinComponentToWMRAnchorStoreARPin: Component %s is already pinned to pin %s.  Doing nothing."), *ComponentToPin->GetReadableName(), *Pin->GetDebugName().ToString());
				return true;
			}
			else
			{
				UE_LOG(LogHoloLensAR, Warning, TEXT("PinComponentToWMRAnchorStoreARPin: Component %s is pinned to pin %s. Unpinning it from that pin first.  The pin will not be destroyed."), *ComponentToPin->GetReadableName(), *Pin->GetDebugName().ToString());
				FindResult->SetPinnedComponent(nullptr);
			}
		}

		Pin->SetPinnedComponent(ComponentToPin);

		return true;
	}

	return false;
}

bool FHoloLensARSystem::IsWMRAnchorStoreReady() const
{
	return WMRIsSpatialAnchorStoreLoaded();
}

TArray<UWMRARPin*> FHoloLensARSystem::LoadWMRAnchorStoreARPins()
{
	TArray<FString> AnchorIds;
	bool Success = WMRLoadAnchors([&AnchorIds](const wchar_t* AnchorId) { AnchorIds.Add(FString(AnchorId)); });

	TArray<UWMRARPin*> LoadedPins;
	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();
	for (FString& AnchorId : AnchorIds)
	{
		FTransform Transform;
		const bool bTracked = WMRGetAnchorTransform(*AnchorId, Transform);

		UWMRARPin* NewPin = NewObject<UWMRARPin>();
		NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, Transform, nullptr, FName(*AnchorId));

		AnchorIdToPinMap.Add(AnchorId, NewPin);
		NewPin->SetAnchorId(AnchorId);
		NewPin->SetIsInAnchorStore(true);

		Pins.Add(NewPin);
		LoadedPins.Add(NewPin);

		NewPin->OnTrackingStateChanged(bTracked ? EARTrackingState::Tracking : EARTrackingState::NotTracking);
	}

	return LoadedPins;
}

bool FHoloLensARSystem::SaveARPinToAnchorStore(UARPin* InPin)
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
		bool Saved = WMRSaveAnchor(*AnchorId);
		WMRPin->SetIsInAnchorStore(Saved);
		return Saved;
	}

}

void FHoloLensARSystem::RemoveARPinFromAnchorStore(UARPin* InPin)
{
	UWMRARPin* WMRPin = Cast<UWMRARPin>(InPin);
	check(WMRPin);

	WMRPin->SetIsInAnchorStore(false);
	const FString& AnchorId = WMRPin->GetAnchorId();
	if (AnchorId.IsEmpty())
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("RemoveARPinFromAnchorStore: ARPin %s has already been removed as a runtime pin, which means its WMR anchorID is not longer valid and it cannot be removed from the store.  You must remove the pin from the anchor store *before* removing the runtime ARPin. RemoveAllARPinsFromWMRAnchorStore will also remove stored anchors that have been orphaned in this way."), *WMRPin->GetDebugName().ToString());
		return;
	}
	WMRRemoveSavedAnchor(*AnchorId);
}

void FHoloLensARSystem::RemoveAllARPinsFromAnchorStore()
{
	for(UWMRARPin* Pin : Pins)
	{
		Pin->SetIsInAnchorStore(false);
	}
	WMRClearSavedAnchors();
}


// These functions operate in WMR Tracking Space but UE4 units (so we will deal with worldscale here).

bool FHoloLensARSystem::WMRIsSpatialAnchorStoreLoaded() const
{
#if WITH_WINDOWS_MIXED_REALITY
	return WMRInterop && WMRInterop->IsSpatialAnchorStoreLoaded();
#else
	return false;
#endif
}

bool FHoloLensARSystem::WMRCreateAnchor(const wchar_t* AnchorId, FVector InPosition, FQuat InRotationQuat)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		const float WorldScale = 1.0 / TrackingSystem->GetWorldToMetersScale();
		DirectX::XMFLOAT3 Position = WindowsMixedReality::WMRUtility::ToMixedRealityVector(InPosition * WorldScale);
		DirectX::XMFLOAT4 RotationQuat = WindowsMixedReality::WMRUtility::ToMixedRealityQuaternion(InRotationQuat);
		WindowsMixedReality::HMDTrackingOrigin Origin = WindowsMixedReality::WMRUtility::ToMixedRealityTrackingOrigin(TrackingSystem->GetTrackingOrigin());
		return WMRInterop->CreateAnchor(AnchorId, Position, RotationQuat, Origin);
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
		WindowsMixedReality::HMDTrackingOrigin Origin = WindowsMixedReality::WMRUtility::ToMixedRealityTrackingOrigin(TrackingSystem->GetTrackingOrigin());
		DirectX::XMFLOAT3 scale;
		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 trans;
		if (WMRInterop->GetAnchorPose(AnchorId, scale, rot, trans, Origin))
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

bool FHoloLensARSystem::WMRSaveAnchor(const wchar_t* anchorId)
{
#if WITH_WINDOWS_MIXED_REALITY
	if (WMRInterop)
	{
		return WMRInterop->SaveAnchor(anchorId);
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

bool FHoloLensARSystem::WMRLoadAnchors(std::function<void(const wchar_t* text)> anchorIdWritingFunctionPointer)
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

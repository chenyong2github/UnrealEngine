// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensARFunctionLibrary.h"
#include "HoloLensModule.h"
#include "Engine/Engine.h"

UWMRARPin* UDEPRECATED_HoloLensARFunctionLibrary::CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return nullptr;
	}
	return ARSystem->WMRCreateNamedARPin(Name, PinToWorldTransform);
}

TArray<UWMRARPin*> UDEPRECATED_HoloLensARFunctionLibrary::LoadWMRAnchorStoreARPins()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		static TArray<UWMRARPin*> Empty;
		return Empty;
	}
	return ARSystem->WMRLoadWMRAnchorStoreARPins();
}

bool UDEPRECATED_HoloLensARFunctionLibrary::SaveARPinToWMRAnchorStore(UARPin* InPin)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return false;
	}
	if (!InPin)
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("SaveARPinToWMRAnchorStore: Trying to save Null Pin.  Ignoring."));
		return false;
	}

	return ARSystem->WMRSaveARPinToAnchorStore(InPin);
}

void UDEPRECATED_HoloLensARFunctionLibrary::RemoveARPinFromWMRAnchorStore(UARPin* InPin)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}
	if (!InPin)
	{
		UE_LOG(LogHoloLensAR, Warning, TEXT("RemoveARPinFromWMRAnchorStore: Trying to remove Null Pin.  Ignoring."));
		return;
	}

	ARSystem->WMRRemoveARPinFromAnchorStore(InPin);
}


void UDEPRECATED_HoloLensARFunctionLibrary::SetEnabledMixedRealityCamera(bool IsEnabled)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}
	ARSystem->SetEnabledMixedRealityCamera(IsEnabled);
}


FIntPoint UDEPRECATED_HoloLensARFunctionLibrary::ResizeMixedRealityCamera(const FIntPoint& size)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return FIntPoint::ZeroValue;
	}
	FIntPoint newSize = size;
	ARSystem->ResizeMixedRealityCamera(newSize);
	return newSize;
}

FTransform UDEPRECATED_HoloLensARFunctionLibrary::GetPVCameraToWorldTransform()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return FTransform::Identity;
	}
	
	return ARSystem->GetPVCameraToWorldTransform();
}

bool UDEPRECATED_HoloLensARFunctionLibrary::GetPVCameraIntrinsics(FVector2D& focalLength, int& width, int& height, FVector2D& principalPoint, FVector& radialDistortion, FVector2D& tangentialDistortion)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return false;
	}

	return ARSystem->GetPVCameraIntrinsics(focalLength, width, height, principalPoint, radialDistortion, tangentialDistortion);
}

FVector UDEPRECATED_HoloLensARFunctionLibrary::GetWorldSpaceRayFromCameraPoint(FVector2D pixelCoordinate)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return FVector(0, 0, 0);
	}

	return ARSystem->GetWorldSpaceRayFromCameraPoint(pixelCoordinate);
}

void UDEPRECATED_HoloLensARFunctionLibrary::StartCameraCapture()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}

	ARSystem->StartCameraCapture();
}

void UDEPRECATED_HoloLensARFunctionLibrary::StopCameraCapture()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}

	ARSystem->StopCameraCapture();
}

void UDEPRECATED_HoloLensARFunctionLibrary::StartQRCodeCapture()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}

	ARSystem->SetupQRCodeTracking();
}

void UDEPRECATED_HoloLensARFunctionLibrary::StopQRCodeCapture()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}

	ARSystem->StopQRCodeTracking();
}

bool UDEPRECATED_HoloLensARFunctionLibrary::ShowKeyboard()
{
	return false;
}

bool UDEPRECATED_HoloLensARFunctionLibrary::HideKeyboard()
{
	return false;
}

UWMRARPin* UDEPRECATED_HoloLensARFunctionLibrary::CreateNamedARPinAroundAnchor(FName Name, const FString& AnchorId)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return nullptr;
	}
	return ARSystem->WMRCreateNamedARPinAroundAnchor(Name, AnchorId);
}

void UDEPRECATED_HoloLensARFunctionLibrary::SetUseLegacyHandMeshVisualization(bool bUseLegacyHandMeshVisualization)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}

	return ARSystem->SetUseLegacyHandMeshVisualization(bUseLegacyHandMeshVisualization);
}


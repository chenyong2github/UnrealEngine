// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensARFunctionLibrary.h"
#include "HoloLensModule.h"
#include "Engine/Engine.h"



bool UHoloLensARFunctionLibrary::IsWMRAnchorStoreReady()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return false;
	}

	return ARSystem->IsWMRAnchorStoreReady();
}

UWMRARPin* UHoloLensARFunctionLibrary::CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return nullptr;
	}
	return ARSystem->CreateNamedARPin(Name, PinToWorldTransform);
}

bool UHoloLensARFunctionLibrary::PinComponentToARPin(USceneComponent* ComponentToPin, UWMRARPin* Pin)
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return false;
	}
	return ARSystem->PinComponentToARPin(ComponentToPin, Pin);
}

TArray<UWMRARPin*> UHoloLensARFunctionLibrary::LoadWMRAnchorStoreARPins()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		static TArray<UWMRARPin*> Empty;
		return Empty;
	}
	return ARSystem->LoadWMRAnchorStoreARPins();
}

bool UHoloLensARFunctionLibrary::SaveARPinToWMRAnchorStore(UARPin* InPin)
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

	return ARSystem->SaveARPinToAnchorStore(InPin);
}

void UHoloLensARFunctionLibrary::RemoveARPinFromWMRAnchorStore(UARPin* InPin)
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

	ARSystem->RemoveARPinFromAnchorStore(InPin);
}

void UHoloLensARFunctionLibrary::RemoveAllARPinsFromWMRAnchorStore()
{
	TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem = FHoloLensModuleAR::GetHoloLensARSystem();
	if (!ARSystem.IsValid())
	{
		return;
	}
	ARSystem->RemoveAllARPinsFromAnchorStore();
}

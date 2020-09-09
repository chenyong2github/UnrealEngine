// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "WheeledVehicleMovementComponent4WDetails.h"
#include "PropertyEditorModule.h"
#include "VehicleTransmissionDataCustomization.h"
#include "Modules/ModuleManager.h"
#include "IPhysXVehiclesEditorPlugin.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Vehicles/TireType.h"
#include "TireConfig.h"
#include "VehicleWheel.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Blueprint.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class FPhysXVehiclesEditorPlugin : public IPhysXVehiclesEditorPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout("WheeledVehicleMovementComponent4W", FOnGetDetailCustomizationInstance::CreateStatic(&FWheeledVehicleMovementComponent4WDetails::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout("VehicleTransmissionData", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FVehicleTransmissionDataCustomization::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();
	}


	virtual void ShutdownModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("WheeledVehicleMovementComponent4W");
		PropertyModule.UnregisterCustomPropertyTypeLayout("VehicleTransmissionData");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
};

IMPLEMENT_MODULE(FPhysXVehiclesEditorPlugin, PhysXVehiclesEditor)

PRAGMA_ENABLE_DEPRECATION_WARNINGS


// CONVERT TIRE TYPES UTIL
static const FString EngineDir = TEXT("/Engine/");

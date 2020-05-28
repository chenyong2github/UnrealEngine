// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosVehiclesEditorPlugin.h"

#include "AssetToolsModule.h"
#include "CoreMinimal.h"
#include "ChaosVehicles.h"
#include "AssetTypeActions_ChaosVehicles.h"
#include "ChaosVehiclesEditorStyle.h"
#include "ChaosVehiclesEditorCommands.h"
#include "HAL/ConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
//#include "ChaosVehiclesEditorDetails.h"
#include "ChaosVehicleManager.h"

PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE( IChaosVehiclesEditorPlugin, ChaosVehiclesEditor )


void IChaosVehiclesEditorPlugin::PhysSceneInit(FPhysScene* PhysScene)
{
#if WITH_CHAOS
	new FChaosVehicleManager(PhysScene);
#endif // WITH_PHYSX
}

void IChaosVehiclesEditorPlugin::PhysSceneTerm(FPhysScene* PhysScene)
{
#if WITH_CHAOS
	FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(PhysScene);
	if (VehicleManager != nullptr)
	{
		VehicleManager->DetachFromPhysScene(PhysScene);
		delete VehicleManager;
		VehicleManager = nullptr;
	}
#endif // WITH_PHYSX
}




void IChaosVehiclesEditorPlugin::StartupModule()
{
	//OnUpdatePhysXMaterialHandle = FPhysicsDelegates::OnUpdatePhysXMaterial.AddRaw(this, &FPhysXVehiclesPlugin::UpdatePhysXMaterial);
	//OnPhysicsAssetChangedHandle = FPhysicsDelegates::OnPhysicsAssetChanged.AddRaw(this, &FPhysXVehiclesPlugin::PhysicsAssetChanged);
	OnPhysSceneInitHandle = FPhysicsDelegates::OnPhysSceneInit.AddRaw(this, &IChaosVehiclesEditorPlugin::PhysSceneInit);
	OnPhysSceneTermHandle = FPhysicsDelegates::OnPhysSceneTerm.AddRaw(this, &IChaosVehiclesEditorPlugin::PhysSceneTerm);

	FChaosVehiclesEditorStyle::Get();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools& AssetTools = AssetToolsModule.Get();
	AssetTypeActions_ChaosVehicles = new FAssetTypeActions_ChaosVehicles();
	AssetTools.RegisterAssetTypeActions(MakeShareable(AssetTypeActions_ChaosVehicles));

	if (GIsEditor && !IsRunningCommandlet())
	{
	}

	// Register details view customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
}


void IChaosVehiclesEditorPlugin::ShutdownModule()
{
	//FPhysicsDelegates::OnUpdatePhysXMaterial.Remove(OnUpdatePhysXMaterialHandle);
	//FPhysicsDelegates::OnPhysicsAssetChanged.Remove(OnPhysicsAssetChangedHandle);
	FPhysicsDelegates::OnPhysSceneInit.Remove(OnPhysSceneInitHandle);
	FPhysicsDelegates::OnPhysSceneTerm.Remove(OnPhysSceneTermHandle);

	if (UObjectInitialized())
	{
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools& AssetTools = AssetToolsModule.Get();
		AssetTools.UnregisterAssetTypeActions(AssetTypeActions_ChaosVehicles->AsShared());
	}

	// Unregister details view customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomPropertyTypeLayout("ChaosDebugSubstepControl");
}

PRAGMA_ENABLE_OPTIMIZATION

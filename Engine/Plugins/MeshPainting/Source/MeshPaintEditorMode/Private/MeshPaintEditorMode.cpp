// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshPaintEditorMode.h"
#include "EditorModeRegistry.h"
#include "Modules/ModuleManager.h"
#include "MeshPaintMode.h"
#include "EditorModeManager.h"
#include "MeshPaintModeSettings.h"
#include "MeshPaintModeCommands.h"
#include "BrushSettingsCustomization.h"
#include "PropertyEditorModule.h"
#include "IMeshPaintGeometryAdapterFactory.h"
#include "MeshPaintAdapterFactory.h"
#include "MeshPaintSplineMeshAdapter.h"
#include "MeshPaintStaticMeshAdapter.h"
#include "MeshPaintSkeletalMeshAdapter.h"
#include "Settings/LevelEditorMiscSettings.h"
 

#define LOCTEXT_NAMESPACE "MeshPaintMode"

class FMeshPaintModeModule : public IModuleInterface
{
public:
	FMeshPaintModeModule() 
	{
	}

	// FModuleInterface overrides
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

protected:

	/** Stores the FEditorMode ID of the associated editor mode */
	static inline FEditorModeID GetEditorModeID()
	{
		static FEditorModeID MeshPaintModeFeatureName = FName( TEXT( "MeshPaintMode" ) );
		return MeshPaintModeFeatureName;
	}

	void Register();
	void OnPostEngineInit();
	void Unregister();


	void OnMeshPaintModeButtonClicked();
	bool IsMeshPaintModeButtonEnabled();
	void RegisterGeometryAdapterFactory(TSharedRef<IMeshPaintGeometryAdapterFactory> Factory);
	void UnregisterGeometryAdapterFactory(TSharedRef<IMeshPaintGeometryAdapterFactory> Factory);
};

void FMeshPaintModeModule::Register()
{
	RegisterGeometryAdapterFactory(MakeShareable(new FMeshPaintGeometryAdapterForSplineMeshesFactory));
	RegisterGeometryAdapterFactory(MakeShareable(new FMeshPaintGeometryAdapterForStaticMeshesFactory));
	RegisterGeometryAdapterFactory(MakeShareable(new FMeshPaintGeometryAdapterForSkeletalMeshesFactory));


	FEditorModeRegistry::Get().RegisterScriptableMode<UMeshPaintMode>(
		GetEditorModeID(),
		LOCTEXT("ModeName", "Mesh Paint"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode", "LevelEditor.MeshPaintMode.Small"),
		true,
		600
		);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("MeshColorPaintingToolProperties", FOnGetDetailCustomizationInstance::CreateStatic(&FColorPaintingSettingsCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("MeshWeightPaintingToolProperties", FOnGetDetailCustomizationInstance::CreateStatic(&FWeightPaintingSettingsCustomization::MakeInstance));
}


void FMeshPaintModeModule::OnPostEngineInit()
{
	if (!GetDefault<ULevelEditorMiscSettings>()->bEnableLegacyMeshPaintMode)
	{
		Register();
		FMeshPaintingToolActionCommands::RegisterAllToolActions();
		FMeshPaintEditorModeCommands::Register();
	}
}

void FMeshPaintModeModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMeshPaintModeModule::OnPostEngineInit);
}


void FMeshPaintModeModule::ShutdownModule()
{
	
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	FMeshPaintingToolActionCommands::UnregisterAllToolActions();
	FMeshPaintEditorModeCommands::Unregister();

	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	Unregister();
	
}




void FMeshPaintModeModule::Unregister()
{
	FEditorModeRegistry::Get().UnregisterMode(GetEditorModeID());
}


void FMeshPaintModeModule::OnMeshPaintModeButtonClicked()
{
	// *Important* - activate the mode first since FEditorModeTools::DeactivateMode will
	// activate the default mode when the stack becomes empty, resulting in multiple active visible modes.
	GLevelEditorModeTools().ActivateMode(GetEditorModeID());

	// Find and disable any other 'visible' modes since we only ever allow one of those active at a time.
	GLevelEditorModeTools().DeactivateOtherVisibleModes(GetEditorModeID());

}



bool FMeshPaintModeModule::IsMeshPaintModeButtonEnabled( )
{
	return true;
}

void  FMeshPaintModeModule::RegisterGeometryAdapterFactory(TSharedRef<IMeshPaintGeometryAdapterFactory> Factory)
{
	FMeshPaintAdapterFactory::FactoryList.Add(Factory);
}

void  FMeshPaintModeModule::UnregisterGeometryAdapterFactory(TSharedRef<IMeshPaintGeometryAdapterFactory> Factory)
{
	FMeshPaintAdapterFactory::FactoryList.Remove(Factory);
}

IMPLEMENT_MODULE(FMeshPaintModeModule, MeshPaintEditorMode)

#undef LOCTEXT_NAMESPACE

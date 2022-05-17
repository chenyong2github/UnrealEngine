// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshEditorModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "IAssetTools.h"
#include "ISettingsModule.h"
#include "AssetTypeActions_NaniteDisplacedMesh.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMeshEditor"


void FNaniteDisplacedMeshEditorModule::StartupModule()
{
	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools& AssetTools = AssetToolsModule.Get();

	NaniteDisplacedMeshAssetActions = new FAssetTypeActions_NaniteDisplacedMesh();
	AssetTools.RegisterAssetTypeActions(MakeShareable(NaniteDisplacedMeshAssetActions));

	// The procedural tools flow use this transient package to avoid name collision with other transient object
	NaniteDisplacedMeshTransientPackage = NewObject<UPackage>(nullptr, TEXT("/Engine/Transient/NaniteDisplacedMesh"), RF_Transient);
	NaniteDisplacedMeshTransientPackage->AddToRoot();
}
	
void FNaniteDisplacedMeshEditorModule::ShutdownModule()
{
	NaniteDisplacedMeshTransientPackage->RemoveFromRoot();
}

FNaniteDisplacedMeshEditorModule& FNaniteDisplacedMeshEditorModule::GetModule()
{
	static const FName ModuleName = "NaniteDisplacedMeshEditor";
	return FModuleManager::LoadModuleChecked<FNaniteDisplacedMeshEditorModule>(ModuleName);
}

UPackage* FNaniteDisplacedMeshEditorModule::GetNaniteDisplacementMeshTransientPackage() const
{
	return NaniteDisplacedMeshTransientPackage;
}

IMPLEMENT_MODULE(FNaniteDisplacedMeshEditorModule, NaniteDisplacedMeshEditor);

#undef LOCTEXT_NAMESPACE

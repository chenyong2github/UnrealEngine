// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "IAssetTools.h"
#include "ISettingsModule.h"
#include "AssetTypeActions_NaniteDisplacedMesh.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMeshEditor"

class FNaniteDisplacedMeshEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools& AssetTools = AssetToolsModule.Get();

		NaniteDisplacedMeshAssetActions = new FAssetTypeActions_NaniteDisplacedMesh();
		AssetTools.RegisterAssetTypeActions(MakeShareable(NaniteDisplacedMeshAssetActions));
	}
	
	virtual void ShutdownModule() override
	{
	}

private:
	FAssetTypeActions_NaniteDisplacedMesh* NaniteDisplacedMeshAssetActions;
};

IMPLEMENT_MODULE(FNaniteDisplacedMeshEditorModule, NaniteDisplacedMeshEditor);

#undef LOCTEXT_NAMESPACE

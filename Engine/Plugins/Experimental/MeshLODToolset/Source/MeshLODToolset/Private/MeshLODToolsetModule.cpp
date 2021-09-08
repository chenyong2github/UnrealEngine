// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshLODToolsetModule.h"
#include "Tools/LODGenerationSettingsAsset.h"
#include "AssetTypeActions_Base.h"

#define LOCTEXT_NAMESPACE "FMeshLODToolsetModule"

DEFINE_LOG_CATEGORY(LogMeshLODToolset);


/**
 * Asset Type Actions for UStaticMeshLODGenerationSettings Assets
 */
class FAssetTypeActions_StaticMeshLODGenerationSettings : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "FAssetTypeActions_StaticMeshLODGenerationSettings", "AutoLOD Settings"); }
	virtual FColor GetTypeColor() const override { return FColor( 175, 0, 128 ); }
	virtual UClass* GetSupportedClass() const override { return UStaticMeshLODGenerationSettings::StaticClass(); }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }
	virtual bool CanLocalize() const override { return false; }
};




void FMeshLODToolsetModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	// Register asset actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	{
		TSharedRef<IAssetTypeActions> StaticMeshLODGenerationSettings_AssetActions = MakeShareable( new FAssetTypeActions_StaticMeshLODGenerationSettings );
		AssetTools.RegisterAssetTypeActions( StaticMeshLODGenerationSettings_AssetActions );
		RegisteredAssetTypeActions.Add(StaticMeshLODGenerationSettings_AssetActions);
	}
}

void FMeshLODToolsetModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Unregister asset actions
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		for (TSharedRef<IAssetTypeActions> RegisteredAssetTypeAction : RegisteredAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(RegisteredAssetTypeAction);
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMeshLODToolsetModule, MeshLODToolset)

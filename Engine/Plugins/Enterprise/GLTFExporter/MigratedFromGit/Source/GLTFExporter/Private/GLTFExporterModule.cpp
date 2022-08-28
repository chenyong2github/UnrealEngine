// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExporterModule.h"
#include "Converters/GLTFMaterialProxyFactory.h"
#include "Actions/GLTFProxyAssetActions.h"
#include "Interfaces/IPluginManager.h"
#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AssetToolsModule.h"

class FAssetTypeActions_Base;
#endif

DEFINE_LOG_CATEGORY(LogGLTFExporter);

UMaterialInterface* IGLTFExporterModule::CreateProxyMaterial(UMaterialInterface* Material, const UGLTFProxyOptions* Options, const FString& RootPath)
{
#if WITH_EDITOR
	FGLTFMaterialProxyFactory ProxyFactory(Options);
	ProxyFactory.RootPath = RootPath.IsEmpty() ? FPaths::GetPath(Material->GetPathName()) / TEXT("GLTF") : RootPath;
	return ProxyFactory.Create(Material);
#else
	// TODO: report error
	return nullptr;
#endif
}

class FGLTFExporterModule final : public IGLTFExporterModule
{
public:

	virtual void StartupModule() override
	{
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("GLTFExporter"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/GLTFExporter"), PluginShaderDir);

#if WITH_EDITOR
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FGLTFExporterModule::PostEngineInit);
#endif
	}

#if WITH_EDITOR
	void PostEngineInit()
	{
		if (FAssetToolsModule::IsModuleLoaded())
		{
			IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();

			// TODO: add support for when GetAssetTypeActionsForClass returns null pointer
			AssetTypeActionsArray.Add(MakeShared<FGLTFProxyAssetActions>(AssetTools.GetAssetTypeActionsForClass(UMaterial::StaticClass()).Pin().ToSharedRef()));
			AssetTypeActionsArray.Add(MakeShared<FGLTFProxyAssetActions>(AssetTools.GetAssetTypeActionsForClass(UMaterialInstanceConstant::StaticClass()).Pin().ToSharedRef()));
			AssetTypeActionsArray.Add(MakeShared<FGLTFProxyAssetActions>(AssetTools.GetAssetTypeActionsForClass(UMaterialInstanceDynamic::StaticClass()).Pin().ToSharedRef()));
			AssetTypeActionsArray.Add(MakeShared<FGLTFProxyAssetActions>(AssetTools.GetAssetTypeActionsForClass(UMaterialInterface::StaticClass()).Pin().ToSharedRef()));

			for (TSharedRef<IAssetTypeActions>& AssetTypeActions : AssetTypeActionsArray)
			{
				AssetTools.RegisterAssetTypeActions(AssetTypeActions);
			}
		}
	}
#endif

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (FAssetToolsModule::IsModuleLoaded())
		{
			IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
			for (TSharedRef<IAssetTypeActions>& AssetTypeActions : AssetTypeActionsArray)
			{
				AssetTools.UnregisterAssetTypeActions(AssetTypeActions);
			}
		}

		AssetTypeActionsArray.Empty();
#endif
	}

private:

#if WITH_EDITOR
	TArray<TSharedRef<IAssetTypeActions>> AssetTypeActionsArray;
#endif
};

IMPLEMENT_MODULE(FGLTFExporterModule, GLTFExporter);

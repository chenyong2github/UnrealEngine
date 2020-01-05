// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCoreTechExtensionModule.h"

#include "CoreTechParametricSurfaceExtension.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IStaticMeshEditor.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshEditorModule.h"
#include "UObject/StrongObjectPtr.h"


#define LOCTEXT_NAMESPACE "DatasmithCoreTechExtensionModule"


/** UI extension that displays a Retessellate action in the StaticMeshEditor */
namespace StaticMeshEditorExtenser
{
	bool CanExecute(UStaticMesh* Target)
	{
		TArray<FAssetData> AssetData;
		AssetData.Emplace(Target);

		FCoreTechRetessellate_Impl RetessellateAction;
		return RetessellateAction.CanApplyOnAssets(AssetData);
	}

	void Execute(UStaticMesh* Target)
	{
		TArray<FAssetData> AssetData;
		AssetData.Emplace(Target);

		FCoreTechRetessellate_Impl RetessellateAction;
		RetessellateAction.ApplyOnAssets(AssetData);
	}

	void ExtendAssetMenu(FMenuBuilder& MenuBuilder, UStaticMesh* Target)
	{
		MenuBuilder.AddMenuEntry(
			FCoreTechRetessellate_Impl::Label,
			FCoreTechRetessellate_Impl::Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&StaticMeshEditorExtenser::Execute, Target),
				FCanExecuteAction::CreateStatic(&StaticMeshEditorExtenser::CanExecute, Target)
			)
		);
	}

	TSharedRef<FExtender> CreateExtenderForObjects(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects)
	{
		TSharedRef<FExtender> Extender = MakeShareable(new FExtender);

		if (UStaticMesh* Target = Objects.Num() ? Cast<UStaticMesh>(Objects[0]) : nullptr)
		{
			Extender->AddMenuExtension(
				"AssetEditorActions",
				EExtensionHook::Position::After,
				CommandList,
				FMenuExtensionDelegate::CreateStatic(&StaticMeshEditorExtenser::ExtendAssetMenu, Target)
			);
		}

		return Extender;
	}

	void Register()
	{
		if (!IsRunningCommandlet())
		{
			IStaticMeshEditorModule& StaticMeshEditorModule = FModuleManager::Get().LoadModuleChecked<IStaticMeshEditorModule>("StaticMeshEditor");
			TArray<FAssetEditorExtender>& ExtenderDelegates = StaticMeshEditorModule.GetMenuExtensibilityManager()->GetExtenderDelegates();
			ExtenderDelegates.Add(FAssetEditorExtender::CreateStatic(&StaticMeshEditorExtenser::CreateExtenderForObjects));
		}
	}
};

FDatasmithCoreTechExtensionModule& FDatasmithCoreTechExtensionModule::Get()
{
	return FModuleManager::LoadModuleChecked< FDatasmithCoreTechExtensionModule >(DATASMITHCORETECHEXTENSION_MODULE_NAME);
}

bool FDatasmithCoreTechExtensionModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(DATASMITHCORETECHEXTENSION_MODULE_NAME);
}

void FDatasmithCoreTechExtensionModule::StartupModule()
{
	if (!IsRunningCommandlet())
	{
		StaticMeshEditorExtenser::Register();
	}
}

IMPLEMENT_MODULE(FDatasmithCoreTechExtensionModule, DatasmithCoreTechExtension);

#undef LOCTEXT_NAMESPACE // "DatasmithCoreTechExtensionModule"


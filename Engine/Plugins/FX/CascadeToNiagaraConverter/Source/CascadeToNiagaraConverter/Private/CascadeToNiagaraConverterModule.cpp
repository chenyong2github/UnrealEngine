// Copyright Epic Games, Inc. All Rights Reserved.

#include "CascadeToNiagaraConverterModule.h"
#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "ContentBrowserModule.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleSystem.h"
#include "IPythonScriptPlugin.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

IMPLEMENT_MODULE(ICascadeToNiagaraConverterModule, CascadeToNiagaraConverter);

#define LOCTEXT_NAMESPACE "CascadeToNiagaraConverterModule"

void ICascadeToNiagaraConverterModule::StartupModule()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&ICascadeToNiagaraConverterModule::OnExtendContentBrowserAssetSelectionMenu));
}

void ICascadeToNiagaraConverterModule::ShutdownModule()
{
}

TSharedRef<FExtender> ICascadeToNiagaraConverterModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	Extender->AddMenuExtension(
		"GetAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateStatic(&AddMenuExtenderConvertEntry, SelectedAssets)
		);

	return Extender;
}

void ICascadeToNiagaraConverterModule::AddMenuExtenderConvertEntry(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	if (SelectedAssets.Num() == 1 && SelectedAssets[0].GetClass()->IsChildOf<UParticleSystem>())
	{
		UParticleSystem* CascadeSystem = static_cast<UParticleSystem*>(SelectedAssets[0].GetAsset());

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConvertToNiagaraSystem", "Convert To Niagara System"),
			LOCTEXT("ConvertToNiagaraSystem_Tooltip", "Duplicate and convert this Cascade System to an equivalent Niagara System."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&ExecuteConvertCascadeSystemToNiagaraSystem, CascadeSystem))
		);
	}
}

void ICascadeToNiagaraConverterModule::ExecuteConvertCascadeSystemToNiagaraSystem(UParticleSystem* CascadeSystem)
{
	FString Command = "../../Plugins/FX/CascadeToNiagaraConverter/Content/Python/ConvertCascadeToNiagara.py ";
	Command.Append(*CascadeSystem->GetPathName());
	IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);
}

#undef LOCTEXT_NAMESPACE //"CascadeToNiagaraConverterModule"

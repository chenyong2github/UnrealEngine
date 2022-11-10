// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosFlesh/ChaosFleshEditorPlugin.h"
#include "Editor/FleshEditorStyle.h"

#include "AssetToolsModule.h"
#include "ChaosFlesh/FleshComponent.h"
#include "ChaosFlesh/Cmd/FleshAssetConversion.h"
#include "ChaosFlesh/Cmd/ChaosFleshCommands.h"
#include "ChaosFlesh/Asset/AssetTypeActions_FleshAsset.h"
#include "ChaosFlesh/Asset/AssetTypeActions_ChaosDeformableSolverAsset.h"
#include "ChaosFlesh/Asset/FleshAssetThumbnailRenderer.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "Features/IModularFeatures.h"
#include "HAL/ConsoleManager.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/CoreStyle.h"
#include "ToolMenus.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "FleshEditor"

#define BOX_BRUSH(StyleSet, RelativePath, ...) FSlateBoxBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

void IChaosFleshEditorPlugin::StartupModule()
{
	FChaosFleshEditorStyle::Get();

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	FleshAssetActions = new FAssetTypeActions_FleshAsset();
	AssetTools.RegisterAssetTypeActions(MakeShareable(FleshAssetActions));
	ChaosDeformableSolverAssetActions = new FAssetTypeActions_ChaosDeformableSolver();
	AssetTools.RegisterAssetTypeActions(MakeShareable(ChaosDeformableSolverAssetActions));


	if (GIsEditor && !IsRunningCommandlet())
	{
		EditorCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("p.Chaos.Flesh.ImportFile"),
			TEXT("Creates a FleshAsset from the input file"),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FChaosFleshCommands::ImportFile),
			ECVF_Default
		));
	}

	UThumbnailManager::Get().RegisterCustomRenderer(UFleshAsset::StaticClass(), UFleshAssetThumbnailRenderer::StaticClass());

}

void IChaosFleshEditorPlugin::ShutdownModule()
{
	if (UObjectInitialized())
	{	
		UThumbnailManager::Get().UnregisterCustomRenderer(UFleshAsset::StaticClass());

		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
		AssetTools.UnregisterAssetTypeActions(FleshAssetActions->AsShared());
		AssetTools.UnregisterAssetTypeActions(ChaosDeformableSolverAssetActions->AsShared());
	}
}

TSharedRef<FAssetEditorToolkit> IChaosFleshEditorPlugin::CreateFleshAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* FleshAsset)
{
	TSharedPtr<FDataflowEditorToolkit> NewAssetEditor = MakeShared<FDataflowEditorToolkit>();
	NewAssetEditor->InitDataflowEditor(Mode, InitToolkitHost, FleshAsset);
	return StaticCastSharedPtr<FAssetEditorToolkit>(NewAssetEditor).ToSharedRef();
}

IMPLEMENT_MODULE(IChaosFleshEditorPlugin, FleshAssetEditor)


#undef LOCTEXT_NAMESPACE

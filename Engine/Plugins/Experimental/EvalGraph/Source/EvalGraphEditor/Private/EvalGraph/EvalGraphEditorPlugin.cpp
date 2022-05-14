// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEditorPlugin.h"

#include "AssetToolsModule.h"
#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "EvalGraphEditorToolkit.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "EvalGraph/EvalGraphAssetActions.h"
#include "EvalGraph/EvalGraphSNodeFactories.h"

#define LOCTEXT_NAMESPACE "EvalGraphEditor"

//#define BOX_BRUSH(StyleSet, RelativePath, ...) FSlateBoxBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
//#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

void IEvalGraphEditorPlugin::StartupModule()
{
	EvalGraphAssetActions = new FEvalGraphAssetActions();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools& AssetTools = AssetToolsModule.Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(EvalGraphAssetActions));


	EvalGraphSNodeFactory = MakeShareable(new FEvalGraphSNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(EvalGraphSNodeFactory);
}

void IEvalGraphEditorPlugin::ShutdownModule()
{
	if (UObjectInitialized())
	{
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools& AssetTools = AssetToolsModule.Get();

		AssetTools.UnregisterAssetTypeActions(EvalGraphAssetActions->AsShared());

		FEdGraphUtilities::UnregisterVisualNodeFactory(EvalGraphSNodeFactory);
	}
}

TSharedRef<FAssetEditorToolkit> IEvalGraphEditorPlugin::CreateEvalGraphAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* FleshAsset)
{
	TSharedPtr<FEvalGraphEditorToolkit> NewEvalGraphAssetEditor = MakeShared<FEvalGraphEditorToolkit>();
	NewEvalGraphAssetEditor->InitEvalGraphEditor(Mode, InitToolkitHost, FleshAsset);
	return StaticCastSharedPtr<FAssetEditorToolkit>(NewEvalGraphAssetEditor).ToSharedRef();
}


IMPLEMENT_MODULE(IEvalGraphEditorPlugin, EvalGraphEditor)


#undef LOCTEXT_NAMESPACE

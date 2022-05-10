// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEditorPlugin.h"

#include "AssetToolsModule.h"
#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "EvalGraph/EvalGraphAssetActions.h"
#include "EvalGraph/EvalGraphNodeFactories.h"

#define LOCTEXT_NAMESPACE "EvalGraphEditor"

//#define BOX_BRUSH(StyleSet, RelativePath, ...) FSlateBoxBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
//#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

void IEvalGraphEditorPlugin::StartupModule()
{
	EvalGraphAssetActions = new FEvalGraphAssetActions();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools& AssetTools = AssetToolsModule.Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(EvalGraphAssetActions));


	//EvalGraphNodeFactory = MakeShareable(new FEvalGraphNodeFactory());
	//FEdGraphUtilities::RegisterVisualNodeFactory(EvalGraphNodeFactory);
}

void IEvalGraphEditorPlugin::ShutdownModule()
{
	if (UObjectInitialized())
	{
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools& AssetTools = AssetToolsModule.Get();

		AssetTools.UnregisterAssetTypeActions(EvalGraphAssetActions->AsShared());

		//FEdGraphUtilities::UnregisterVisualNodeFactory(EvalGraphNodeFactory);
	}
}

IMPLEMENT_MODULE(IEvalGraphEditorPlugin, EvalGraphEditor)


#undef LOCTEXT_NAMESPACE

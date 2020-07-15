// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorModule.h"

#include "AssetTypeActions_Base.h"
#include "Brushes/SlateImageBrush.h"
#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "MetasoundAssetTypeActions.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"

DEFINE_LOG_CATEGORY(LogMetasoundEditor);


namespace MetasoundEditorUtils
{
	static const FName AssetToolName = TEXT("AssetTools");

	template <typename T>
	void AddAssetAction(IAssetTools& AssetTools, TArray<TSharedPtr<FAssetTypeActions_Base>>& AssetArray)
	{
		TSharedPtr<T> AssetAction = MakeShared<T>();
		TSharedPtr<FAssetTypeActions_Base> AssetActionBase = StaticCastSharedPtr<FAssetTypeActions_Base>(AssetAction);
		AssetTools.RegisterAssetTypeActions(AssetAction.ToSharedRef());
		AssetArray.Add(AssetActionBase);
	}
} // namespace MetasoundEditorUtils

class FMetasoundSlateStyle : public FSlateStyleSet
{
public:
	FMetasoundSlateStyle()
		: FSlateStyleSet("MetasoundStyle")
	{
		SetParentStyleName(FEditorStyle::GetStyleSetName());

		SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		static const FVector2D Icon20x20(20.0f, 20.0f);
		static const FVector2D Icon40x40(40.0f, 40.0f);

		// Metasound Editor
		{
			Set("MetasoundEditor.Play", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_PlayCue_40x.png")), Icon40x40));
			Set("MetasoundEditor.Play.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_PlayCue_40x.png")), Icon20x20));
			Set("MetasoundEditor.Stop", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_Stop_40x.png")), Icon40x40));
			Set("MetasoundEditor.Stop.Small", new FSlateImageBrush(RootToContentDir(TEXT("Icons/icon_SCueEd_Stop_40x.png")), Icon20x20));
		}

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}
};

class FMetasoundEditorModule : public IMetasoundEditorModule
{
	virtual void StartupModule() override
	{
		// Register Metasound asset type actions
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(MetasoundEditorUtils::AssetToolName).Get();
		MetasoundEditorUtils::AddAssetAction<FAssetTypeActions_Metasound>(AssetTools, AssetActions);

		StyleSet = MakeShared<FMetasoundSlateStyle>();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded(MetasoundEditorUtils::AssetToolName))
		{
			IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(MetasoundEditorUtils::AssetToolName).Get();
			for (TSharedPtr<FAssetTypeActions_Base>& AssetAction : AssetActions)
			{
				AssetTools.UnregisterAssetTypeActions(AssetAction.ToSharedRef());
			}
		}
		AssetActions.Reset();
	}

	TArray<TSharedPtr<FAssetTypeActions_Base>> AssetActions;
	TSharedPtr<FSlateStyleSet> StyleSet;
};


IMPLEMENT_MODULE(FMetasoundEditorModule, MetasoundEditor);

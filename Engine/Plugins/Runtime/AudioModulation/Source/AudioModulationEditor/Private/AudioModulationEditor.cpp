// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationEditor.h"

#include "AssetRegistryModule.h"
#include "AssetTypeActions/AssetTypeActions_SoundControlBus.h"
#include "AssetTypeActions/AssetTypeActions_SoundControlBusMix.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulationGenerator.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulationParameter.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulationPatch.h"
#include "Editors/ModulationPatchCurveEditorViewStacked.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IAssetRegistry.h"
#include "ICurveEditorModule.h"
#include "Internationalization/Internationalization.h"
#include "Layouts/SoundControlBusMixStageLayout.h"
#include "Layouts/SoundControlModulationPatchLayout.h"
#include "Layouts/SoundModulationParameterSettingsLayout.h"
#include "Layouts/SoundModulationTransformLayout.h"
#include "LevelEditor.h"
#include "Sound/SoundBase.h"
#include "SoundModulationParameter.h"
#include "SoundModulationTransform.h"
#include "Templates/SharedPointer.h"
#include "Textures/SlateIcon.h"
#include "UObject/UObjectIterator.h"


DEFINE_LOG_CATEGORY(LogAudioModulationEditor);

namespace AudioModulationEditor
{
	static const FName ToolName = TEXT("AssetTools");

	template <typename T>
	void AddAssetAction(IAssetTools& AssetTools, TArray<TSharedPtr<FAssetTypeActions_Base>>& AssetArray)
	{
		TSharedPtr<T> AssetAction = MakeShared<T>();
		TSharedPtr<FAssetTypeActions_Base> AssetActionBase = StaticCastSharedPtr<FAssetTypeActions_Base>(AssetAction);
		AssetTools.RegisterAssetTypeActions(AssetAction.ToSharedRef());
		AssetArray.Add(AssetActionBase);
	}
} // namespace AudioModulationEditor


FAudioModulationEditorModule::FAudioModulationEditorModule()
{
}

TSharedPtr<FExtensibilityManager> FAudioModulationEditorModule::GetModulationPatchMenuExtensibilityManager()
{
	return ModulationPatchMenuExtensibilityManager;
}

TSharedPtr<FExtensibilityManager> FAudioModulationEditorModule::GetModulationPatchToolbarExtensibilityManager()
{
	return ModulationPatchToolBarExtensibilityManager;
}

void FAudioModulationEditorModule::SetIcon(const FString& ClassName)
{
	static const FVector2D Icon16 = FVector2D(16.0f, 16.0f);
	static const FVector2D Icon64 = FVector2D(64.0f, 64.0f);

	static const FString IconDir = FPaths::EnginePluginsDir() / FString(TEXT("Runtime/AudioModulation/Content"));

	const FString IconFileName16 = FString::Printf(TEXT("%s_16x.png"), *ClassName);
	const FString IconFileName64 = FString::Printf(TEXT("%s_64x.png"), *ClassName);

	StyleSet->Set(*FString::Printf(TEXT("ClassIcon.%s"), *ClassName), new FSlateImageBrush(IconDir / IconFileName16, Icon16));
	StyleSet->Set(*FString::Printf(TEXT("ClassThumbnail.%s"), *ClassName), new FSlateImageBrush(IconDir / IconFileName64, Icon64));
}

void FAudioModulationEditorModule::StartupModule()
{
	static const FName AudioModulationStyleName(TEXT("AudioModulationStyle"));
	StyleSet = MakeShared<FSlateStyleSet>(AudioModulationStyleName);

	ModulationPatchToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();
	ModulationPatchMenuExtensibilityManager = MakeShared<FExtensibilityManager>();

	// Register the audio editor asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AudioModulationEditor::ToolName).Get();

	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundControlBusMix>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulationGenerator>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulationParameter>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulationPatch>(AssetTools, AssetActions);

	SetIcon(TEXT("SoundControlBus"));
	SetIcon(TEXT("SoundControlBusMix"));
	SetIcon(TEXT("SoundModulationGenerator"));
	SetIcon(TEXT("SoundModulationPatch"));
	SetIcon(TEXT("SoundModulationParameter"));

	RegisterCustomPropertyLayouts();

	ICurveEditorModule& CurveEditorModule = FModuleManager::LoadModuleChecked<ICurveEditorModule>("CurveEditor");
	FModPatchCurveEditorModel::ViewId = CurveEditorModule.RegisterView(FOnCreateCurveEditorView::CreateStatic(
		[](TWeakPtr<FCurveEditor> WeakCurveEditor) -> TSharedRef<SCurveEditorView>
		{
			return SNew(SModulationPatchEditorViewStacked, WeakCurveEditor);
		}
	));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

	// All parameters are required to always be loaded in editor to enable them to be referenced via object
	// metadata and custom layouts, even if they are not referenced by runtime uobjects/systems directly
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AssetRegistry.OnAssetAdded().AddLambda([](const FAssetData& InAssetData)
	{
		if (InAssetData.GetClass()->IsChildOf<USoundModulationParameter>())
		{
			if (USoundModulationParameter* Parameter = CastChecked<USoundModulationParameter>(InAssetData.GetAsset()))
			{
				Parameter->AddToRoot();
			}
		}
	});
	AssetRegistry.OnInMemoryAssetDeleted().AddLambda([](UObject* ObjectDeleted)
	{
		if (USoundModulationParameter* Parameter = Cast<USoundModulationParameter>(ObjectDeleted))
		{
			Parameter->RemoveFromRoot();
		}
	});
}

void FAudioModulationEditorModule::RegisterCustomPropertyLayouts()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundModulationTransform",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundModulationTransformLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundModulationParameterSettings",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundModulationParameterSettingsLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundControlModulationPatch",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundControlModulationPatchLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundControlBusMixStage",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundControlBusMixStageLayoutCustomization::MakeInstance));
}

void FAudioModulationEditorModule::ShutdownModule()
{
	ModulationPatchToolBarExtensibilityManager.Reset();
	ModulationPatchMenuExtensibilityManager.Reset();

	if (FModuleManager::Get().IsModuleLoaded(AudioModulationEditor::ToolName))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(AudioModulationEditor::ToolName).Get();
		for (TSharedPtr<FAssetTypeActions_Base>& AssetAction : AssetActions)
		{
			AssetTools.UnregisterAssetTypeActions(AssetAction.ToSharedRef());
		}
	}
	AssetActions.Reset();

	if (ICurveEditorModule* CurveEditorModule = FModuleManager::GetModulePtr<ICurveEditorModule>("CurveEditor"))
	{
		CurveEditorModule->UnregisterView(FModPatchCurveEditorModel::ViewId);
	}
	FModPatchCurveEditorModel::ViewId = ECurveEditorViewID::Invalid;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
}

IMPLEMENT_MODULE(FAudioModulationEditorModule, AudioModulationEditor);

// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationEditor.h"

#include "AssetTypeActions/AssetTypeActions_SoundControlBus.h"
#include "AssetTypeActions/AssetTypeActions_SoundControlBusMix.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulationPatch.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulationSettings.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulatorLFO.h"
#include "Editors/ModulationSettingsCurveEditorViewStacked.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "ICurveEditorModule.h"
#include "Internationalization/Internationalization.h"
#include "Layouts/SoundControlModulationPatchLayout.h"
#include "Layouts/SoundModulationTransformLayout.h"
#include "LevelEditor.h"
#include "SoundModulationTransform.h"
#include "Templates/SharedPointer.h"
#include "Textures/SlateIcon.h"
#include "UObject/UObjectIterator.h"


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

TSharedPtr<FExtensibilityManager> FAudioModulationEditorModule::GetModulationSettingsMenuExtensibilityManager()
{
	return ModulationSettingsMenuExtensibilityManager;
}

TSharedPtr<FExtensibilityManager> FAudioModulationEditorModule::GetModulationSettingsToolbarExtensibilityManager()
{
	return ModulationSettingsToolBarExtensibilityManager;
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

	ModulationSettingsToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();
	ModulationSettingsMenuExtensibilityManager = MakeShared<FExtensibilityManager>();

	// Register the audio editor asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AudioModulationEditor::ToolName).Get();

	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundVolumeControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundPitchControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundHPFControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundLPFControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundControlBus>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundControlBusMix>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulatorLFO>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulationPatch>(AssetTools, AssetActions);
	AudioModulationEditor::AddAssetAction<FAssetTypeActions_SoundModulationSettings>(AssetTools, AssetActions);

	SetIcon(TEXT("SoundVolumeControlBus"));
	SetIcon(TEXT("SoundPitchControlBus"));
	SetIcon(TEXT("SoundHPFControlBus"));
	SetIcon(TEXT("SoundLPFControlBus"));
	SetIcon(TEXT("SoundControlBus"));
	SetIcon(TEXT("SoundControlBusMix"));
	SetIcon(TEXT("SoundBusModulatorLFO"));
	SetIcon(TEXT("SoundModulationSettings"));

	RegisterCustomPropertyLayouts();

	ICurveEditorModule& CurveEditorModule = FModuleManager::LoadModuleChecked<ICurveEditorModule>("CurveEditor");
	FModCurveEditorModel::ViewId = CurveEditorModule.RegisterView(FOnCreateCurveEditorView::CreateStatic(
		[](TWeakPtr<FCurveEditor> WeakCurveEditor) -> TSharedRef<SCurveEditorView>
		{
			return SNew(SModulationSettingsEditorViewStacked, WeakCurveEditor);
		}
	));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FAudioModulationEditorModule::RegisterCustomPropertyLayouts()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundModulationOutputTransform",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundModulationOutputTransformLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundVolumeModulationPatch",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundVolumeModulationPatchLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundPitchModulationPatch",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundPitchModulationPatchLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundHPFModulationPatch",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundHPFModulationPatchLayoutCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundLPFModulationPatch",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundLPFModulationPatchLayoutCustomization::MakeInstance));
}

void FAudioModulationEditorModule::ShutdownModule()
{
	ModulationSettingsToolBarExtensibilityManager.Reset();
	ModulationSettingsMenuExtensibilityManager.Reset();

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
		CurveEditorModule->UnregisterView(FModCurveEditorModel::ViewId);
	}
	FModCurveEditorModel::ViewId = ECurveEditorViewID::Invalid;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
}

IMPLEMENT_MODULE(FAudioModulationEditorModule, AudioModulationEditor);

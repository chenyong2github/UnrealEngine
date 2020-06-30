// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "EditorUndoClient.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Color.h"
#include "Misc/NotifyHook.h"
#include "Sound/SoundEffectPreset.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWidget.h"

#include "SoundEffectPresetEditor.generated.h"

// Forward Declarations
class FCurveEditor;
class IToolkitHost;
class SCurveEditorPanel;
class SSoundEffectEditorPreviewViewport;
class UClass;
class UCurveBase;
class UWidgetBlueprint;
class USoundEffectPreset;


UCLASS(Blueprintable)
class USoundEffectPresetUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
	USoundEffectPreset* Preset;

	UFUNCTION(BlueprintImplementableEvent)
	void OnPresetChanged(FName PropertyName);
};

class FSoundEffectPresetEditor : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient, public FGCObject
{
public:
	FSoundEffectPresetEditor();
	virtual ~FSoundEffectPresetEditor() = default;

	void Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundEffectPreset* PresetToEdit);

	/** FAssetEditorToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	/** FNotifyHook interface */
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

protected:
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	/** Finds and returns the UserWidget of the preset if one is provided by the use in the AudioEditor developer settings. */
	void InitPresetWidget();

	/**	Spawns the tab allowing for editing/viewing the blueprint widget for the associated SoundEffectPreset */
	TSharedRef<SDockTab> SpawnTab_UserWidgetEditor(const FSpawnTabArgs& Args);

	/**	Spawns the tab allowing for editing/viewing details panel */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

	/** Get the orientation for the snap value controls. */
	EOrientation GetSnapLabelOrientation() const;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesView;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;

	USoundEffectPreset* SoundEffectPreset;
	USoundEffectPresetUserWidget* UserWidget;

	/** Tab Ids */
	static const FName PropertiesTabId;
	static const FName UserWidgetTabId;
};

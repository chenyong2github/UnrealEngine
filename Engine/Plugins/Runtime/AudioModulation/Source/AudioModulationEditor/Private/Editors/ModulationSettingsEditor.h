// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CurveEditorTypes.h"
#include "Curves/CurveOwnerInterface.h"
#include "EditorUndoClient.h"
#include "Framework/Docking/TabManager.h"
#include "Math/Color.h"
#include "Misc/NotifyHook.h"
#include "ModulationSettingsCurveEditorViewStacked.h"
#include "SoundModulationPatch.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/SWidget.h"


// Forward Declarations
class FCurveEditor;
class IToolkitHost;
class SCurveEditorPanel;
class UCurveBase;
class USoundModulationSettings;


class FModulationSettingsEditor : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient
{
public:
	FModulationSettingsEditor();

	virtual ~FModulationSettingsEditor() = default;

	void Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundModulationSettings* SettingsToEdit);

	/** FAssetEditorToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	/** FNotifyHook interface */
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, UProperty* PropertyThatChanged) override;

protected:
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	void SetCurveAtOrderIndex(int32 InCurveIndex, FRichCurve& InRichCurve, EModSettingsOutputEditorCurveSource InSource, UCurveFloat* InSharedCurve = nullptr);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_OutputCurve(const FSpawnTabArgs& Args);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

	int32 GetControlModulationCount() const;

	int32 GetCurveOrderIndex(EModSettingsEditorCurveOutput InCurveOutput, int32 InControlIndex) const;

	int32 GetControlIndex(int32 InCurveIndex) const;

	FName GetControlName(int32 InCurveIndex) const;

	int32 GetCurveCount() const;

	/** Get the orientation for the snap value controls. */
	EOrientation GetSnapLabelOrientation() const;

	/** Updates provided curve output's curve. Returns index of CurveModelIDs that was updated */
	void UpdateCurve(int32 InCurveIndex);

	/** Updates all curves */
	void UpdateCurves();

	/** Trims keys out-of-bounds in provided output */
	void TrimKeys(FSoundModulationOutputBase& InOutput) const;

	FSoundModulationOutputBase* FindModulationOutput(int32 InCurveIndex);

	void GenerateExpressionCurve(const FSoundModulationOutputBase& InOutput, int32 InCurveIndex, bool bIsUnset = false);

	TSharedPtr<FUICommandList> ToolbarCurveTargetCommands;

	TSharedPtr<FCurveEditor> CurveEditor;
	TSharedPtr<SCurveEditorPanel> CurvePanel;

	TArray<TSharedPtr<FRichCurve>> ExpressionCurves;

	TArray<FCurveModelID> CurveModels;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesView;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;
	static const FName CurveTabId;
	static const FName PropertiesTabId;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CurveEditorTypes.h"
#include "Curves/CurveOwnerInterface.h"
#include "EditorUndoClient.h"
#include "Framework/Docking/TabManager.h"
#include "Math/Color.h"
#include "Misc/NotifyHook.h"
#include "ModulationPatchCurveEditorViewStacked.h"
#include "SoundModulationPatch.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/SWidget.h"


// Forward Declarations
class FCurveEditor;
class IToolkitHost;
class SCurveEditorPanel;
class UCurveBase;
class USoundModulationPatch;


class FModulationPatchEditor : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient
{
public:
	FModulationPatchEditor();
	virtual ~FModulationPatchEditor() = default;

	void Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundModulationPatch* PatchToEdit);

	/** FAssetEditorToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	/** FNotifyHook interface */
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

protected:
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	void SetCurve(FRichCurve& InRichCurve, EModPatchOutputEditorCurveSource InSource, UCurveFloat* InSharedCurve = nullptr);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_OutputCurve(const FSpawnTabArgs& Args);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

	bool GetIsBypassed() const;

	/** Get the orientation for the snap value controls. */
	EOrientation GetSnapLabelOrientation() const;

	/** Updates patch's output curve. */
	void UpdateCurve();

	/** Trims keys out-of-bounds in provided output transform's curve */
	void TrimKeys(FSoundModulationOutputTransform& OutTransform) const;

	void GenerateExpressionCurve(const FSoundModulationOutputTransform& InTransform, bool bIsUnset = false);

	TSharedPtr<FUICommandList> ToolbarCurveTargetCommands;

	TSharedPtr<FCurveEditor> CurveEditor;
	TSharedPtr<SCurveEditorPanel> CurvePanel;

	TSharedPtr<FRichCurve> ExpressionCurve;

	FCurveModelID CurveModel;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesView;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;
	static const FName CurveTabId;
	static const FName PropertiesTabId;
};

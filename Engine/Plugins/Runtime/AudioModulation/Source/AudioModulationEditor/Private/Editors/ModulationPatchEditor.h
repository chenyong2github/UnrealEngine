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

struct FSoundControlModulationInput;


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
	void SetCurve(int32 InInputIndex, FRichCurve& InRichCurve, EModPatchOutputEditorCurveSource InSource);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_OutputCurve(const FSpawnTabArgs& Args);

	/**	Spawns the tab allowing for editing/viewing the output curve(s) */
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);

	bool GetIsBypassed() const;

	/** Get the orientation for the snap value controls. */
	EOrientation GetSnapLabelOrientation() const;

	/** Updates patch's input curves. */
	void RefreshCurves();

	/** Trims keys out-of-bounds in provided output transform's curve */
	static void TrimKeys(FSoundModulationTransform& OutTransform);

	/** Clears the expression curve at the given input index */
	void ClearExpressionCurve(int32 InInputIndex);

	/** Generates expression curve at the given index. */
	void GenerateExpressionCurve(int32 InInputIndex, EModPatchOutputEditorCurveSource InSource, bool bInIsUnset = false);

	void InitCurves();

	void ResetCurves();

	bool RequiresNewCurve(int32 InInputIndex, const FRichCurve& InRichCurve) const;

	TSharedPtr<FUICommandList> ToolbarCurveTargetCommands;

	TSharedPtr<FCurveEditor> CurveEditor;
	TSharedPtr<SCurveEditorPanel> CurvePanel;

	struct FCurveData
	{
		FCurveModelID ModelID;
		TSharedPtr<FRichCurve> ExpressionCurve;

		FCurveData()
			: ModelID(FCurveModelID::Unique())
		{
		}
	};
	TArray<FCurveData> CurveData;

	/** Properties tab */
	TSharedPtr<IDetailsView> PropertiesView;

	/** Settings Editor App Identifier */
	static const FName AppIdentifier;
	static const FName CurveTabId;
	static const FName PropertiesTabId;
};

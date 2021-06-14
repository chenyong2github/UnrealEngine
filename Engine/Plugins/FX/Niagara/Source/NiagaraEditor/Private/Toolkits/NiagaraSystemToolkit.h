// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"

#include "ISequencer.h"
#include "ISequencerTrackEditor.h"

#include "NiagaraScript.h"
#include "Widgets/SItemSelector.h"
#include "ViewModels/NiagaraSystemGraphSelectionViewModel.h"

class FNiagaraSystemInstance;
class FNiagaraSystemViewModel;
class FNiagaraObjectSelection;
class SNiagaraSystemEditorViewport;
class SNiagaraSystemEditorWidget;
class SNiagaraSystemViewport;
class SNiagaraSystemEditor;
class UNiagaraSystem;
class UNiagaraEmitter;
class UNiagaraSequence;
struct FAssetData;
class FMenuBuilder;
class ISequencer;
class FNiagaraMessageLogViewModel;
class FNiagaraSystemToolkitParameterPanelViewModel;
class FNiagaraSystemToolkitParameterDefinitionsPanelViewModel;
class FNiagaraScriptStatsViewModel;
class FNiagaraBakerViewModel;

/** Viewer/editor for a NiagaraSystem
*/
class FNiagaraSystemToolkit : public FAssetEditorToolkit, public FGCObject, public FEditorUndoClient
{
	enum class ESystemToolkitMode
	{
		System,
		Emitter,
	};

public:
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

	/** Edits the specified Niagara System */
	void InitializeWithSystem(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraSystem& InSystem);

	/** Edits the specified Niagara Emitter */
	void InitializeWithEmitter(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraEmitter& InEmitter);

	/** Destructor */
	virtual ~FNiagaraSystemToolkit();

	//~ Begin IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	//~ End IToolkit Interface

		//~ Begin FEditorUndoClient Interface
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjects) const override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	FSlateIcon GetCompileStatusImage() const;
	FText GetCompileStatusTooltip() const;

	/** Compiles the system script. */
	void CompileSystem(bool bFullRebuild);

	TSharedPtr<FNiagaraSystemViewModel> GetSystemViewModel();

public:
	FRefreshItemSelectorDelegate RefreshItemSelector;

protected:
	void OnToggleBounds();
	bool IsToggleBoundsChecked() const;
	void OnToggleBoundsSetFixedBounds_Emitters();
	void OnToggleBoundsSetFixedBounds_System();

	void ClearStatPerformance();
	void ToggleStatPerformance();
	bool IsStatPerformanceChecked();
	void ToggleStatPerformanceGPU();
	bool IsStatPerformanceGPUChecked();
	void ToggleStatPerformanceTypeAvg();
	void ToggleStatPerformanceTypeMax();
	bool IsStatPerformanceTypeAvg();
	bool IsStatPerformanceTypeMax();
	void ToggleStatPerformanceModePercent();
	void ToggleStatPerformanceModeAbsolute();
	bool IsStatPerformanceModePercent();
	bool IsStatPerformanceModeAbsolute();

	void ToggleDrawOption(int32 Element);
	bool IsDrawOptionEnabled(int32 Element) const;

	void OpenDebugHUD();
	void OpenDebugOutliner();
	void OpenAttributeSpreadsheet();

	//~ FAssetEditorToolkit interface
	virtual void GetSaveableObjects(TArray<UObject*>& OutObjects) const override;
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;
	virtual bool OnRequestClose() override;
	
private:
	void InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid);

	void UpdateOriginalEmitter();
	void UpdateExistingEmitters();

	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_CurveEd(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Sequencer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemScript(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemParameters(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemParameters2(const FSpawnTabArgs& Args); //@todo(ng) cleanup
	TSharedRef<SDockTab> SpawnTab_SystemParameterDefinitions(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SelectedEmitterStack(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SelectedEmitterGraph(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_DebugSpreadsheet(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PreviewSettings(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GeneratedCode(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_MessageLog(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemOverview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScratchPad(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScriptStats(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Baker(const FSpawnTabArgs& Args);

	/** Builds the toolbar widget */
	void ExtendToolbar();	
	void SetupCommands();

	void ResetSimulation();

	void GetSequencerAddMenuContent(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer);
	TSharedRef<SWidget> CreateAddEmitterMenuContent();
	TSharedRef<SWidget> GenerateCompileMenuContent();

	void EmitterAssetSelected(const FAssetData& AssetData);

	static void ToggleCompileEnabled();
	static bool IsAutoCompileEnabled();
	
	void OnApply();
	bool OnApplyEnabled() const;

	void OnApplyScratchPadChanges();
	bool OnApplyScratchPadChangesEnabled() const;

	void OnPinnedCurvesChanged();
	void RefreshParameters();
	void OnSystemSelectionChanged();
	void OnViewModelRequestFocusTab(FName TabName);

	void RenderBaker();

	TSharedRef<SWidget> GenerateBoundsMenuContent(TSharedRef<FUICommandList> InCommandList);
	TSharedRef<SWidget> GenerateStatConfigMenuContent(TSharedRef<FUICommandList> InCommandList);
	const FName GetNiagaraSystemMessageLogName(UNiagaraSystem* InSystem) const;
	void OnSaveThumbnailImage();
	void OnThumbnailCaptured(UTexture2D* Thumbnail);

private:

	/** The System being edited in system mode, or the placeholder system being edited in emitter mode. */
	UNiagaraSystem* System;

	/** The emitter being edited in emitter mode, or null when editing in system mode. */
	UNiagaraEmitter* Emitter;

	/** The value of the emitter change id from the last time it was in sync with the original emitter. */
	FGuid LastSyncedEmitterChangeId;

	/** The graphs being undone/redone currently so we only mark for compile the right ones */
	mutable TArray<TWeakObjectPtr<UNiagaraGraph>> LastUndoGraphs;

	/** Whether or not the emitter thumbnail has been updated.  The is needed because after the first update the
		screenshot uobject is reused, so a pointer comparison doesn't work to checking if the images has been updated. */
	bool bEmitterThumbnailUpdated;

	ESystemToolkitMode SystemToolkitMode;

	TSharedPtr<SNiagaraSystemViewport> Viewport;

	/* The view model for the System being edited */
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;

	/* The view model for the selected Emitter Script graphs of the System being edited. */
	TSharedPtr<FNiagaraSystemGraphSelectionViewModel> SystemGraphSelectionViewModel;

	/** Message log, with the log listing that it reflects */
	TSharedPtr<FNiagaraMessageLogViewModel> NiagaraMessageLogViewModel;
	TSharedPtr<class SWidget> NiagaraMessageLog;

	/** Display for script stats on selected platforms */
	TSharedPtr<FNiagaraScriptStatsViewModel> ScriptStats;

	/** Baker preview */
	TSharedPtr<FNiagaraBakerViewModel> BakerViewModel;

	/** The command list for this editor */
	TSharedPtr<FUICommandList> EditorCommands;

	TSharedPtr<class SNiagaraParameterMapView> ParameterMapView;

	TSharedPtr<FNiagaraSystemToolkitParameterPanelViewModel> ParameterPanelViewModel;
	TSharedPtr<FNiagaraSystemToolkitParameterDefinitionsPanelViewModel> ParameterDefinitionsPanelViewModel;
	TSharedPtr<class SNiagaraParameterPanel> ParameterPanel;

	TSharedPtr<FNiagaraObjectSelection> ObjectSelectionForParameterMapView;

	bool bChangesDiscarded;

	bool bScratchPadChangesDiscarded;

	static IConsoleVariable* VmStatEnabledVar;
	static IConsoleVariable* GpuStatEnabledVar;

public:
	static const FName ViewportTabID;
	static const FName CurveEditorTabID;
	static const FName SequencerTabID;
	static const FName SystemScriptTabID;
	static const FName SystemDetailsTabID;
	static const FName SystemParametersTabID;
	static const FName SystemParametersTabID2;
	static const FName SystemParameterDefinitionsTabID;
	static const FName SelectedEmitterStackTabID;
	static const FName SelectedEmitterGraphTabID;
	static const FName DebugSpreadsheetTabID;
	static const FName PreviewSettingsTabId;
	static const FName GeneratedCodeTabID;
	static const FName MessageLogTabID;
	static const FName SystemOverviewTabID;
	static const FName ScratchPadTabID;
	static const FName ScriptStatsTabID;
	static const FName BakerTabID;
};

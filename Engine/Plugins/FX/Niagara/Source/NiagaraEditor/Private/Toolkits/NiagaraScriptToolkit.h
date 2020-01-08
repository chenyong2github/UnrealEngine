// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Toolkits/IToolkitHost.h"
#include "Misc/NotifyHook.h"
#include "EditorUndoClient.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

#include "NiagaraEditorCommon.h"

class IDetailsView;
class SGraphEditor;
class UEdGraph;
class UNiagaraScript;
class UNiagaraScriptSource;
class FNiagaraScriptViewModel;
class FNiagaraObjectSelection;
struct FEdGraphEditAction;
class FNiagaraMessageLogViewModel;
class FNiagaraStandaloneScriptViewModel;

/** Viewer/editor for a DataTable */
class FNiagaraScriptToolkit : public FAssetEditorToolkit, public FGCObject
{
public:
	FNiagaraScriptToolkit();

	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

	/** Edits the specified Niagara Script */
	void Initialize( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraScript* Script );

	/** Destructor */
	virtual ~FNiagaraScriptToolkit();

	//~ Begin IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	//~ End IToolkit Interface

	/** The original NiagaraScript being edited by this editor.. */
	UNiagaraScript* OriginalNiagaraScript;

	/** The transient, duplicated script that is being edited by this editor.*/
	UNiagaraScript* EditedNiagaraScript;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/**
	* Updates list of module info used to show stats
	*/
	void UpdateModuleStats();

protected:
	//~ FAssetEditorToolkit interface
	virtual void GetSaveableObjects(TArray<UObject*>& OutObjects) const override;
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;
	virtual bool OnRequestClose() override;

private:
	void OnEditedScriptPropertyFinishedChanging(const FPropertyChangedEvent& InEvent);

	void OnVMScriptCompiled(UNiagaraScript* InScript);

	/** Spawns the tab with the update graph inside */
	TSharedRef<SDockTab> SpawnTabNodeGraph(const FSpawnTabArgs& Args);

	/** Spawns the tab with the script details inside. */
	TSharedRef<SDockTab> SpawnTabScriptDetails(const FSpawnTabArgs& Args);

	/** Spawns the tab with the details of the current selection. */
	TSharedRef<SDockTab> SpawnTabSelectedDetails(const FSpawnTabArgs& Args);

	/** Spawns the tab with the script details inside. */
	TSharedRef<SDockTab> SpawnTabScriptParameters(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTabStats(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTabMessageLog(const FSpawnTabArgs& Args);

	/** Sets up commands for the toolkit toolbar. */
	void SetupCommands();

	const FName GetNiagaraScriptMessageLogName(UNiagaraScript* InScript) const;
	FSlateIcon GetCompileStatusImage() const;
	FText GetCompileStatusTooltip() const;

	/** Builds the toolbar widget */
	void ExtendToolbar();

	/** Compiles the script. */
	void CompileScript(bool bForce);

	/** Refreshes the nodes in the script graph, updating the pins to match external changes. */
	void RefreshNodes();

	FSlateIcon GetRefreshStatusImage() const;
	FText GetRefreshStatusTooltip() const;
	
	bool IsEditScriptDifferentFromOriginalScript() const;

	/** Command for the apply button */
	void OnApply();
	bool OnApplyEnabled() const;

	void UpdateOriginalNiagaraScript();

	void OnEditedScriptGraphChanged(const FEdGraphEditAction& InAction);

	/** Navigates to element in graph (node, pin, etc.) 
	* @Param ElementToFocus Defines the graph element to navigate to and select.
	*/
	void FocusGraphElementIfSameScriptID(const FNiagaraScriptIDAndGraphFocusInfo* ElementToFocus);

private:

	/** The Script being edited */
	TSharedPtr<FNiagaraStandaloneScriptViewModel> ScriptViewModel;

	/** The selection displayed by the details tab. */
	TSharedPtr<FNiagaraObjectSelection> DetailsScriptSelection;

	/** Message log, with the log listing that it reflects */
	TSharedPtr<FNiagaraMessageLogViewModel> NiagaraMessageLogViewModel;
	TSharedPtr<class SWidget> NiagaraMessageLog;

	/**	The tab ids for the Niagara editor */
	static const FName NodeGraphTabId; 
	static const FName ScriptDetailsTabId;
	static const FName SelectedDetailsTabId;
	static const FName ParametersTabId;
	static const FName StatsTabId;
	static const FName MessageLogTabID;

	/** Stats log, with the log listing that it reflects */
	TSharedPtr<class SWidget> Stats;
	TSharedPtr<class IMessageLogListing> StatsListing;

	FDelegateHandle OnEditedScriptGraphChangedHandle;

	bool bEditedScriptHasPendingChanges;
	bool bChangesDiscarded;

	TSharedPtr<class SNiagaraScriptGraph> NiagaraScriptGraphWidget;
};

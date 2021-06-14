// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/NiagaraScriptViewModel.h"
#include "GraphEditAction.h"

class INiagaraParameterPanelViewModel;
class FNiagaraScriptToolkitParameterPanelViewModel;
class FUICommandList;


class NIAGARAEDITOR_API FNiagaraScratchPadScriptViewModel : public FNiagaraScriptViewModel, public FGCObject
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnRenamed);
	DECLARE_MULTICAST_DELEGATE(FOnPinnedChanged);
	DECLARE_MULTICAST_DELEGATE(FOnHasUnappliedChangesChanged);
	DECLARE_MULTICAST_DELEGATE(FOnChangesApplied);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNodeIDFocusRequested, FNiagaraScriptIDAndGraphFocusInfo*  /* FocusInfo */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPinIDFocusRequested, FNiagaraScriptIDAndGraphFocusInfo*  /* FocusInfo */);

	FNiagaraScratchPadScriptViewModel(bool bInIsForDataProcessingOnly);

	~FNiagaraScratchPadScriptViewModel();

	void Initialize(UNiagaraScript* Script);

	void Finalize();

	//~ Begin FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject

	//~ Begin NiagaraParameterDefinitionsSubscriberViewModel Interface
protected:
	virtual INiagaraParameterDefinitionsSubscriber* GetParameterDefinitionsSubscriber() override { return &EditScript; };
	//~ End NiagaraParameterDefinitionsSubscriberViewModel Interface

public:

	UNiagaraScript* GetOriginalScript() const;

	const FVersionedNiagaraScript& GetEditScript() const;

	TSharedPtr<INiagaraParameterPanelViewModel> GetParameterPanelViewModel() const;

	TSharedPtr<FUICommandList> GetParameterPanelCommands() const;

	FText GetToolTip() const;

	bool GetIsPendingRename() const;

	void SetIsPendingRename(bool bInIsPendingRename);

	void SetScriptName(FText InScriptName);

	bool GetIsPinned() const;

	void SetIsPinned(bool bInIsPinned);

	float GetEditorHeight() const;

	void SetEditorHeight(float InEditorHeight);

	bool HasUnappliedChanges() const;

	void ApplyChanges();

	void DiscardChanges();

	FOnRenamed& OnRenamed();

	FOnPinnedChanged& OnPinnedChanged();

	FOnHasUnappliedChangesChanged& OnHasUnappliedChangesChanged();

	FOnChangesApplied& OnChangesApplied();

	FSimpleDelegate& OnRequestDiscardChanges();

	FOnNodeIDFocusRequested& OnNodeIDFocusRequested();
	FOnPinIDFocusRequested& OnPinIDFocusRequested();

	void RaisePinFocusRequested(FNiagaraScriptIDAndGraphFocusInfo*  InFocusInfo);
	void RaiseNodeFocusRequested(FNiagaraScriptIDAndGraphFocusInfo* InFocusInfo);

private:
	FText GetDisplayNameInternal() const;

	void OnScriptGraphChanged(const FEdGraphEditAction &Action);

	void OnScriptPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent);

	bool bIsPendingRename;
	bool bIsPinned;
	float EditorHeight;

	UNiagaraScript* OriginalScript = nullptr;
	FVersionedNiagaraScript EditScript;

	bool bHasPendingChanges;

	TSharedPtr<FUICommandList> ParameterPanelCommands;
	TSharedPtr<FNiagaraScriptToolkitParameterPanelViewModel> ParameterPaneViewModel;

	FDelegateHandle OnGraphNeedsRecompileHandle;

	FOnRenamed OnRenamedDelegate;
	FOnPinnedChanged OnPinnedChangedDelegate;
	FOnHasUnappliedChangesChanged OnHasUnappliedChangesChangedDelegate;
	FOnChangesApplied OnChangesAppliedDelegate;
	FSimpleDelegate	OnRequestDiscardChangesDelegate;

	FOnNodeIDFocusRequested OnNodeIDFocusRequestedDelegate;
	FOnPinIDFocusRequested OnPinIDFocusRequestedDelegate;
};
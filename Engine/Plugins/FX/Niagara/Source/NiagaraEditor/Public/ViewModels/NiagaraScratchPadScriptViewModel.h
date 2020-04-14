// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/NiagaraScriptViewModel.h"
#include "GraphEditAction.h"

class INiagaraParameterPanelViewModel;
class FUICommandList;

class NIAGARAEDITOR_API FNiagaraScratchPadScriptViewModel : public FNiagaraScriptViewModel, public FGCObject
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnRenamed);
	DECLARE_MULTICAST_DELEGATE(FOnPinnedChanged);

public:
	FNiagaraScratchPadScriptViewModel();

	~FNiagaraScratchPadScriptViewModel();

	void Initialize(UNiagaraScript* Script);

	void Finalize();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	UNiagaraScript* GetOriginalScript() const;

	UNiagaraScript* GetEditScript() const;

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

	FSimpleDelegate& OnRequestDiscardChanges();

private:
	FText GetDisplayNameInternal() const;

	void OnScriptGraphChanged(const FEdGraphEditAction &Action);

	void OnScriptPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent);

private:
	bool bIsPendingRename;

	bool bIsPinned;

	float EditorHeight;

	UNiagaraScript* OriginalScript;

	UNiagaraScript* EditScript;

	bool bHasPendingChanges;

	TSharedPtr<FUICommandList> ParameterPanelCommands;
	TSharedPtr<INiagaraParameterPanelViewModel> ParameterPaneViewModel;

	FDelegateHandle OnGraphNeedsRecompileHandle;

	FOnRenamed OnRenamedDelegate;
	FOnPinnedChanged OnPinnedChangedDelegate;
	FSimpleDelegate OnRequestDiscardChangesDelegate;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/NiagaraScriptViewModel.h"
#include "GraphEditAction.h"

class NIAGARAEDITOR_API FNiagaraScratchPadScriptViewModel : public FNiagaraScriptViewModel, public FGCObject
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnRenamed);
	DECLARE_MULTICAST_DELEGATE(FOnPinnedChanged);

public:
	FNiagaraScratchPadScriptViewModel();

	~FNiagaraScratchPadScriptViewModel();

	void Initialize(UNiagaraScript* Script);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	UNiagaraScript* GetOriginalScript() const;

	UNiagaraScript* GetEditScript() const;

	FText GetToolTip() const;

	bool GetIsPendingRename() const;

	void SetIsPendingRename(bool bInIsPendingRename);

	void SetScriptName(FText InScriptName);

	bool GetIsPinned() const;

	void SetIsPinned(bool bInIsPinned);

	float GetEditorHeight() const;

	void SetEditorHeight(float InEditorHeight);

	bool CanApplyChanges() const;

	void ApplyChanges();

	FOnRenamed& OnRenamed();

	FOnPinnedChanged& OnPinnedChanged();

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

	FDelegateHandle OnGraphNeedsRecompileHandle;

	FOnRenamed OnRenamedDelegate;
	FOnPinnedChanged OnPinnedChangedDelegate;
};
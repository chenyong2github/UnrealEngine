// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraScriptHighlight.h"
#include "Layout/Visibility.h"
#include "NiagaraStackItem.generated.h"

class UNiagaraStackItemFooter;
class UNiagaraNode;
class UNiagaraClipboardContent;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackItem : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnModifiedGroupItems);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnRequestCanPaste, const UNiagaraClipboardContent* /* ClipboardContent */, FText& /* OutCanPasteMessage */);
	DECLARE_DELEGATE_ThreeParams(FOnRequestPaste, const UNiagaraClipboardContent* /* ClipboardContent */, int32 /* PasteIndex */, FText& /* OutPasteWarning */);

public:
	void Initialize(FRequiredEntryData InRequiredEntryData, FString InStackEditorDataKey);

	virtual EStackRowStyle GetStackRowStyle() const override;

	FOnModifiedGroupItems& OnModifiedGroupItems();

	void SetOnRequestCanPaste(FOnRequestCanPaste InOnRequestCanPaste);
	void SetOnRequestPaste(FOnRequestPaste InOnRequestCanPaste);

	virtual bool SupportsChangeEnabled() const { return false; }
	void SetIsEnabled(bool bInIsEnabled);

	virtual bool SupportsHighlights() const { return false; }
	virtual const TArray<FNiagaraScriptHighlight>& GetHighlights() const;

	virtual bool SupportsIcon() const { return false; }
	virtual const FSlateBrush* GetIconBrush() const;

	virtual bool SupportsResetToBase() const { return false; }
	virtual bool TestCanResetToBaseWithMessage(FText& OutCanResetToBaseMessage) const { return false; }
	virtual void ResetToBase() { }
	
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void PostRefreshChildrenInternal() override;

	virtual int32 GetChildIndentLevel() const override;

	virtual void SetIsEnabledInternal(bool bInIsEnabled) { }

private:
	bool FilterAdvancedChildren(const UNiagaraStackEntry& Child) const;

	void ToggleShowAdvanced();

protected:
	FOnModifiedGroupItems ModifiedGroupItemsDelegate;
	FOnRequestCanPaste RequestCanPasteDelegete;
	FOnRequestPaste RequestPasteDelegate;

private:
	UPROPERTY()
	UNiagaraStackItemFooter* ItemFooter;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackItemContent : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData, bool bInIsAdvanced, FString InOwningStackItemEditorDataKey, FString InStackEditorDataKey);

	virtual EStackRowStyle GetStackRowStyle() const override;

	bool GetIsAdvanced() const;

protected:
	FString GetOwnerStackItemEditorDataKey() const;

	void SetIsAdvanced(bool bInIsAdvanced);

private:
	bool FilterAdvancedChildren(const UNiagaraStackEntry& Child) const;

private:
	FString OwningStackItemEditorDataKey;
	bool bIsAdvanced;
};

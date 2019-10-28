// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "Layout/Visibility.h"
#include "NiagaraStackItem.generated.h"

class UNiagaraStackItemFooter;
class UNiagaraNode;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackItem : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnModifiedGroupItems);

public:
	void Initialize(FRequiredEntryData InRequiredEntryData, FString InStackEditorDataKey);

	virtual EStackRowStyle GetStackRowStyle() const override;

	FOnModifiedGroupItems& OnModifiedGroupItems();

	virtual bool SupportsChangeEnabled() const { return false; }
	void SetIsEnabled(bool bInIsEnabled);

	virtual bool SupportsDelete() const { return false; }
	virtual bool TestCanDeleteWithMessage(FText& OutCanDeleteMessage) const { return false; }
	void Delete();
	
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;

	virtual void PostRefreshChildrenInternal() override;

	virtual int32 GetChildIndentLevel() const override;

	virtual void SetIsEnabledInternal(bool bInIsEnabled) { }
	virtual void DeleteInternal() { }

private:
	bool FilterAdvancedChildren(const UNiagaraStackEntry& Child) const;

	void ToggleShowAdvanced();

protected:
	FOnModifiedGroupItems ModifiedGroupItemsDelegate;

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

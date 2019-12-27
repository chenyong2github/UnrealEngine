// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "NiagaraStackRenderItemGroup.generated.h"

class UNiagaraRendererProperties;
class UNiagaraEmitter;
class UNiagaraClipboardContent;

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackRenderItemGroup : public UNiagaraStackItemGroup
{
	GENERATED_BODY()

public:
	void Initialize(FRequiredEntryData InRequiredEntryData);

	virtual bool SupportsPaste() const override { return true; }
	virtual bool TestCanPasteWithMessage(const UNiagaraClipboardContent* ClipboardContent, FText& OutMessage) const override;
	virtual FText GetPasteTransactionText(const UNiagaraClipboardContent* ClipboardContent) const override;
	virtual void Paste(const UNiagaraClipboardContent* ClipboardContent) override;

protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
	virtual void FinalizeInternal() override;
private:
	void EmitterRenderersChanged();

	void ChildRequestPaste(const UNiagaraClipboardContent* ClipboardContent, int32 PasteIndex);

private:
	TSharedPtr<INiagaraStackItemGroupAddUtilities> AddUtilities;

	TWeakObjectPtr<UNiagaraEmitter> EmitterWeak;
};

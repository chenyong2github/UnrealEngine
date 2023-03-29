// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphNode_Comment.h"
#include "NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraStackCommentCollection.generated.h"

UCLASS()
class NIAGARAEDITOR_API UNiagaraStackCommentCollection : public UNiagaraStackEntry
{
	GENERATED_BODY()

public:
	UNiagaraStackCommentCollection() {}

	void Initialize(FRequiredEntryData InRequiredEntryData);

	virtual bool GetShouldShowInOverview() const override { return false; }
	virtual bool GetShouldShowInStack() const override { return false; }

	UNiagaraStackObject* FindStackObjectForCommentNode(UEdGraphNode_Comment* CommentNode) const;
	
protected:
	virtual void RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues) override;
};
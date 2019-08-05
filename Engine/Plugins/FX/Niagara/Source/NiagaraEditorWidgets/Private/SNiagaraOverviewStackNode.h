// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"

class UNiagaraOverviewNode;
class UNiagaraStackViewModel;
class UNiagaraSystemSelectionViewModel;

class SNiagaraOverviewStackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewStackNode) {}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode);

protected:
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;

private:

private:
	UNiagaraOverviewNode* OverviewStackNode;
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;
};
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"

class UNiagaraOverviewNode;
class UNiagaraStackViewModel;
class UNiagaraSystemSelectionViewModel;
class FNiagaraEmitterHandleViewModel;

class SNiagaraOverviewStackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewStackNode) {}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UNiagaraOverviewNode* InNode);

protected:
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;

private:
	EVisibility GetIssueIconVisibility() const;
	EVisibility GetEnabledCheckBoxVisibility() const;
	ECheckBoxState GetEnabledCheckState() const;
	void OnEnabledCheckStateChanged(ECheckBoxState InCheckState);

private:
	UNiagaraOverviewNode* OverviewStackNode;
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraSystemSelectionViewModel* OverviewSelectionViewModel;
	TWeakPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModelWeak;
};
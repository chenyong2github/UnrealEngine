// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraSystemScalabilityViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
//#include "SNiagaraOverviewGraph.h"

class SNiagaraOverviewGraph;

class SNiagaraOverviewGraphTitleBar : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraOverviewGraphTitleBar)
		: _OverviewGraph(nullptr)
	{}
		SLATE_ARGUMENT(TSharedPtr<SNiagaraOverviewGraph>, OverviewGraph)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

	void RebuildWidget();
	//virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
private:
	TWeakPtr<FNiagaraSystemViewModel> SystemViewModel;
	bool bScalabilityModeActive = false;
	TSharedPtr<SVerticalBox> ContainerWidget;
	TWeakPtr<SNiagaraOverviewGraph> OverviewGraph;
private:
	void OnUpdateScalabilityMode(bool bActive);
};

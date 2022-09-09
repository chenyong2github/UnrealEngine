// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SNiagaraParameterMenu.h"

class SNiagaraSystemUserParameters : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnThumbnailCaptured, UTexture2D*);

public:
	SLATE_BEGIN_ARGS( SNiagaraSystemUserParameters ){}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TSharedPtr<FNiagaraSystemViewModel> InSystemViewModel);
	virtual ~SNiagaraSystemUserParameters() override {}
	
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override {}

private:
	TSharedRef<SWidget> GetParameterMenu();
	
	void AddParameter(FNiagaraVariable NewParameter) const;
	bool CanMakeNewParameterOfType(const FNiagaraTypeDefinition& InType) const;

	FReply SummonHierarchyEditor();
private:
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	
	TSharedPtr<SComboButton> AddParameterButton;
	TSharedPtr<SNiagaraAddParameterFromPanelMenu> ParameterPanel;
};

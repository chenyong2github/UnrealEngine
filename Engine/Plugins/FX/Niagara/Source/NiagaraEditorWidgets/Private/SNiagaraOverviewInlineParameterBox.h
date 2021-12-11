// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Brushes/SlateImageBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"

class SWrapBox;
class SScrollBox;
class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class SBox;
class SInlineEditableTextBlock;

class SNiagaraOverviewInlineParameterBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewInlineParameterBox)
	{
		_Clipping = EWidgetClipping::OnDemand;
	}
	
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InStackModuleItem);

	virtual ~SNiagaraOverviewInlineParameterBox() override;

	int32 GetNumChildWidgets();

private:
	/** Selects the module item in the stack and searches for the function input */
	FReply NavigateToStack(TWeakObjectPtr<UNiagaraStackFunctionInput> FunctionInput);
	void ConstructChildren();
	TArray<TSharedRef<SWidget>> GenerateParameterWidgets();
	TSharedRef<SWidget> GenerateParameterWidgetFromLocalValue(UNiagaraStackFunctionInput* FunctionInput);
	TSharedRef<SWidget> GenerateParameterWidgetFromDataInterface(UNiagaraStackFunctionInput* FunctionInput);

private:
	/** The module item whose parameters this box is representing */
	TWeakObjectPtr<UNiagaraStackModuleItem> ModuleItem;
	
	TSharedPtr<SScrollBox> Container;
	
	/** We keep track of the function inputs we are observing through this parameter box so we can unbind the delegates later */
	TArray<TWeakObjectPtr<UNiagaraStackFunctionInput>> BoundFunctionInputs;

	TArray<FSlateBrush> ImageBrushes;

	int32 NumParameterWidgets = 0;
};

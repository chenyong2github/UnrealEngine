// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "NiagaraActions.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SCheckBox.h"

class NIAGARAEDITOR_API SNiagaraSourceFilterButton : public SCheckBox
{
	typedef TMap<EScriptSource, bool> SourceStates;
	DECLARE_DELEGATE_TwoParams(FOnSourceStateChanged, EScriptSource, bool);
	DECLARE_DELEGATE_TwoParams(FOnShiftClicked, EScriptSource, bool);
	
	SLATE_BEGIN_ARGS( SNiagaraSourceFilterButton )
	{}
		SLATE_EVENT(FOnSourceStateChanged, OnSourceStateChanged)
		SLATE_EVENT(FOnShiftClicked, OnShiftClicked)
		SLATE_ATTRIBUTE(ECheckBoxState, IsChecked)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, EScriptSource Source);

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
private:	
	FOnSourceStateChanged OnSourceStateChanged;
	FOnShiftClicked OnShiftClicked;
	EScriptSource Source;
private:

	FSlateColor GetColor() const;
	FSlateColor GetBackgroundColor() const;
};

class NIAGARAEDITOR_API SNiagaraSourceFilterBox : public SBorder
{
public:
	typedef TMap<EScriptSource, bool> SourceMap;
	DECLARE_DELEGATE_OneParam(FOnFiltersChanged, const SourceMap&);
	
	SLATE_BEGIN_ARGS( SNiagaraSourceFilterBox )
	{}
		SLATE_EVENT(FOnFiltersChanged, OnFiltersChanged)
	
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	bool IsFilterActive(EScriptSource Source) const;

private:
	TMap<EScriptSource, TSharedRef<SNiagaraSourceFilterButton>> SourceButtons;
	static TMap<EScriptSource, bool> SourceState;
	
	void BroadcastFiltersChanged() const
	{
		OnFiltersChanged.Execute(SourceState);
	}

	ECheckBoxState OnIsFilterActive(EScriptSource Source) const;

private:
	FOnFiltersChanged OnFiltersChanged;
};
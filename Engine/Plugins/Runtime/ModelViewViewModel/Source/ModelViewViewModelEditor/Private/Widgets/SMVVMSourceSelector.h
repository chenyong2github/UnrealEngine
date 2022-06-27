// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/MVVMBindingSource.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SMVVMSourceEntry;

class SMVVMSourceSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FSelectionChanged, UE::MVVM::FBindingSource);

	SLATE_BEGIN_ARGS(SMVVMSourceSelector) :
		_TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(UE::MVVM::FBindingSource, SelectedSource)
		SLATE_ATTRIBUTE(TArray<UE::MVVM::FBindingSource>, AvailableSources)
		SLATE_EVENT(FSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

private:
	void OnComboBoxSelectionChanged(UE::MVVM::FBindingSource Selected, ESelectInfo::Type SelectionType);

	EVisibility GetClearVisibility() const;
	FReply OnClearSource();

private:
	FSelectionChanged OnSelectionChanged;
	TAttribute<TArray<UE::MVVM::FBindingSource>> AvailableSourcesAttribute;
	TAttribute< UE::MVVM::FBindingSource> SelectedSourceAttribute;
	TSharedPtr<SComboBox<UE::MVVM::FBindingSource>> SourceComboBox;
	TArray<UE::MVVM::FBindingSource> AvailableSources;
	UE::MVVM::FBindingSource SelectedSource;
	TSharedPtr<SMVVMSourceEntry> SelectedSourceWidget;
	const FTextBlockStyle* TextStyle = nullptr;
};


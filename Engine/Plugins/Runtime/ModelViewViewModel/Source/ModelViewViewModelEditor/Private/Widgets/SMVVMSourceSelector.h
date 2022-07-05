// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/MVVMBindingSource.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

namespace UE::MVVM
{

class SSourceEntry;

class SSourceSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FSelectionChanged, FBindingSource);

	SLATE_BEGIN_ARGS(SSourceSelector) :
		_TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ATTRIBUTE(FBindingSource, SelectedSource)
		SLATE_ATTRIBUTE(TArray<FBindingSource>, AvailableSources)
		SLATE_EVENT(FSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh();

private:
	void OnComboBoxSelectionChanged(FBindingSource Selected, ESelectInfo::Type SelectionType);

	EVisibility GetClearVisibility() const;
	FReply OnClearSource();

private:
	FSelectionChanged OnSelectionChanged;
	TAttribute<TArray<FBindingSource>> AvailableSourcesAttribute;
	TAttribute<FBindingSource> SelectedSourceAttribute;
	TSharedPtr<SComboBox<FBindingSource>> SourceComboBox;
	TArray<FBindingSource> AvailableSources;
	FBindingSource SelectedSource;
	TSharedPtr<SSourceEntry> SelectedSourceWidget;
	const FTextBlockStyle* TextStyle = nullptr;
};

} // namespace UE::MVVM

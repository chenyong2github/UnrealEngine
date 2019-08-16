// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepFilter.h"

#include "DataprepEditorStyle.h"
#include "DataprepEditorUtils.h"
#include "SelectionSystem/DataprepBoolFilter.h"
#include "SelectionSystem/DataprepFilter.h"
#include "SelectionSystem/DataprepFloatFilter.h"
#include "SelectionSystem/DataprepStringFilter.h"
#include "Widgets/Action/SDataprepBoolFilter.h"
#include "Widgets/Action/SDataprepFloatFilter.h"
#include "Widgets/Action/SDataprepStringFilter.h"
#include "Widgets/DataprepWidgets.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SDataprepFilter"

void SDataprepFilter::Construct(const FArguments& InArgs, UDataprepFilter& InFilter, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext)
{
	Filter = &InFilter;
	SDataprepActionBlock::Construct( SDataprepActionBlock::FArguments(), InDataprepActionContext );
}

FText SDataprepFilter::GetBlockTitle() const
{
	if ( Filter )
	{
		UClass* Class = Filter->GetClass();
		if ( Class == UDataprepStringFilter::StaticClass() )
		{
			if ( Filter->IsExcludingResult() )
			{
				static FText StringExcludingFilterTitle =  LOCTEXT("StringExcludingFilterTitle", "Exclude by String");
				return StringExcludingFilterTitle;
			}
			else
			{
				static FText StringFilterTitle = LOCTEXT("StringFilterTitle", "Select by String");
				return StringFilterTitle;
			}
		}
		else if ( Class == UDataprepBoolFilter::StaticClass() )
		{
			if ( Filter->IsExcludingResult() )
			{
				static FText BoolExcludingFilterTitle = LOCTEXT("BoolExcludingFilterTitle", "Exclude by Condition");
				return BoolExcludingFilterTitle;
			}
			else
			{
				static FText BoolFilterTitle = LOCTEXT("BoolFilterTitle", "Select by Condition");
				return BoolFilterTitle;
			}
		}
		else if ( Class == UDataprepFloatFilter::StaticClass() )
		{
			if ( Filter->IsExcludingResult() )
			{
				static FText FloatExcludingFilterTitle = LOCTEXT("FloatExcludingFilterTitle", "Exclude by Float");
				return FloatExcludingFilterTitle;
			}
			else
			{
				static FText FloatFilterTitle = LOCTEXT("FloatFilterTitle", "Select by Float");
				return FloatFilterTitle;
			}
		}
	}
	return LOCTEXT("DefaultFilterTitle", "Unknow Filter Type");
}

TSharedRef<SWidget> SDataprepFilter::GetTitleWidget() const
{
	const ISlateStyle* DataprepEditorStyle = FSlateStyleRegistry::FindSlateStyle( FDataprepEditorStyle::GetStyleSetName() );
	check( DataprepEditorStyle );
	const float DefaultPadding = DataprepEditorStyle->GetFloat( "DataprepAction.Padding" );

	return SNew( STextBlock )
		.Text( this, &SDataprepFilter::GetBlockTitle )
		.TextStyle( &DataprepEditorStyle->GetWidgetStyle<FTextBlockStyle>( "DataprepActionBlock.TitleTextBlockStyle" ) )
		.ColorAndOpacity( FLinearColor( 1.f, 1.f, 1.f ) )
		.Margin( FMargin( DefaultPadding ) )
		.Justification( ETextJustify::Center );
}

TSharedRef<SWidget> SDataprepFilter::GetContentWidget() const
{
	TSharedPtr< SWidget > FilterWidget;

	if ( Filter )
	{
		UClass* Class = Filter->GetClass();
		// This down casting implementation is faster then using Cast<UDataprepStringFilter>( Filter ) 
		if ( Class ==  UDataprepStringFilter::StaticClass() )
		{
			SAssignNew( FilterWidget, SDataprepStringFilter, *static_cast< UDataprepStringFilter* >( Filter ) );
		}
		else if ( Class == UDataprepBoolFilter::StaticClass() )
		{
			SAssignNew( FilterWidget, SDataprepBoolFilter, *static_cast< UDataprepBoolFilter* >( Filter ) );
		}
		else if (Class == UDataprepFloatFilter::StaticClass())
		{
			SAssignNew( FilterWidget, SDataprepFloatFilter, *static_cast< UDataprepFloatFilter* >( Filter ) );
		}
	}
	else
	{
		FilterWidget = SNullWidget::NullWidget;
	}

	return SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			FilterWidget.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SDataprepDetailsView )
			.Object_Lambda( [Filter = Filter]()
				{
					return Filter->GetFetcher();
				})
			.Class( UDataprepFilter::StaticClass() )
		];
}

void SDataprepFilter::PopulateMenuBuilder(FMenuBuilder& MenuBuilder) const
{
	SDataprepActionBlock::PopulateMenuBuilder( MenuBuilder );

	MenuBuilder.BeginSection( FName( TEXT("FilterSection") ), LOCTEXT("FilterSection", "Filter") );
	{
		FUIAction InverseFilterAction;
		InverseFilterAction.ExecuteAction.BindSP( this, &SDataprepFilter::InverseFilter );
		MenuBuilder.AddMenuEntry( LOCTEXT("InverseFilter", "Inverse Selection"), 
			LOCTEXT("InverseFilterTooltip", "Inverse the resulting selection"),
			FSlateIcon(),
			InverseFilterAction );
	}
	MenuBuilder.EndSection();
}

void SDataprepFilter::InverseFilter()
{
	if ( Filter )
	{
		FScopedTransaction Transaction( LOCTEXT("InverseFilterTransaction", "Inverse the filter") );
		Filter->SetIsExcludingResult( !Filter->IsExcludingResult() );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
	}
}

void SDataprepFilter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Filter );
}

#undef LOCTEXT_NAMESPACE


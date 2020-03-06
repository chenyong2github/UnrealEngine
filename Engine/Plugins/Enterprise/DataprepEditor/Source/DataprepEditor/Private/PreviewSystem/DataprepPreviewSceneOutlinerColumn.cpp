// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewSystem/DataprepPreviewSceneOutlinerColumn.h"

#include "PreviewSystem/DataprepPreviewSystem.h"
#include "Widgets/SDataprepPreviewRow.h"

#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"
#include "ISceneOutliner.h"
#include "SceneOutlinerVisitorTypes.h"
#include "SceneOutlinerVisitorTypes.h"
#include "SubComponentTreeItem.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "DataprepPreviewOutlinerColumn"

namespace DataprepPreviewOutlinerColumnUtils
{
	struct FObjectGetter : public SceneOutliner::TTreeItemGetter<TWeakObjectPtr<UObject>>
	{
		virtual TWeakObjectPtr<UObject> Get(const SceneOutliner::FActorTreeItem& ActorItem) const override { return ActorItem.Actor; };
		virtual TWeakObjectPtr<UObject> Get(const SceneOutliner::FWorldTreeItem& WorldItem) const override { return nullptr; }
		virtual TWeakObjectPtr<UObject> Get(const SceneOutliner::FFolderTreeItem& FolderItem) const override { return nullptr; }
		virtual TWeakObjectPtr<UObject> Get(const SceneOutliner::FComponentTreeItem& ComponentItem) const override { return ComponentItem.Component; }
		virtual TWeakObjectPtr<UObject> Get(const SceneOutliner::FSubComponentTreeItem& SubComponentItem) const override { return SubComponentItem.ParentComponent; }
	};
}

const FName FDataprepPreviewOutlinerColumn::ColumnID = FName( TEXT("DataprepPreview") );

FDataprepPreviewOutlinerColumn::FDataprepPreviewOutlinerColumn(ISceneOutliner& SceneOutliner, const TSharedRef<FDataprepPreviewSystem>& PreviewData)
	: ISceneOutlinerColumn()
	, WeakSceneOutliner( StaticCastSharedRef<ISceneOutliner>( SceneOutliner.AsShared() ) )
	, CachedPreviewData( PreviewData )
{
}

FName FDataprepPreviewOutlinerColumn::GetColumnID()
{
	return ColumnID;
}

SHeaderRow::FColumn::FArguments FDataprepPreviewOutlinerColumn::ConstructHeaderRowColumn()
{
	CachedPreviewData->GetOnPreviewIsDoneProcessing().AddSP( this, &FDataprepPreviewOutlinerColumn::OnPreviewSystemIsDoneProcessing );

	return SHeaderRow::Column( GetColumnID() )
		.DefaultLabel( LOCTEXT("Preview_HeaderText", "Preview") )
		.DefaultTooltip( LOCTEXT("Preview_HeaderTooltip", "Show the result of the current preview.") )
		.FillWidth( 5.0f );
}

const TSharedRef<SWidget> FDataprepPreviewOutlinerColumn::ConstructRowWidget(SceneOutliner::FTreeItemRef TreeItem, const STableRow<SceneOutliner::FTreeItemPtr>& Row)
{
	DataprepPreviewOutlinerColumnUtils::FObjectGetter Visitor; 
	TreeItem->Visit( Visitor );

	if ( UObject* Object = Visitor.Result().Get() )
	{
		if ( TSharedPtr<ISceneOutliner> SceneOutliner = WeakSceneOutliner.Pin() )
		{ 
			return SNew( SDataprepPreviewRow, CachedPreviewData->GetPreviewDataForObject( Object ) )
				.HighlightText( SceneOutliner->GetFilterHighlightText() );
		}
	}
	
	return SNullWidget::NullWidget;
}

void FDataprepPreviewOutlinerColumn::PopulateSearchStrings(const SceneOutliner::ITreeItem& Item, TArray< FString >& OutSearchStrings) const
{
	DataprepPreviewOutlinerColumnUtils::FObjectGetter Visitor;
	Item.Visit(Visitor);
	if (UObject* Object = Visitor.Result().Get())
	{
		if ( TSharedPtr<FDataprepPreviewProcessingResult> PreviewResult = CachedPreviewData->GetPreviewDataForObject( Object ) )
		{
			PreviewResult->PopulateSearchStringFromFetchedData( OutSearchStrings );
		}
	}
}

void FDataprepPreviewOutlinerColumn::SortItems(TArray<SceneOutliner::FTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const
{
	OutItems.Sort([this, SortMode](const SceneOutliner::FTreeItemPtr& First, const SceneOutliner::FTreeItemPtr& Second)
	{
			DataprepPreviewOutlinerColumnUtils::FObjectGetter Visitor;
			First->Visit( Visitor );
			UObject* FirstObject = Visitor.Result().Get();
			Second->Visit( Visitor );
			UObject* SecondObject = Visitor.Result().Get();
			if ( FirstObject && SecondObject )
			{

				FDataprepPreviewProcessingResult* FirstPreviewData = CachedPreviewData->GetPreviewDataForObject( FirstObject ).Get();
				FDataprepPreviewProcessingResult* SecondPreviewData = CachedPreviewData->GetPreviewDataForObject( SecondObject ).Get();

				if ( FirstPreviewData && SecondPreviewData )
				{
					if ( FirstPreviewData->Status == SecondPreviewData->Status && FirstPreviewData->CurrentProcessingIndex == SecondPreviewData->CurrentProcessingIndex )
					{
						EDataprepPreviewResultComparison Comparaison = FirstPreviewData->CompareFetchedDataTo( *SecondPreviewData );
						if ( Comparaison != EDataprepPreviewResultComparison::Equal )
						{
							if ( SortMode == EColumnSortMode::Descending )
							{
								return Comparaison == EDataprepPreviewResultComparison::BiggerThan;
							}
							return Comparaison == EDataprepPreviewResultComparison::SmallerThan;
						}
					}
					else if ( FirstPreviewData->Status == EDataprepPreviewStatus::Pass )
					{
						return true;
					}
					else if ( SecondPreviewData->Status == EDataprepPreviewStatus::Pass )
					{
						return false;
					}
					else
					{
						// Filter by at which index they fail
						if ( FirstPreviewData->CurrentProcessingIndex != SecondPreviewData->CurrentProcessingIndex )
						{
							return FirstPreviewData->CurrentProcessingIndex > SecondPreviewData->CurrentProcessingIndex;
						}
					}
				}
			}

		// If all else fail filter by name (always Ascending)
		int32 SortPriorityFirst = First->GetTypeSortPriority();
		int32 SortPrioritySecond = Second->GetTypeSortPriority();
		if ( SortPriorityFirst != SortPrioritySecond )
		{
			return SortPriorityFirst < SortPrioritySecond;
		}

		return First->GetDisplayString() < Second->GetDisplayString();
	});
}

void FDataprepPreviewOutlinerColumn::OnPreviewSystemIsDoneProcessing()
{
	if ( TSharedPtr<ISceneOutliner> SceneOutliner = WeakSceneOutliner.Pin() )
	{
		if ( SceneOutliner->GetColumnSortMode( ColumnID ) != EColumnSortMode::None )
		{
			SceneOutliner->RequestSort();
		}
	}
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterList.h"

#include "ConjunctionFilter.h"
#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotsEditorFilters.h"
#include "LevelSnapshotsEditorStyle.h"
#include "SCreateNewFilterWidget.h"
#include "SLevelSnapshotsEditorFilter.h"
#include "Components/HorizontalBox.h"

#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"


SLevelSnapshotsEditorFilterList::~SLevelSnapshotsEditorFilterList()
{
	if (ensure(ManagedAndCondition.IsValid()))
	{
		ManagedAndCondition->OnChildAdded.Remove(AddDelegateHandle);
	}
}

void SLevelSnapshotsEditorFilterList::Construct(const FArguments& InArgs, UConjunctionFilter* InManagedAndCondition, const TSharedRef<FLevelSnapshotsEditorFilters>& InEditorFilterModel)
{
	ManagedAndCondition = InManagedAndCondition;

	AddDelegateHandle = InManagedAndCondition->OnChildAdded.AddRaw(this, &SLevelSnapshotsEditorFilterList::AddChild, InEditorFilterModel, false);

	ChildSlot
	[
		SAssignNew(FilterBox, SWrapBox)
			.UseAllottedSize(true)
	];

	TSharedPtr<FLevelSnapshotsEditorViewBuilder> ViewBuilder = InEditorFilterModel->GetBuilder();

	if (ensureMsgf(ViewBuilder.IsValid(), TEXT("Invalid Editor View Builder")))
	{
		SAssignNew(AddFilterWidget, SCreateNewFilterWidget, ViewBuilder->EditorDataPtr->GetFavoriteFilters(), InManagedAndCondition);

		if (!AddTutorialTextAndCreateFilterWidgetIfEmpty())
		{
			bool bSkipAnd = true;
			for (UNegatableFilter* Filter : InManagedAndCondition->GetChildren())
			{
				AddChild(Filter, InEditorFilterModel, bSkipAnd);
				bSkipAnd = false;
			}
		}
	}
}

void SLevelSnapshotsEditorFilterList::OnClickRemoveFilter(TSharedRef<SLevelSnapshotsEditorFilter> RemovedFilterWidget) const
{
	const TWeakObjectPtr<UNegatableFilter> RemovedFilter = RemovedFilterWidget->GetSnapshotFilter();
	if (!ensure(FilterBox.IsValid()) || !ensure(ManagedAndCondition.IsValid() || !ensure(RemovedFilter.IsValid())))
	{
		return;
	}

	const int32 RemovedSlotIndex = FilterBox->RemoveSlot(RemovedFilterWidget);
	FChildren* FilterChildren = FilterBox->GetChildren();
	if (RemovedSlotIndex != INDEX_NONE && FilterChildren->Num() > 1)
	{
		const bool bRemovedLastWidgetInList = FilterChildren->Num() - 1 == RemovedSlotIndex;
		const int32 IndexToRemove = bRemovedLastWidgetInList ?  RemovedSlotIndex - 1 : RemovedSlotIndex;
		
		TSharedRef<SWidget> AndText = FilterChildren->GetChildAt(IndexToRemove);
		FilterBox->RemoveSlot(AndText);
	}
	ManagedAndCondition->RemoveChild(RemovedFilterWidget->GetSnapshotFilter().Get());

	// We remove last item; avoid adding AddFilterWidget twice. 
	const bool bHasNoFilters = ManagedAndCondition->GetChildren().Num() == 0; 
	if (bHasNoFilters)
	{
		FilterBox->RemoveSlot(AddFilterWidget.ToSharedRef());
	}
	
	AddTutorialTextAndCreateFilterWidgetIfEmpty();
}

bool SLevelSnapshotsEditorFilterList::AddTutorialTextAndCreateFilterWidgetIfEmpty() const
{
	if (!ensure(ManagedAndCondition.IsValid()))
	{
		return false;
	}
	
	const bool bHasNoFilters = ManagedAndCondition->GetChildren().Num() == 0; 
	if (bHasNoFilters)
	{
		FilterBox->AddSlot()
            .Padding(3, 3)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
                .Padding(2.f, 0.f)
                .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("FilterListDragTutorial", "Drag a favorite filter here"))
                .Justification(ETextJustify::Center)
            ]
        ];
		
		FilterBox->AddSlot()
		.Padding(3,5)
		[
			AddFilterWidget.ToSharedRef()
		];
	}
	
	return bHasNoFilters;
}

void SLevelSnapshotsEditorFilterList::AddChild(UNegatableFilter* AddedFilter, TSharedRef<FLevelSnapshotsEditorFilters> InEditorFilterModel, bool bSkipAnd) const
{
	if (!ensure(ManagedAndCondition.IsValid()))
	{
		return;
	}
	
	const bool bWasEmptyBefore = ManagedAndCondition->GetChildren().Num() == 1;
	if (bWasEmptyBefore)
	{
		FilterBox->ClearChildren();
	}
	else if (!bSkipAnd)
	{	
		FilterBox->AddSlot()
			.Padding(1)
			.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
                .TextStyle( FLevelSnapshotsEditorStyle::Get(), "LevelSnapshotsEditor.FilterRow.And")
                .Text(LOCTEXT("FilterRow.And", "&"))
                .WrapTextAt(128.0f)
        ];
	}

	const TSharedRef<SCreateNewFilterWidget> AddFilterWidgetAsRef = AddFilterWidget.ToSharedRef();
	FilterBox->RemoveSlot(AddFilterWidgetAsRef);
	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter, AddedFilter, InEditorFilterModel)
				.OnClickRemoveFilter(SLevelSnapshotsEditorFilter::FOnClickRemoveFilter::CreateSP(this, &SLevelSnapshotsEditorFilterList::OnClickRemoveFilter))
				.IsParentFilterIgnored_Lambda([this]() { return ManagedAndCondition->IsIgnored(); })
		];
	
	// AddFilterWidget should be last widget of row
	FilterBox->AddSlot()
		.Padding(3,3)
	[
		AddFilterWidgetAsRef
	];
}

#undef LOCTEXT_NAMESPACE
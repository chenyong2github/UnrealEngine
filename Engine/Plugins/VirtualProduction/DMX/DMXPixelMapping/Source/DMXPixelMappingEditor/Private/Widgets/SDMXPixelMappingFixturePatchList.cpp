// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXPixelMappingFixturePatchList.h"

#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "ViewModels/DMXPixelMappingDMXLibraryViewModel.h"
#include "Widgets/SDMXReadOnlyFixturePatchList.h"

#include "ScopedTransaction.h"
#include "Algo/Find.h"
#include "Widgets/Input/SButton.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingFixturePatchList"

SDMXPixelMappingFixturePatchList::~SDMXPixelMappingFixturePatchList()
{
	if (WeakViewModel.IsValid() && FixturePatchList.IsValid())
	{
		WeakViewModel->SaveFixturePatchListDescriptor(FixturePatchList->GetListDescriptor());
	}
}

void SDMXPixelMappingFixturePatchList::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit, UDMXPixelMappingDMXLibraryViewModel* InViewModel)
{
	if (!InToolkit.IsValid() || !InViewModel || !InViewModel->GetDMXLibrary())
	{
		return;
	}
	WeakToolkit = InToolkit;
	WeakViewModel = InViewModel;

	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.Padding(4.f)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FLinearColor::White)
					.ToolTipText(LOCTEXT("AddSelectedPatchesTooltip", "Adds the selected patches to the Pixel Mapping"))
					.ContentPadding(FMargin(5.0f, 1.0f))
					.OnClicked(this, &SDMXPixelMappingFixturePatchList::OnAddSelectedPatchesClicked)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddSelectedPatchesLabel", "Add Selected Patches"))
					]
				]

				+ SHorizontalBox::Slot()
				.Padding(8.f, 4.f)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FLinearColor::White)
					.ToolTipText(LOCTEXT("AddAllPatchesTooltip", "Adds all patches to the Pixel Mapping"))
					.ContentPadding(FMargin(5.0f, 1.0f))
					.OnClicked(this, &SDMXPixelMappingFixturePatchList::OnAddAllPatchesClicked)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddAllPatchesLabel", "Add all Patches"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(FixturePatchList, SDMXReadOnlyFixturePatchList)
				.DMXLibrary(InViewModel->GetDMXLibrary())
				.ListDescriptor(InViewModel->GetFixturePatchListDescriptor())
				.OnRowDragged(this, &SDMXPixelMappingFixturePatchList::OnRowDragged)
			]
		];

	
	// Make an initial selection
	if (FixturePatchList.IsValid() && !FixturePatchList->GetListItems().IsEmpty())
	{
		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> NewSelection{ FixturePatchList->GetListItems()[0] };
		FixturePatchList->SelectItems(NewSelection);
	}

	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &SDMXPixelMappingFixturePatchList::OnComponentAddedOrRemoved);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &SDMXPixelMappingFixturePatchList::OnComponentAddedOrRemoved);
}

void SDMXPixelMappingFixturePatchList::PostUndo(bool bSuccess)
{
	if (WeakViewModel.IsValid())
	{
		FixturePatchList->SetExcludedFixturePatches(WeakViewModel->GetFixturePatchesInUse());
	}
}

void SDMXPixelMappingFixturePatchList::PostRedo(bool bSuccess)
{
	if (WeakViewModel.IsValid())
	{
		FixturePatchList->SetExcludedFixturePatches(WeakViewModel->GetFixturePatchesInUse());
	}
}

FReply SDMXPixelMappingFixturePatchList::OnRowDragged(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = WeakViewModel.IsValid() ? WeakViewModel->GetFixtureGroupComponent() : nullptr;
	if (!FixtureGroupComponent)
	{
		return FReply::Unhandled();
	}

	const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> SelectedPatches = FixturePatchList->GetSelectedFixturePatchRefs();
	if (!SelectedPatches.IsEmpty())
	{
		TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>> Templates;
		for (const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef : SelectedPatches)
		{
			if (!FixturePatchRef.IsValid())
			{
				continue;
			}

			const UDMXEntityFixturePatch* FixturePatch = FixturePatchRef->GetFixturePatch();
			const UDMXEntityFixtureType* FixtureType = FixturePatch ? FixturePatch->GetFixtureType() : nullptr;
			const FDMXFixtureMode* ActiveModePtr = FixturePatch ? FixturePatch->GetActiveMode() : nullptr;
			if (FixturePatch && FixtureType && ActiveModePtr)
			{
				if (ActiveModePtr->bFixtureMatrixEnabled)
				{
					TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchMatrixTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingMatrixComponent::StaticClass(), *FixturePatchRef);
					Templates.Add(FixturePatchMatrixTemplate);
				}
				else
				{
					TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchItemTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingFixtureGroupItemComponent::StaticClass(), *FixturePatchRef);
					Templates.Add(FixturePatchItemTemplate);
				}
			}
		}
		return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FVector2D::ZeroVector, Templates, FixtureGroupComponent));
	}

	return FReply::Unhandled();
}

void SDMXPixelMappingFixturePatchList::OnComponentAddedOrRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	FixturePatchList->RequestListRefresh();
}

FReply SDMXPixelMappingFixturePatchList::OnAddSelectedPatchesClicked()
{
	if (WeakViewModel.IsValid())
	{
		const FScopedTransaction AddSelectedFixturePatchesTransaction(LOCTEXT("AddSelectedFixturePatchesTransaction", "Add selected Fixture Patches"));

		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> SelectedPatches = FixturePatchList->GetSelectedFixturePatchRefs();
		
		if (!SelectedPatches.IsEmpty())
		{
			AddFixturePatches(SelectedPatches);

			SelectAfter(SelectedPatches.Last()->GetFixturePatch());
			FixturePatchList->SetExcludedFixturePatches(WeakViewModel->GetFixturePatchesInUse());
		}
	}

	return FReply::Handled();
}

FReply SDMXPixelMappingFixturePatchList::OnAddAllPatchesClicked()
{
	if (WeakViewModel.IsValid())
	{
		const FScopedTransaction AddAllFixturePatchesTransaction(LOCTEXT("AddAllFixturePatchesTransaction", "Add all Fixture Patches"));

		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> AllVisiblePatches = FixturePatchList->GetVisibleFixturePatchRefs();

		if (!AllVisiblePatches.IsEmpty())
		{
			AddFixturePatches(AllVisiblePatches);

			FixturePatchList->SetExcludedFixturePatches(WeakViewModel->GetFixturePatchesInUse());
		}
	}

	return FReply::Handled();
}

void SDMXPixelMappingFixturePatchList::SelectAfter(const UDMXEntityFixturePatch* FixturePatch)
{
	if (!FixturePatch)
	{
		return;
	}

	const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> Items = FixturePatchList->GetListItems();
	int32 IndexOfPatch = Items.IndexOfByPredicate([FixturePatch](const TSharedPtr<FDMXEntityFixturePatchRef>& Item)
		{
			return Item->GetFixturePatch() == FixturePatch;
		});
	if (Items.IsValidIndex(IndexOfPatch + 1))
	{
		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> NewSelection{ Items[IndexOfPatch + 1]};
		FixturePatchList->SelectItems(NewSelection);
	}
	else if (Items.IsValidIndex(IndexOfPatch - 1))
	{
		const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> NewSelection{ Items[IndexOfPatch - 1] };
		FixturePatchList->SelectItems(NewSelection);
	}
}

void SDMXPixelMappingFixturePatchList::AddFixturePatches(const TArray<TSharedPtr<FDMXEntityFixturePatchRef>>& FixturePatches)
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	UDMXPixelMappingDMXLibraryViewModel* ViewModel = WeakViewModel.Get();
	UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = ViewModel ? ViewModel->GetFixtureGroupComponent() : nullptr;
	UDMXPixelMappingRootComponent* RootComponent = ViewModel ? ViewModel->GetPixelMappingRootComponent() : nullptr;
	if (!Toolkit.IsValid() || !ViewModel || !FixtureGroupComponent || !RootComponent)
	{
		return;
	}

	TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>> Templates;
	for (const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef : FixturePatches)
	{
		if (!FixturePatchRef.IsValid())
		{
			continue;
		}

		UDMXEntityFixturePatch* FixturePatch = FixturePatchRef->GetFixturePatch();
		UDMXEntityFixtureType* FixtureType = FixturePatch ? FixturePatch->GetFixtureType() : nullptr;
		const FDMXFixtureMode* ActiveModePtr = FixturePatch ? FixturePatch->GetActiveMode() : nullptr;
		if (FixturePatch && FixtureType && ActiveModePtr)
		{
			if (ActiveModePtr->bFixtureMatrixEnabled)
			{
				const TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchMatrixTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingMatrixComponent::StaticClass(), *FixturePatchRef);
				Templates.Add(FixturePatchMatrixTemplate);
			}
			else
			{
				const TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchItemTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingFixtureGroupItemComponent::StaticClass(), *FixturePatchRef);
				Templates.Add(FixturePatchItemTemplate);
			}
		}
	}

	const TArray<UDMXPixelMappingBaseComponent*> NewComponents = Toolkit->CreateComponentsFromTemplates(RootComponent, FixtureGroupComponent, Templates);

	// Layout
	const TArray<UDMXEntityFixturePatch*> FixturePatchesInLibrary = ViewModel->GetDMXLibrary()->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	bool bLayoutEvenOverParent = FixturePatchesInLibrary.Num() == FixturePatches.Num();
	if (bLayoutEvenOverParent)
	{
		LayoutEvenOverParent(NewComponents);
	}
	else
	{
		LayoutAfterLastPatch(NewComponents);
	}
}

void SDMXPixelMappingFixturePatchList::LayoutEvenOverParent(const TArray<UDMXPixelMappingBaseComponent*> Components)
{
	UDMXPixelMappingDMXLibraryViewModel* ViewModel = WeakViewModel.Get();
	UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = ViewModel ? ViewModel->GetFixtureGroupComponent() : nullptr;
	if (!ViewModel || !FixtureGroupComponent)
	{
		return;
	}

	const int32 Columns = FMath::RoundFromZero(FMath::Sqrt((float)Components.Num()));
	const int32 Rows = FMath::RoundFromZero((float)Components.Num() / Columns);
	const FVector2D Size = FVector2D(FixtureGroupComponent->GetSize().X / Columns, FixtureGroupComponent->GetSize().Y / Rows);

	const FVector2D ParentPosition = FixtureGroupComponent->GetPosition();
	int32 Column = -1;
	int32 Row = 0;
	for (UDMXPixelMappingBaseComponent* Component : Components)
	{
		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component))
		{
			Column++;
			if (Column >= Columns)
			{
				Column = 0;
				Row++;
			}
			const FVector2D Position = ParentPosition + FVector2D(Column * Size.X, Row * Size.Y);
			OutputComponent->SetPosition(Position);
			OutputComponent->SetSize(Size);
		}
	}
}

void SDMXPixelMappingFixturePatchList::LayoutAfterLastPatch(const TArray<UDMXPixelMappingBaseComponent*> Components)
{
	UDMXPixelMappingDMXLibraryViewModel* ViewModel = WeakViewModel.Get();
	UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = ViewModel ? ViewModel->GetFixtureGroupComponent() : nullptr;
	UDMXPixelMappingOutputComponent* FirstComponentToLayout = Components.IsEmpty() ? nullptr : Cast<UDMXPixelMappingOutputComponent>(Components[0]);
	if (!ViewModel || !FixtureGroupComponent || !FirstComponentToLayout)
	{
		return;
	}

	// Find other components
	TArray<UDMXPixelMappingBaseComponent*> OtherComponentsInGroup;
	for (UDMXPixelMappingBaseComponent* Child : FixtureGroupComponent->GetChildren())
	{
		if (!Components.Contains(Child))
		{
			OtherComponentsInGroup.Add(Child);
		}
	}

	// Find a starting position
	FVector2D NextPosition = FixtureGroupComponent->GetPosition();
	for (UDMXPixelMappingBaseComponent* OtherComponent : OtherComponentsInGroup)
	{
		if (UDMXPixelMappingOutputComponent* OtherOutputComponent = Cast<UDMXPixelMappingOutputComponent>(OtherComponent))
		{
			NextPosition.X = FMath::Max(NextPosition.X, OtherOutputComponent->GetPosition().X + OtherOutputComponent->GetSize().X + 1);
			if (NextPosition.X + FirstComponentToLayout->GetSize().X > FixtureGroupComponent->GetPosition().X + FixtureGroupComponent->GetSize().X - 1)
			{
				NextPosition.X = FixtureGroupComponent->GetPosition().X;
				NextPosition.Y += FirstComponentToLayout->GetSize().Y;
			}
		}
	}

	// Layout
	float RowHeight = 0.f;
	for (UDMXPixelMappingBaseComponent* Component : Components)
	{
		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component))
		{
			if (FixtureGroupComponent->IsOverPosition(NextPosition) &&
				FixtureGroupComponent->IsOverPosition(NextPosition + OutputComponent->GetSize()))
			{
				OutputComponent->SetPosition(NextPosition);

				RowHeight = FMath::Max(OutputComponent->GetSize().Y, RowHeight);
				NextPosition = FVector2D(NextPosition.X + OutputComponent->GetSize().X, NextPosition.Y);
			}
			else
			{
				// Try on a new row
				FVector2D NewRowPosition = FVector2D(FixtureGroupComponent->GetPosition().X, NextPosition.Y + RowHeight);

				const FVector2D NextPositionOnNewRow = FVector2D(NewRowPosition.X + OutputComponent->GetSize().X, NewRowPosition.Y);
				if (FixtureGroupComponent->IsOverPosition(NextPositionOnNewRow) &&
					FixtureGroupComponent->IsOverPosition(NextPositionOnNewRow + OutputComponent->GetSize()))
				{
					OutputComponent->SetPosition(NewRowPosition);

					NextPosition = FVector2D(NewRowPosition.X + OutputComponent->GetSize().X, NewRowPosition.Y);
					RowHeight = OutputComponent->GetSize().Y;
				}
				else
				{
					OutputComponent->SetPosition(NextPosition);

					NextPosition = FVector2D(NextPosition.X + OutputComponent->GetSize().X, NextPosition.Y);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

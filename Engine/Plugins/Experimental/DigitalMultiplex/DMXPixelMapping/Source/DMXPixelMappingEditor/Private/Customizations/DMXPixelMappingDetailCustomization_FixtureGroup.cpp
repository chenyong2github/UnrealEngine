// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_FixtureGroup.h"

#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Widgets/SDMXPixelMappingFixturePatchDetailRow.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "PropertyHandle.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Layout/Visibility.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingDetailCustomization_FixtureGroup"

void FDMXPixelMappingDetailCustomization_FixtureGroup::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	WeakFixtureGroupComponent = GetSelectedFixtureGroupComponent(InDetailLayout);
	if(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = WeakFixtureGroupComponent.Get())
	{
		// Listen to component changes
		UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentAdded);
		UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentRemoved);

		// Listen to the library being changed in the group component
		DMXLibraryHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, DMXLibrary), UDMXPixelMappingFixtureGroupComponent::StaticClass());
		DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnLibraryChanged));
		DMXLibraryHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnLibraryChanged));
		
		if (UDMXLibrary* DMXLibrary = GetSelectedDMXLibrary(FixtureGroupComponent))
		{
			UpdateFixturePatchesInUse(DMXLibrary);

			// Get editing categories
			IDetailCategoryBuilder& FixtureListCategoryBuilder = InDetailLayout.EditCategory("Fixture List", FText::GetEmpty(), ECategoryPriority::Important);

			// Listen to the entities array being changed in the library
			EntitiesHandle = InDetailLayout.GetProperty(UDMXLibrary::GetEntitiesPropertyName());
			EntitiesHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::ForceRefresh));

			// Add the library property
			FixtureListCategoryBuilder.AddProperty(DMXLibraryHandle);

			// Add fixture patches as custom rows
			TArray<UDMXEntityFixturePatch*> AllFixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
			for (UDMXEntityFixturePatch* FixturePatch : AllFixturePatches)
			{
				const bool bPatchIsAssigned = FixtureGroupComponent->Children.ContainsByPredicate([FixturePatch](UDMXPixelMappingBaseComponent* BaseComponent)
					{
						if (UDMXPixelMappingFixtureGroupItemComponent* GroupItemComponent = Cast<UDMXPixelMappingFixtureGroupItemComponent>(BaseComponent))
						{
							return GroupItemComponent->FixturePatchRef.GetFixturePatch() == FixturePatch;
						}
						else if (UDMXPixelMappingMatrixComponent*  MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(BaseComponent))
						{
							return MatrixComponent->FixturePatchRef.GetFixturePatch() == FixturePatch;
						}
						return false;
					});

				if (!bPatchIsAssigned)
				{
					TSharedRef<SDMXPixelMappingFixturePatchDetailRow> FixturePatchDetailRowWidget =
						SNew(SDMXPixelMappingFixturePatchDetailRow)
						.FixturePatch(FixturePatch)
						.OnLMBDown(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchLMBDown, FDMXEntityFixturePatchRef(FixturePatch))
						.OnLMBUp(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchLMBUp, FDMXEntityFixturePatchRef(FixturePatch))
						.OnDragged(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchesDragged);

					FDMXPixelMappingDetailCustomization_FixtureGroup::FDetailRowWidgetWithPatch DetailRowWidgetWithPatch;
					DetailRowWidgetWithPatch.DetailRowWidget = FixturePatchDetailRowWidget;
					DetailRowWidgetWithPatch.WeakFixturePatch = FixturePatch;
					DetailRowWidgetsWithPatch.Add(DetailRowWidgetWithPatch);

					FixtureListCategoryBuilder.AddCustomRow(FText::GetEmpty())
						.WholeRowContent()
						[
							FixturePatchDetailRowWidget
						];
				}
			}		
		}
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnLibraryChanged()
{
	// Remove patches not in the library
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin())
	{
		if (UDMXPixelMappingFixtureGroupComponent* GroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(WeakFixtureGroupComponent))
		{
			const FScopedTransaction ChangeDMXLibraryTransaction(LOCTEXT("DMXLibraryChangedResetTransactionReason", "PixelMapping Changed DMX Library"));

			TArray<UDMXPixelMappingBaseComponent*> CachedChildren(GroupComponent->Children);
			for (UDMXPixelMappingBaseComponent* ChildComponent : CachedChildren)
			{
				if (UDMXPixelMappingFixtureGroupItemComponent* ChildGroupItem = Cast<UDMXPixelMappingFixtureGroupItemComponent>(ChildComponent))
				{
					if (ChildGroupItem->FixturePatchRef.GetFixturePatch() &&
						ChildGroupItem->FixturePatchRef.GetFixturePatch()->GetParentLibrary() != GroupComponent->DMXLibrary)
					{
						GroupComponent->RemoveChild(ChildGroupItem);
					}
				}
				else if (UDMXPixelMappingMatrixComponent* ChildMatrix = Cast<UDMXPixelMappingMatrixComponent>(ChildComponent))
				{
					if (ChildMatrix->FixturePatchRef.GetFixturePatch() &&
						ChildMatrix->FixturePatchRef.GetFixturePatch()->GetParentLibrary() != GroupComponent->DMXLibrary)
					{
						GroupComponent->RemoveChild(ChildMatrix);
					}
				}
			};
		}
	}

	if (!bRefreshing)
	{
		ForceRefresh();
		bRefreshing = true;
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	if (!bRefreshing)
	{
		ForceRefresh();
		bRefreshing = true;
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	if (!bRefreshing)
	{
		ForceRefresh();
		bRefreshing = true;
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::ForceRefresh()
{
	// Reset the handles so they won't fire any changes after refreshing
	DMXLibraryHandle.Reset();
	EntitiesHandle.Reset();

	if (ensure(PropertyUtilities.IsValid()))
	{
		PropertyUtilities->ForceRefresh();
	}

	bRefreshing = false;
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchLMBDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FDMXEntityFixturePatchRef FixturePatchRef)
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	if(FixturePatch)
	{
		if (SelectedFixturePatches.Num() == 0)
		{
			SelectedFixturePatches = TArray<FDMXEntityFixturePatchRef>({ FixturePatchRef });
		}
		else
		{
			if (MouseEvent.IsShiftDown())
			{
				// Shift select
				const int32 IndexOfAnchor = DetailRowWidgetsWithPatch.IndexOfByPredicate([this](const FDMXPixelMappingDetailCustomization_FixtureGroup::FDetailRowWidgetWithPatch& DetailRowWidgetWithPatch) {
					return
						DetailRowWidgetWithPatch.WeakFixturePatch.IsValid() &&
						SelectedFixturePatches[0].GetFixturePatch() &&
						DetailRowWidgetWithPatch.WeakFixturePatch.Get() == SelectedFixturePatches[0].GetFixturePatch();
					});

				const int32 IndexOfSelected = DetailRowWidgetsWithPatch.IndexOfByPredicate([FixturePatch](const FDMXPixelMappingDetailCustomization_FixtureGroup::FDetailRowWidgetWithPatch& DetailRowWidgetWithPatch) {
					return
						DetailRowWidgetWithPatch.WeakFixturePatch.IsValid() &&
						DetailRowWidgetWithPatch.WeakFixturePatch.Get() == FixturePatch;
					});

				if (ensure(IndexOfSelected != INDEX_NONE))
				{
					if (IndexOfAnchor == INDEX_NONE || IndexOfAnchor == IndexOfSelected)
					{
						SelectedFixturePatches = TArray<FDMXEntityFixturePatchRef>({ FixturePatchRef });
					}
					else
					{
						SelectedFixturePatches.Reset();
						SelectedFixturePatches.Add(FDMXEntityFixturePatchRef(DetailRowWidgetsWithPatch[IndexOfAnchor].WeakFixturePatch.Get()));

						const bool bAscending = IndexOfAnchor < IndexOfSelected;
						const int32 StartIndex = bAscending ? IndexOfAnchor + 1 : IndexOfSelected;
						const int32 EndIndex = bAscending ? IndexOfSelected : IndexOfAnchor - 1;

						for (int32 IndexDetailRowWidget = StartIndex; IndexDetailRowWidget <= EndIndex; IndexDetailRowWidget++)
						{
							if (UDMXEntityFixturePatch* NewlySelectedPatch = DetailRowWidgetsWithPatch[IndexDetailRowWidget].WeakFixturePatch.Get())
							{
								SelectedFixturePatches.AddUnique(FDMXEntityFixturePatchRef(NewlySelectedPatch));
							}
						}
					}
				}
			}
			else if (MouseEvent.IsControlDown())
			{
				// Ctrl select
				if (!SelectedFixturePatches.Contains(FixturePatchRef))
				{
					SelectedFixturePatches.Add(FixturePatchRef);
				}
				else
				{
					SelectedFixturePatches.Remove(FixturePatchRef);
				}
			}
		}

		UpdateFixturePatchHighlights();
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchLMBUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FDMXEntityFixturePatchRef FixturePatchRef)
{
	if (!MouseEvent.IsShiftDown() && !MouseEvent.IsControlDown())
	{
		// Make a new selection
		SelectedFixturePatches.Reset();
		SelectedFixturePatches.Add(FixturePatchRef);

		UpdateFixturePatchHighlights();
	}
}

FReply FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchesDragged(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (WeakFixtureGroupComponent.IsValid())
	{
		TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>> Templates;
		for (const FDMXEntityFixturePatchRef& FixturePatchRef : SelectedFixturePatches)
		{
			if (UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch())
			{
				if (UDMXEntityFixtureType* FixtureType = FixturePatch->GetFixtureType())
				{
					if (FixtureType->bFixtureMatrixEnabled)
					{
						TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchMatrixTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingMatrixComponent::StaticClass(), FixturePatchRef);
						Templates.Add(FixturePatchMatrixTemplate);
					}
					else
					{
						TSharedRef<FDMXPixelMappingComponentTemplate> FixturePatchItemTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingFixtureGroupItemComponent::StaticClass(), FixturePatchRef);
						Templates.Add(FixturePatchItemTemplate);
					}
				}
			}
		}

		UpdateFixturePatchHighlights();

		ForceRefresh();

		return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FVector2D::ZeroVector, Templates, WeakFixtureGroupComponent.Get()));
	}

	return FReply::Handled();
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::UpdateFixturePatchHighlights()
{
	for (const FDMXPixelMappingDetailCustomization_FixtureGroup::FDetailRowWidgetWithPatch& DetailRowWidgetWithPatch : DetailRowWidgetsWithPatch)
	{
		bool bIsSelected = SelectedFixturePatches.ContainsByPredicate([DetailRowWidgetWithPatch](const FDMXEntityFixturePatchRef& SelectedRef) {
			return
				SelectedRef.GetFixturePatch() &&
				DetailRowWidgetWithPatch.WeakFixturePatch.IsValid() &&
				SelectedRef.GetFixturePatch() == DetailRowWidgetWithPatch.WeakFixturePatch.Get();
			});

		DetailRowWidgetWithPatch.DetailRowWidget->SetHighlight(bIsSelected);
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::UpdateFixturePatchesInUse(UDMXLibrary* DMXLibrary)
{
	TArray<UDMXEntityFixturePatch*> FixturePatchesInLibrary = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();

	FixturePatches.Reset(FixturePatchesInLibrary.Num());
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatchesInLibrary)
	{
		FDMXEntityFixturePatchRef FixturePatchRef;
		FixturePatchRef.SetEntity(FixturePatch);
		FixturePatches.Add(FixturePatchRef);
	}
		
	SelectedFixturePatches.RemoveAll([&FixturePatchesInLibrary](const FDMXEntityFixturePatchRef& SelectedFixturePatch) {
		return !SelectedFixturePatch.GetFixturePatch() || !FixturePatchesInLibrary.Contains(SelectedFixturePatch.GetFixturePatch());
	});
}

UDMXLibrary* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedDMXLibrary(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent) const
{
	if (FixtureGroupComponent)
	{
		return FixtureGroupComponent->DMXLibrary;
	}

	return nullptr;
}

UDMXPixelMappingFixtureGroupComponent* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedFixtureGroupComponent(const IDetailLayoutBuilder& InDetailLayout) const
{
	const TArray<TWeakObjectPtr<UObject> >& SelectedSelectedObjects = InDetailLayout.GetSelectedObjects();
	TArray<UDMXPixelMappingFixtureGroupComponent*> SelectedSelectedComponents;

	for (TWeakObjectPtr<UObject> SelectedObject : SelectedSelectedObjects)
	{
		if (UDMXPixelMappingFixtureGroupComponent* SelectedComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(SelectedObject.Get()))
		{
			SelectedSelectedComponents.Add(SelectedComponent);
		}
	}

	// we only support 1 uobject editing for now
	// we set singe UObject here SDMXPixelMappingDetailsView::OnSelectedComponenetChanged()
	// and that is why we getting only one UObject from here TArray<TWeakObjectPtr<UObject>>& SDetailsView::GetSelectedObjects()
	check(SelectedSelectedComponents.Num());
	return SelectedSelectedComponents[0];
}

#undef LOCTEXT_NAMESPACE

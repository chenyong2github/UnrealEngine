// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_FixtureGroup.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "DetailWidgetRow.h"
#include "Layout/Visibility.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingDetailCustomization_FixtureGroup"

void FDMXPixelMappingDetailCustomization_FixtureGroup::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	DetailLayout = &InDetailLayout;

	FixturePatchItemTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingFixtureGroupItemComponent::StaticClass());

	UpdateFixturePatchRefs();

	// Get editing categories
	IDetailCategoryBuilder& FixtureListCategoryBuilder = DetailLayout->EditCategory("Fixture List", FText::GetEmpty(), ECategoryPriority::Important);

	// Add DMX library property and listener
	TSharedPtr<IPropertyHandle> DMXLibraryHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, DMXLibrary), UDMXPixelMappingFixtureGroupComponent::StaticClass());
	FixtureListCategoryBuilder.AddProperty(DMXLibraryHandle);
	DMXLibraryHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnDMXLibraryChanged));

	// Add Fixture Patch list widget
	FixtureListCategoryBuilder.AddCustomRow(FText::GetEmpty())
		.WholeRowContent()
		[
			SAssignNew(FixturePatchListArea, SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBrush"))
		];

	RebuildFixturePatchListView();
}

TSharedRef<ITableRow> FDMXPixelMappingDetailCustomization_FixtureGroup::GenerateFixturePatchRow(TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InFixturePatchRef.IsValid())
	{
		if (UDMXEntityFixturePatch* FixturePatch = InFixturePatchRef->GetFixturePatch())
		{
			return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
				.Padding(2.0f)
				.OnDragDetected(FOnDragDetected::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchDragDetected, InFixturePatchRef))
				.Style(FEditorStyle::Get(), "UMGEditor.PaletteItem")
				.ShowSelection(true)
				[
					SNew(SBox)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FixturePatch->GetDisplayName()))
					]
				];
		}
	}

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixtureSelectionChanged(TSharedPtr<FDMXEntityFixturePatchRef> FixturePatchRef, ESelectInfo::Type SelectInfo)
{
	TArray<TSharedPtr<FDMXEntityFixturePatchRef>> SelectedItems;
	FixturePatchListView->GetSelectedItems(SelectedItems);
	UDMXPixelMappingFixtureGroupComponent* Component = GetSelectedFixtureGroupComponent();
	if (Component != nullptr)
	{
		Component->SelectedFixturePatchRef.Empty();
		for (TSharedPtr<FDMXEntityFixturePatchRef> SelectedItem : SelectedItems)
		{
			if (SelectedItem.IsValid())
			{
				Component->SelectedFixturePatchRef.Add(*SelectedItem.Get());
			}
		}
	}
}

FReply FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixturePatchDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef)
{
	if (UDMXPixelMappingFixtureGroupComponent* Component = GetSelectedFixtureGroupComponent())
	{
		Component->SelectedFixturePatchRef.Add(*InFixturePatchRef.Get());
		return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FixturePatchItemTemplate, Component));
	}

	return FReply::Handled();
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnDMXLibraryChanged()
{
	UpdateFixturePatchRefs();
	RebuildFixturePatchListView();
}

UDMXPixelMappingFixtureGroupComponent* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedFixtureGroupComponent()
{
	const TArray<TWeakObjectPtr<UObject> >& SelectedSelectedObjects = DetailLayout->GetSelectedObjects();
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

UDMXLibrary* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedDMXLibrary()
{
	if (UDMXPixelMappingFixtureGroupComponent * Component = GetSelectedFixtureGroupComponent())
	{
		return Component->DMXLibrary;
	}

	return nullptr;
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::UpdateFixturePatchRefs()
{
	FixturePatchRefs.Empty();
	if (UDMXLibrary* DMXLibrary = GetSelectedDMXLibrary())
	{
		DMXLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([this](UDMXEntityFixturePatch* InFixturePatch)
		{
			FixturePatchRefs.Add(MakeShared<FDMXEntityFixturePatchRef>(InFixturePatch));
		});
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::RebuildFixturePatchListView()
{
	SAssignNew(FixturePatchListView, SListView<TSharedPtr<FDMXEntityFixturePatchRef>>)
		.ListItemsSource(&FixturePatchRefs)
		.SelectionMode(ESelectionMode::Multi)
		.OnSelectionChanged(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnFixtureSelectionChanged)
		.OnGenerateRow(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::GenerateFixturePatchRow);

	FixturePatchListArea->SetContent(
		SNew(SScrollBorder, FixturePatchListView.ToSharedRef())
		[
			FixturePatchListView.ToSharedRef()
		]);
}

#undef LOCTEXT_NAMESPACE

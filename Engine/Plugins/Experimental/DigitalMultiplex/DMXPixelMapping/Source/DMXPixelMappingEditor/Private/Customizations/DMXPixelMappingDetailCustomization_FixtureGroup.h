// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Types/SlateEnums.h"

class FDMXPixelMappingToolkit;
class ITableRow;
class STableViewBase;
class UDMXLibrary;
class UDMXPixelMappingFixtureGroupComponent;
class IDetailLayoutBuilder;
class FDMXPixelMappingComponentTemplate;
class SBorder;
class FReply;

struct FPointerEvent;
struct FGeometry;
struct FDMXEntityFixturePatchRef;

template <typename ItemType>
class SListView;

class FDMXPixelMappingDetailCustomization_FixtureGroup
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
	{
		return MakeShared<FDMXPixelMappingDetailCustomization_FixtureGroup>(InToolkitWeakPtr);
	}

	/** Constructor */
	FDMXPixelMappingDetailCustomization_FixtureGroup(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
		: ToolkitWeakPtr(InToolkitWeakPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	TSharedRef<ITableRow> GenerateFixturePatchRow(TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef, const TSharedRef<STableViewBase>& OwnerTable);

	UDMXPixelMappingFixtureGroupComponent* GetSelectedFixtureGroupComponent();

	UDMXLibrary* GetSelectedDMXLibrary();

	void OnFixtureSelectionChanged(TSharedPtr<FDMXEntityFixturePatchRef> FixturePatchRef, ESelectInfo::Type SelectInfo);

	void OnDMXLibraryChanged();

	void UpdateFixturePatchRefs();

	void RebuildFixturePatchListView();

	FReply OnFixturePatchDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef);

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

private:
	IDetailLayoutBuilder* DetailLayout;
	
	TArray<TSharedPtr<FDMXEntityFixturePatchRef>> FixturePatchRefs;

	TSharedPtr<SListView<TSharedPtr<FDMXEntityFixturePatchRef>>> FixturePatchListView;

	TSharedPtr<FDMXPixelMappingComponentTemplate> FixturePatchItemTemplate;

	TSharedPtr<SBorder> FixturePatchListArea;
};

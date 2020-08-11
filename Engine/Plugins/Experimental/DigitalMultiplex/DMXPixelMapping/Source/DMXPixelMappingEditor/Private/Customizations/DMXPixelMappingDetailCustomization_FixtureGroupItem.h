// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Misc/Attribute.h"
#include "UObject/WeakObjectPtr.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"

class FDMXPixelMappingToolkit;
class IDetailLayoutBuilder;
class ITableRow;
class STableViewBase;
class IPropertyHandle;
class UDMXPixelMappingFixtureGroupItemComponent;

template <typename ItemType>
class SListView;

class FDMXPixelMappingDetailCustomization_FixtureGroupItem
	: public IDetailCustomization
{
private:
	struct FFunctionAttribure
	{
		TSharedPtr<IPropertyHandle> Handle;
		TSharedPtr<IPropertyHandle> ExposeHandle;
		TSharedPtr<IPropertyHandle> InvertHandle;
		TAttribute<EVisibility> Visibility;
	};


public:
	static TSharedRef<IDetailCustomization> MakeInstance(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
	{
		return MakeShared<FDMXPixelMappingDetailCustomization_FixtureGroupItem>(InToolkitWeakPtr);
	}

	/** Constructor */
	FDMXPixelMappingDetailCustomization_FixtureGroupItem(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
		: ToolkitWeakPtr(InToolkitWeakPtr)
	{}

	//~ IDetailCustomization interface begin
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	//~ IPropertyTypeCustomization interface end

private:

	EVisibility GetRGBAttributeRowVisibilty(FFunctionAttribure* Attribute) const;

	EVisibility GetRGBAttributesVisibility() const;

	EVisibility GetMonochromeRowVisibilty(FFunctionAttribure* Attribute) const;

	EVisibility GetMonochromeAttributesVisibility() const;

	TSharedRef<ITableRow> GenerateExposeAndInvertRow(TSharedPtr<FFunctionAttribure> InAtribute, const TSharedRef<STableViewBase>& OwnerTable);

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

private:
	IDetailLayoutBuilder* DetailLayout;

	TWeakObjectPtr<UDMXPixelMappingFixtureGroupItemComponent> FixtureGroupItemComponent;

	TArray<TSharedPtr<FName>> ActiveModeFunctions;

	TArray<TSharedPtr<FFunctionAttribure>> RGBAttributes;

	TArray<TSharedPtr<FFunctionAttribure>> MonochromeAttributes;

	TSharedPtr<SListView<TSharedPtr<FFunctionAttribure>>> ExposeAndInvertListView;
};

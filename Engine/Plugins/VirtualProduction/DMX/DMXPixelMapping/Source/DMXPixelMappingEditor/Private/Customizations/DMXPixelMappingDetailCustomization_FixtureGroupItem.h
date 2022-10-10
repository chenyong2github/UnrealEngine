// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
#include "Misc/Attribute.h"
#include "UObject/WeakObjectPtr.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"

class FDMXPixelMappingToolkit;
class IDetailLayoutBuilder;
class ITableRow;
class STableViewBase;
class IPropertyHandle;
class UDMXEntityFixturePatch;
class UDMXPixelMappingFixtureGroupItemComponent;
enum class EDMXColorMode : uint8;

class IPropertyUitilities;
template <typename ItemType> class SListView;


class FDMXPixelMappingDetailCustomization_FixtureGroupItem
	: public IDetailCustomization
{
private:
	struct FFunctionAttribute
	{
		TSharedPtr<IPropertyHandle> Handle;
		TSharedPtr<IPropertyHandle> ExposeHandle;
		TSharedPtr<IPropertyHandle> InvertHandle;

		/** Returns true if the Attribute Handle has multiple values */
		bool HasMultipleAttributeValues() const;

		/** Gets the value of the Attribute Handle. Should not be called if HasMultipleAttributeValues returns true */
		FName GetAttributeValue() const;

		/** Sets the value of the Attribute Handle */
		void SetAttributeValue(const FName& NewValue);
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
	EVisibility GetRGBAttributesVisibility() const;

	EVisibility GetMonochromeAttributesVisibility() const;

	TSharedRef<ITableRow> GenerateExposeAndInvertRow(TSharedPtr<FFunctionAttribute> InAttribute, const TSharedRef<STableViewBase>& OwnerTable);

	bool CheckComponentsDMXColorMode(const EDMXColorMode DMXColorMode) const;

	/** Creates Details for the Output Modulators */
	void CreateModulatorDetails(IDetailLayoutBuilder& InDetailLayout);

	/** Gets an array of Fixture Patches from Group Item Components */
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> GetFixturePatchFromGroupItemComponents(const TArray<TWeakObjectPtr<UDMXPixelMappingFixtureGroupItemComponent>>& GroupItemComponents);

	/** Forces the layout to redraw */
	void ForceRefresh();

	IDetailLayoutBuilder* DetailLayout;

	TArray<TWeakObjectPtr<UDMXPixelMappingFixtureGroupItemComponent>> FixtureGroupItemComponents;

	TArray<TSharedPtr<FName>> ActiveModeFunctions;

	TArray<TSharedPtr<FFunctionAttribute>> RGBAttributes;

	TArray<TSharedPtr<FFunctionAttribute>> MonochromeAttributes;

	TSharedPtr<SListView<TSharedPtr<FFunctionAttribute>>> ExposeAndInvertListView;

	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;
};

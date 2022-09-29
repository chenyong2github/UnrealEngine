// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Misc/Attribute.h"
#include "UObject/WeakObjectPtr.h"
#include "Layout/Visibility.h"

class FDMXPixelMappingToolkit;
struct FDMXCellAttributeGroup;
class UDMXPixelMappingMatrixComponent;
class UDMXEntityFixturePatch;

enum class EDMXColorMode : uint8;
class ITableRow;
class IPropertyHandle;
class IPropertyUtilities;
class STableViewBase;
template <typename ItemType> class SListView;


class FDMXPixelMappingDetailCustomization_Matrix
	: public IDetailCustomization
{
private:
	struct FDMXCellAttributeGroup
	{
		TSharedPtr<IPropertyHandle> Handle;
		TSharedPtr<IPropertyHandle> ExposeHandle;
		TSharedPtr<IPropertyHandle> InvertHandle;
		TAttribute<EVisibility> Visibility;

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
		return MakeShared<FDMXPixelMappingDetailCustomization_Matrix>(InToolkitWeakPtr);
	}

	/** Constructor */
	FDMXPixelMappingDetailCustomization_Matrix(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
		: ToolkitWeakPtr(InToolkitWeakPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	EVisibility GetRGBAttributeRowVisibilty(FDMXCellAttributeGroup* Attribute) const;

	EVisibility GetRGBAttributesVisibility() const;

	EVisibility GetMonochromeRowVisibilty(FDMXCellAttributeGroup* Attribute) const;

	EVisibility GetMonochromeAttributesVisibility() const;

	TSharedRef<ITableRow> GenerateExposeAndInvertRow(TSharedPtr<FDMXCellAttributeGroup> InAtribute, const TSharedRef<STableViewBase>& OwnerTable);


private:	
	bool CheckComponentsDMXColorMode(const EDMXColorMode DMXColorMode) const;

	/** Creates Details for the Output Modulators */
	void CreateModulatorDetails(IDetailLayoutBuilder& InDetailLayout);

	/** Creates the Details for the Attributes */
	void CreateAttributeDetails(IDetailLayoutBuilder& InDetailLayout);

	/** Forces the layout to redraw */
	void ForceRefresh();

	TArray<TWeakObjectPtr<UDMXPixelMappingMatrixComponent>> MatrixComponents;

	IDetailLayoutBuilder* DetailLayout;

	TArray<TSharedPtr<FName>> ActiveModeFunctions;

	TArray<TSharedPtr<FDMXCellAttributeGroup>> RGBAttributes;

	TArray<TSharedPtr<FDMXCellAttributeGroup>> MonochromeAttributes;

	TSharedPtr<IPropertyHandle> ColorModePropertyHandle;

	TSharedPtr<SListView<TSharedPtr<FDMXCellAttributeGroup>>> ExposeAndInvertListView;

	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;
};

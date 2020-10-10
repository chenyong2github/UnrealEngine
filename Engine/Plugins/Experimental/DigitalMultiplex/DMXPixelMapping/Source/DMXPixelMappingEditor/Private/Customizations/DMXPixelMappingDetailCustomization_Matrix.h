// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Misc/Attribute.h"
#include "UObject/WeakObjectPtr.h"
#include "Layout/Visibility.h"

class FDMXPixelMappingToolkit;
class IPropertyHandle;
class UDMXPixelMappingMatrixComponent;
class UDMXEntityFixturePatch;
class ITableRow;
class STableViewBase;
struct FDMXCellAttributeGroup;
enum class EDMXColorMode : uint8;

template <typename ItemType>
class SListView;

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
	void OnFixturePatchMatrixChanged();

	EVisibility GetRGBAttributeRowVisibilty(FDMXCellAttributeGroup* Attribute) const;

	EVisibility GetRGBAttributesVisibility() const;

	EVisibility GetMonochromeRowVisibilty(FDMXCellAttributeGroup* Attribute) const;

	EVisibility GetMonochromeAttributesVisibility() const;

	TSharedRef<ITableRow> GenerateExposeAndInvertRow(TSharedPtr<FDMXCellAttributeGroup> InAtribute, const TSharedRef<STableViewBase>& OwnerTable);

public:
	static void PopulateActiveModeFunctions(TArray<TSharedPtr<FName>>& OutActiveModeFunctions, const UDMXEntityFixturePatch* InFixturePatch);

	static UDMXEntityFixturePatch* GetFixturePatch(UDMXPixelMappingMatrixComponent* InMatrixComponent);

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

	bool CheckComponentsDMXColorMode(const EDMXColorMode DMXColorMode) const;

private:
	TArray<TWeakObjectPtr<UDMXPixelMappingMatrixComponent>> MatrixComponents;

	IDetailLayoutBuilder* DetailLayout;

	TArray<TSharedPtr<FName>> ActiveModeFunctions;

	TArray<TSharedPtr<FDMXCellAttributeGroup>> RGBAttributes;

	TArray<TSharedPtr<FDMXCellAttributeGroup>> MonochromeAttributes;

	TSharedPtr<IPropertyHandle> ColorModePropertyHandle;

	TSharedPtr<SListView<TSharedPtr<FDMXCellAttributeGroup>>> ExposeAndInvertListView;
};

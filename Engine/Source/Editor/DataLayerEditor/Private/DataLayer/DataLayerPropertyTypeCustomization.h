// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IPropertyTypeCustomization.h"

struct EVisibility;
class FReply;
class SWidget;
class FDragDropOperation;
class UDataLayer;

struct FDataLayerPropertyTypeCustomization : public IPropertyTypeCustomization
{
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:

	void AssignDataLayer(const UDataLayer* InDataLayer);
	void OpenDataLayerBrowser();

	UDataLayer* GetDataLayerFromPropertyHandle() const;
	FText GetDataLayerDescription(const UDataLayer* InDataLayer) const;
	FText GetDataLayerText() const;
	EVisibility GetSelectDataLayerVisibility() const;

	TSharedRef<SWidget> OnGetDataLayerMenu();
	FReply OnSelectDataLayer();
	FReply OnDrop(TSharedPtr<FDragDropOperation> InDragDrop);
	bool OnVerifyDrag(TSharedPtr<FDragDropOperation> InDragDrop);

	TSharedPtr<IPropertyHandle> PropertyHandle;
};


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "PropertyEditorModule.h"
#include "IPropertyTypeCustomization.h"

struct EVisibility;
class FReply;
class SWidget;
class FDragDropOperation;
class FDataLayerDragDropOp;
class UDataLayer;
struct FSlateColor;
struct FSlateBrush;

struct FDataLayerPropertyTypeCustomization : public IPropertyTypeCustomization
{
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:

	void AssignDataLayer(const UDataLayer* InDataLayer);

	UDataLayer* GetDataLayerFromPropertyHandle(FPropertyAccess::Result* OutPropertyAccessResult = nullptr) const;
	FText GetDataLayerText() const;
	FSlateColor GetForegroundColor() const;
	const FSlateBrush* GetDataLayerIcon() const;
	EVisibility GetSelectDataLayerVisibility() const;

	TSharedRef<SWidget> OnGetDataLayerMenu();
	void OnBrowse();
	FReply OnSelectDataLayer();
	FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent);
	bool OnVerifyDrag(TSharedPtr<FDragDropOperation> InDragDrop);
	TSharedPtr<const FDataLayerDragDropOp> GetDataLayerDragDropOp(TSharedPtr<FDragDropOperation> InDragDrop);

	TSharedPtr<IPropertyHandle> PropertyHandle;
};

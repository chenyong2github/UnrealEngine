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

namespace UE
{
namespace MovieScene
{

struct FCVarOverridesPropertyTypeCustomization : public IPropertyTypeCustomization
{
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

private:

	void OnCVarsCommitted(const FText& NewText, ETextCommit::Type);
	void OnCVarsChanged(const FText& NewText);
	FText GetCVarText() const;

	TSharedPtr<IPropertyHandle> PropertyHandle;
};


} // namespace MovieScene
} // namespace UE
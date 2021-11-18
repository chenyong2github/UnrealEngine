// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "IPropertyTypeCustomization.h"

class FDetailWidgetRow;
class IDetailChildrenBuilder;
class IPropertyHandle;
class UAssetImportData;
struct FDatasmithImportInfo;

class FDatasmithImportInfoCustomization : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	
	void OnSourceUriChanged(const FText& NewText, ETextCommit::Type) const;

	/** Access the struct we are editing - returns null if we have more than one. */
	FDatasmithImportInfo* GetEditStruct() const;

	/** Access the outer class that contains this struct */
	UObject* GetOuterClass() const;

	/** Get text for the UI */
	FText GetUriText() const;

private:
	/** Property handle of the property we're editing */
	TSharedPtr<IPropertyHandle> PropertyHandle;
};
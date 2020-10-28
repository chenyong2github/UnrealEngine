// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "IDetailCustomization.h"
#include "GroomComponent.h"

class IDetailLayoutBuilder;
class IDetailCategoryBuilder;

//////////////////////////////////////////////////////////////////////////
// FGroomComponentDetailsCustomization

class FGroomComponentDetailsCustomization : public IDetailCustomization
{
public:
	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	TWeakObjectPtr<class UGroomComponent> GroomComponentPtr;
	IDetailLayoutBuilder* MyDetailLayout;

	void CustomizeDescGroupProperties(IDetailLayoutBuilder& DetailLayout, IDetailCategoryBuilder& StrandsGroupFilesCategory);
	void OnGenerateElementForHairGroup(TSharedRef<IPropertyHandle> StructProperty, int32 GroupIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout);
	void OnResetToDefault(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle);
	void OnValueChanged(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle);
	void SetOverride(int32 GroupIndex, TSharedPtr<IPropertyHandle> ChildHandle, bool bValue);
};

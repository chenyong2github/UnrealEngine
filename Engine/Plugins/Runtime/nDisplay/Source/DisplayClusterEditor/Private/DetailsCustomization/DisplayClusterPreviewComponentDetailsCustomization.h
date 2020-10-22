// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class UDisplayClusterPreviewComponent;
class IDetailCategoryBuilder;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SWidget;
class SSearchableComboBox;


class FDisplayClusterPreviewComponentDetailsCustomization : public IDetailCustomization
{
public:
	FDisplayClusterPreviewComponentDetailsCustomization();

public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& LayoutBuilder) override;
	// End IDetailCustomization interface

protected:
	void BuildLayout();
	void Initialize();
	void BuildPreview();
	void BuildParams();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Preview layout
	//////////////////////////////////////////////////////////////////////////////////////////////
	void AddProjectionPolicyRow();
	void OnProjectionPolicyChanged(TSharedPtr<FString> ProjPolicy, ESelectInfo::Type SelectInfo);
	FText GetSelectedProjPolicyText() const;

protected:
	TSharedPtr<FString> ProjPolicyOptionNone;

	TSharedPtr<IPropertyHandle> PropertyProjectionPolicy;
	TSharedPtr<IPropertyHandle> PropertyApplyWarpBlend;

	TArray<TSharedPtr<FString>>     ProjPolicyOptions;
	TSharedPtr<SSearchableComboBox> ProjPolicyComboBox;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Internals
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Create combobox widget
	TSharedRef<SWidget> CreateComboWidget(TSharedPtr<FString> InItem);

protected:
	// UDisplayClusterPreviewComponent on which we're acting
	TWeakObjectPtr<UDisplayClusterPreviewComponent> EditedObject;

	// Keep a reference to force refresh the layout
	IDetailLayoutBuilder* LayoutBuilder = nullptr;
	
	// Layout categories
	IDetailCategoryBuilder* CategoryPolicy;
	IDetailCategoryBuilder* CategoryPreview;
	IDetailCategoryBuilder* CategoryParameters;

	// Update UI from async task
	FSimpleDelegate RefreshDelegate;
};

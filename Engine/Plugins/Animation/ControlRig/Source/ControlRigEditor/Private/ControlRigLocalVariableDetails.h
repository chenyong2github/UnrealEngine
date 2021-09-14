// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "DetailsViewWrapperObject.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "IPropertyUtilities.h"
#include "DetailsViewWrapperObject.h"
#include "Graph/ControlRigGraphSchema.h"

class IPropertyHandle;

class FRigVMLocalVariableDetails : public IPropertyTypeCustomization
{
	FRigVMLocalVariableDetails()
	: GraphBeingCustomized(nullptr)
	, BlueprintBeingCustomized(nullptr)
	, NameValidator(nullptr, nullptr, NAME_None)
	{}

	
public:

	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FRigVMLocalVariableDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;


private:

	URigVMGraph* GraphBeingCustomized;
	UControlRigBlueprint* BlueprintBeingCustomized;
	FRigVMGraphVariableDescription VariableDescription;
	TArray<TWeakObjectPtr<UDetailsViewWrapperObject>> ObjectsBeingCustomized;

	TSharedPtr<IPropertyHandle> NameHandle;
	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> TypeObjectHandle;
	TSharedPtr<IPropertyHandle> DefaultValueHandle;

	FControlRigLocalVariableNameValidator NameValidator;
	TArray<TSharedPtr<FString>> EnumOptions;

	FText GetName() const;
	void SetName(const FText& InNewText, ETextCommit::Type InCommitType);
	bool OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage);
	
	FEdGraphPinType OnGetPinInfo() const;
	void HandlePinInfoChanged(const FEdGraphPinType& PinType);

	ECheckBoxState HandleBoolDefaultValueIsChecked( ) const;
	void OnBoolDefaultValueChanged(ECheckBoxState InCheckBoxState);
};




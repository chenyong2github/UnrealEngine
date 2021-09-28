// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealClient.h"
#include "IPropertyTypeCustomization.h"
#include "StateTreeCondition.h"

class IPropertyHandle;

/**
 * Type customization for FStateTreeCondition.
 */

class FStateTreeConditionDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	void OnLeftChanged();
	FText GetDescription() const;
	void OnOperatorComboChange(EGenericAICheck Operator);
	TSharedRef<SWidget> OnGetOperatorContent() const;
	FText GetCurrentOperatorDesc() const;

	TOptional<EGenericAICheck> GetOperator() const;
	TOptional<EStateTreeVariableType> GetVariableType(const TSharedPtr<IPropertyHandle>& PropertyHandle) const;
	TOptional<UClass*> GetBaseClass(const TSharedPtr<IPropertyHandle>& PropertyHandle) const;
	void ResetRightVariableType(EStateTreeVariableType NewType, UClass* NewBaseClass);

	TSharedPtr<IPropertyHandle> LeftProperty;
	TSharedPtr<IPropertyHandle> RightProperty;
	TSharedPtr<IPropertyHandle> OperatorProperty;

	TSharedPtr<IPropertyHandle> LeftTypeProperty;
	TSharedPtr<IPropertyHandle> LeftBaseClassProperty;
	TSharedPtr<IPropertyHandle> RightTypeProperty;
	TSharedPtr<IPropertyHandle> RightBaseClassProperty;
	TSharedPtr<IPropertyHandle> RightIDProperty;
	TSharedPtr<IPropertyHandle> RightNameProperty;
	TSharedPtr<IPropertyHandle> RightValueProperty;

	class IPropertyUtilities* PropUtils;
	TSharedPtr<IPropertyHandle> StructProperty;
};

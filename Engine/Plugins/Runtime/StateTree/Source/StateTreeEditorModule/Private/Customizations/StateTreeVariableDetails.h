// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealClient.h"
#include "Widgets/Input/SCheckBox.h"
#include "IPropertyTypeCustomization.h"
#include "StateTreeVariable.h"

class IPropertyHandle;

/**
 * Type customization for FStateTreeVariable and FStateTreeVariable.
 */

class FStateTreeVariableDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader( TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;
	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;

private:

	TOptional<int32> GetIntValue() const;
	void SetIntValue(int32 Value, ETextCommit::Type CommitType);

	TOptional<float> GetFloatValue() const;
	void SetFloatValue(float Value, ETextCommit::Type CommitType);

	ECheckBoxState GetBoolValue() const;
	void SetBoolValue(ECheckBoxState NewCheckedState);

	TOptional<float>  GetVectorXValue() const;
	TOptional<float>  GetVectorYValue() const;
	TOptional<float>  GetVectorZValue() const;
	void SetVectorXValue(float Value, ETextCommit::Type CommitType);
	void SetVectorYValue(float Value, ETextCommit::Type CommitType);
	void SetVectorZValue(float Value, ETextCommit::Type CommitType);

	TOptional<EStateTreeVariableType> GetVariableType() const;
	TOptional<EStateTreeVariableBindingMode> GetBindingMode() const;
	TOptional<UClass*> GetBaseClass() const;

	bool HasValueProperty() const;
	bool IsBound() const;

	void CacheExposedVariables();
	void OnForceRefresh();
	void OnIdentifierChanged(const class UStateTree& StateTree);

	EVisibility IsIntVisible() const;
	EVisibility IsFloatVisible() const;
	EVisibility IsBoolVisible() const;
	EVisibility IsVectorVisible() const;
	EVisibility IsObjectOrVoidVisible() const;
	EVisibility IsVariableVisible() const;

	void OnKeyComboChange(int32 Index);
	TSharedRef<SWidget> OnGetKeyContent() const;
	FText GetCurrentKeyDesc() const;
	const FSlateBrush* GetCurrentKeyIcon() const;

	TSharedPtr<IPropertyHandle> IDProperty;
	TSharedPtr<IPropertyHandle> NameProperty;
	TSharedPtr<IPropertyHandle> TypeProperty;
	TSharedPtr<IPropertyHandle> BaseClassProperty;
	TSharedPtr<IPropertyHandle> BindingModeProperty;
	TSharedPtr<IPropertyHandle> ValueProperty;

	struct FVariableKey
	{
		FName Name;
		FGuid ID;
		EStateTreeVariableType Type;
		TSubclassOf<UObject> BaseClass;
	};

	TArray<FVariableKey> KeyValues;

	class IPropertyUtilities* PropUtils;
	TSharedPtr<IPropertyHandle> StructProperty;
};

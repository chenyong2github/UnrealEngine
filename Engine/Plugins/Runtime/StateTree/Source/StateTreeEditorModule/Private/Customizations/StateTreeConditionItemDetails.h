// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
struct FInstancedStruct;
struct FStateTreeEditorPropertyPath;
struct FStateTreeConditionBase;
class UStateTreeEditorData;
class UStateTree;

/**
 * Property customization for FStateTreeConditionItem.
 */
class FStateTreeConditionItemDetails : public IPropertyTypeCustomization
{
public:

	virtual ~FStateTreeConditionItemDetails();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	bool ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const;
	void ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle);

	FText GetDescription() const;
	void OnChildChanged();
	void OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath);

	FStateTreeConditionBase* GetSingleCondition() const;
	FInstancedStruct* GetSingleStructPtr() const;
	UObject* GetEditorBindingsOwner() const;

	FText GetDisplayValueString() const;
	TSharedRef<SWidget> GenerateStructPicker();
	void OnStructPicked(const UScriptStruct* InStruct);

	void FindOuterObjects();

	UScriptStruct* BaseScriptStruct = nullptr;
	TSharedPtr<class SComboButton> ComboButton;

	UStateTreeEditorData* EditorData = nullptr;
	UStateTree* StateTree = nullptr;

	class IPropertyUtilities* PropUtils = nullptr;
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> TypeProperty;

	FDelegateHandle OnBindingChangedHandle;
};

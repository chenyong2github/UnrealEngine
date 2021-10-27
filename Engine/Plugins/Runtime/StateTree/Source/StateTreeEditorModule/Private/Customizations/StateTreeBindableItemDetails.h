// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealClient.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;

/**
 * Semi-generic type customization for bindable items (Evaluators and Tasks) in StateTreeState.
 */
class FStateTreeBindableItemDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	bool ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const;
	void ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle);

	FText GetName() const;
	void OnNameCommitted(const FText& NewText, ETextCommit::Type InTextCommit);
	bool IsNameEnabled() const;

	const UScriptStruct* GetCommonScriptStruct() const;
	FText GetDisplayValueString() const;
	const FSlateBrush* GetDisplayValueIcon() const;
	TSharedRef<SWidget> GenerateStructPicker();
	void OnStructPicked(const UScriptStruct* InStruct);

	void OnIdentifierChanged(const UStateTree& StateTree);
	void FindOuterObjects();

	UScriptStruct* BaseScriptStruct = nullptr;
	TSharedPtr<class SComboButton> ComboButton;

	UStateTreeEditorData* EditorData = nullptr;
	UStateTree* StateTree = nullptr;

	class IPropertyUtilities* PropUtils = nullptr;
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> TypeProperty;
};

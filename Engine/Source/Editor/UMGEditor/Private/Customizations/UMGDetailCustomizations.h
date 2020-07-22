// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IPropertyHandle;

/**
 * Provides the customization for all UWidgets.  Bindings, style disabling...etc.
 */
class FBlueprintWidgetCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<class IDetailCustomization> MakeInstance(TSharedRef<FWidgetBlueprintEditor> InEditor, UBlueprint* InBlueprint)
	{
		return MakeShareable(new FBlueprintWidgetCustomization(InEditor, InBlueprint));
	}

	FBlueprintWidgetCustomization(TSharedRef<FWidgetBlueprintEditor> InEditor, UBlueprint* InBlueprint)
		: Editor(InEditor)
		, Blueprint(CastChecked<UWidgetBlueprint>(InBlueprint))
	{
	}
	
	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override;

	/** Make a property binding widget */
	static TSharedRef<SWidget> MakePropertyBindingWidget(TWeakPtr<FWidgetBlueprintEditor> InEditor, FDelegateProperty* InProperty, TSharedRef<IPropertyHandle> InDelegatePropertyHandle, bool bInGeneratePureBindings);

private:
	void PerformBindingCustomization(IDetailLayoutBuilder& DetailLayout);

	void CreateEventCustomization( IDetailLayoutBuilder& DetailLayout, FDelegateProperty* Property, UWidget* Widget );

	void CreateMulticastEventCustomization(IDetailLayoutBuilder& DetailLayout, FName ThisComponentName, UClass* PropertyClass, FMulticastDelegateProperty* Property);

	void ResetToDefault_RemoveBinding(TSharedPtr<IPropertyHandle> PropertyHandle);

	FReply HandleAddOrViewEventForVariable(const FName EventName, FName PropertyName, TWeakObjectPtr<UClass> PropertyClass);

	int32 HandleAddOrViewIndexForButton(const FName EventName, FName PropertyName) const;

	void PerformAccessibilityCustomization(IDetailLayoutBuilder& DetailLayout);
	void CustomizeAccessibilityProperty(IDetailLayoutBuilder& DetailLayout, const FName& BehaviorPropertyName, const FName& TextPropertyName);
private:

	TWeakPtr<FWidgetBlueprintEditor> Editor;

	UWidgetBlueprint* Blueprint;
};

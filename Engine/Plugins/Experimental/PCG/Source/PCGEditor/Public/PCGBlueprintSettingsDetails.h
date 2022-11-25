// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "PCGSettings.h"
#include "Elements/PCGExecuteBlueprint.h"

class IDetailLayoutBuilder;
class UFunction;
class UPCGBlueprintElement;

class FPCGBlueprintSettingsDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ~Begin IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	/** ~End IDetailCustomization interface */

private:
	// Creates a button strip in each category that contains reflected functions marked as CallInEditor
	void AddCallInEditorMethods(IDetailLayoutBuilder& DetailBuilder);

	// Executes the specified method on the selected objects
	FReply OnExecuteCallInEditorFunction(TWeakObjectPtr<UFunction> WeakFunctionPtr);

private:
	TWeakObjectPtr<UPCGBlueprintElement> SelectedObject;
};
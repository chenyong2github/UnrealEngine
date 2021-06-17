// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "IPropertyAccessEditor.h"
#include "ControlRigBlueprint.h"

class SControlRigVariableBinding : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SControlRigVariableBinding)
	: _ModelPin(nullptr)
    , _FunctionReferenceNode(nullptr)
    , _InnerVariableName(NAME_None)
    , _Blueprint(nullptr)
	, _CanRemoveBinding(true)
	{}

		SLATE_ARGUMENT(URigVMPin*, ModelPin)
		SLATE_ARGUMENT(URigVMFunctionReferenceNode*, FunctionReferenceNode)
		SLATE_ARGUMENT(FName, InnerVariableName)
		SLATE_ARGUMENT(UControlRigBlueprint*, Blueprint)
		SLATE_ARGUMENT(bool, CanRemoveBinding)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:

	FText GetBindingText() const;
	const FSlateBrush* GetBindingImage() const;
	FLinearColor GetBindingColor() const;
	bool OnCanBindProperty(FProperty* InProperty) const;
	bool OnCanBindToClass(UClass* InClass) const;
	void OnAddBinding(FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain);
	bool OnCanRemoveBinding(FName InPropertyName);
	void OnRemoveBinding(FName InPropertyName);
	void FillLocalVariableMenu( FMenuBuilder& MenuBuilder );
	void HandleBindToLocalVariable(FRigVMGraphVariableDescription InLocalVariable);

	URigVMPin* ModelPin;
	URigVMFunctionReferenceNode* FunctionReferenceNode;
	FName InnerVariableName;
	UControlRigBlueprint* Blueprint;
	FPropertyBindingWidgetArgs BindingArgs;
	bool bCanRemoveBinding;
};

class SControlRigGraphPinVariableBinding : public SGraphPin
{
public:

	SLATE_BEGIN_ARGS(SControlRigGraphPinVariableBinding){}

		SLATE_ARGUMENT(URigVMPin*, ModelPin)
		SLATE_ARGUMENT(UControlRigBlueprint*, Blueprint)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	URigVMPin* ModelPin;
	UControlRigBlueprint* Blueprint;
};

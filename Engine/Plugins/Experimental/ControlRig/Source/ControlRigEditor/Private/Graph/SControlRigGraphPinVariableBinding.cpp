// Copyright Epic Games, Inc. All Rights Reserved.


#include "Graph/SControlRigGraphPinVariableBinding.h"
#include "Widgets/Layout/SBox.h"
#include "EdGraphSchema_K2.h"
#include "DetailLayoutBuilder.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprintGeneratedClass.h"

#define LOCTEXT_NAMESPACE "SControlRigGraphPinVariableBinding"

void SControlRigVariableBinding::Construct(const FArguments& InArgs)
{
	this->ModelPin = InArgs._ModelPin;
	this->FunctionReferenceNode = InArgs._FunctionReferenceNode;
	this->InnerVariableName = InArgs._InnerVariableName;
	this->Blueprint = InArgs._Blueprint;
	this->bCanRemoveBinding = InArgs._CanRemoveBinding;

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

	BindingArgs.CurrentBindingText.BindRaw(this, &SControlRigVariableBinding::GetBindingText);
	BindingArgs.CurrentBindingImage.BindRaw(this, &SControlRigVariableBinding::GetBindingImage);
	BindingArgs.CurrentBindingColor.BindRaw(this, &SControlRigVariableBinding::GetBindingColor);

	BindingArgs.OnCanBindProperty.BindSP(this, &SControlRigVariableBinding::OnCanBindProperty);
	BindingArgs.OnCanBindToClass.BindSP(this, &SControlRigVariableBinding::OnCanBindToClass);

	BindingArgs.OnAddBinding.BindSP(this, &SControlRigVariableBinding::OnAddBinding);
	BindingArgs.OnCanRemoveBinding.BindSP(this, &SControlRigVariableBinding::OnCanRemoveBinding);
	BindingArgs.OnRemoveBinding.BindSP(this, &SControlRigVariableBinding::OnRemoveBinding);

	BindingArgs.bGeneratePureBindings = true;
	BindingArgs.bAllowNewBindings = true;
	BindingArgs.bAllowArrayElementBindings = false;
	BindingArgs.bAllowStructMemberBindings = false;
	BindingArgs.bAllowUObjectFunctions = false;

	this->ChildSlot
	[
		PropertyAccessEditor.MakePropertyBindingWidget(Blueprint, BindingArgs)
	];
}

FText SControlRigVariableBinding::GetBindingText() const
{
	if (ModelPin)
	{
		const FString VariablePath = ModelPin->GetBoundVariablePath();
		return FText::FromString(VariablePath);
	}
	else if(FunctionReferenceNode && !InnerVariableName.IsNone())
	{
		const FName BoundVariable = FunctionReferenceNode->GetOuterVariableName(InnerVariableName);
		if(!BoundVariable.IsNone())
		{
			return FText::FromName(BoundVariable);
		}
	}

	return FText();
}

const FSlateBrush* SControlRigVariableBinding::GetBindingImage() const
{
	static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
	return FEditorStyle::GetBrush(PropertyIcon);
}

FLinearColor SControlRigVariableBinding::GetBindingColor() const
{
	if (Blueprint)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FName BoundVariable(NAME_None);

		if(ModelPin)
		{
			BoundVariable = *ModelPin->GetBoundVariableName();
		}
		else if(FunctionReferenceNode && !InnerVariableName.IsNone())
		{
			BoundVariable = FunctionReferenceNode->GetOuterVariableName(InnerVariableName);
			if(BoundVariable.IsNone())
			{
				return FLinearColor::Red;
			}
		}

		for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
		{
			if (VariableDescription.VarName == BoundVariable)
			{
				return Schema->GetPinTypeColor(VariableDescription.VarType);
			}
		}
	}
	return FLinearColor::White;
}

bool SControlRigVariableBinding::OnCanBindProperty(FProperty* InProperty) const
{
	if (InProperty == BindingArgs.Property)
	{
		return true;
	}

	if (InProperty)
	{
		const FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(InProperty, nullptr);
		if(ModelPin)
		{
			return ModelPin->CanBeBoundToVariable(ExternalVariable);
		}
		else if(FunctionReferenceNode && !InnerVariableName.IsNone())
		{
			TArray<FRigVMExternalVariable> InnerVariables = FunctionReferenceNode->GetContainedGraph()->GetExternalVariables();
			for(const FRigVMExternalVariable& InnerVariable : InnerVariables)
			{
				if(InnerVariable.Name == InnerVariableName)
				{
					if(!InnerVariable.bIsReadOnly && ExternalVariable.bIsReadOnly)
					{
						return false;
					}
					if(InnerVariable.bIsArray != ExternalVariable.bIsArray)
					{
						return false;
					}
					if(InnerVariable.TypeObject && InnerVariable.TypeObject != ExternalVariable.TypeObject)
					{
						return false;
					}
					else if(InnerVariable.TypeName != ExternalVariable.TypeName)
					{
						return false;
					}
					return true;
				}
			}
		}
	}

	return false;
}

bool SControlRigVariableBinding::OnCanBindToClass(UClass* InClass) const
{
	if (InClass)
	{
		return InClass->ClassGeneratedBy == Blueprint;
	}
	return true;
}

void SControlRigVariableBinding::OnAddBinding(FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
{
	if (Blueprint)
	{
		TArray<FString> Parts;
		for (const FBindingChainElement& ChainElement : InBindingChain)
		{
			ensure(ChainElement.Field);
			Parts.Add(ChainElement.Field.GetName());
		}

		if(ModelPin)
		{
			Blueprint->GetController(ModelPin->GetGraph())->BindPinToVariable(ModelPin->GetPinPath(), FString::Join(Parts, TEXT(".")), true /* undo */);
		}
		else if(FunctionReferenceNode && !InnerVariableName.IsNone())
		{
			const FName BoundVariableName = *FString::Join(Parts, TEXT("."));
			Blueprint->GetController(FunctionReferenceNode->GetGraph())->SetRemappedVariable(FunctionReferenceNode, InnerVariableName, BoundVariableName);
		}
	}
}

bool SControlRigVariableBinding::OnCanRemoveBinding(FName InPropertyName)
{
	return bCanRemoveBinding;
}

void SControlRigVariableBinding::OnRemoveBinding(FName InPropertyName)
{
	if (Blueprint)
	{
		if(ModelPin)
		{
			Blueprint->GetController(ModelPin->GetGraph())->UnbindPinFromVariable(ModelPin->GetPinPath(), true /* undo */);
		}
		else if(FunctionReferenceNode && !InnerVariableName.IsNone())
		{
			Blueprint->GetController(FunctionReferenceNode->GetGraph())->SetRemappedVariable(FunctionReferenceNode, InnerVariableName, NAME_None);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SControlRigGraphPinVariableBinding::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	this->ModelPin = InArgs._ModelPin;
	this->Blueprint = InArgs._Blueprint;

	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SControlRigGraphPinVariableBinding::GetDefaultValueWidget()
{
	return SNew(SControlRigVariableBinding)
		.Blueprint(Blueprint)
		.ModelPin(ModelPin);
}

#undef LOCTEXT_NAMESPACE
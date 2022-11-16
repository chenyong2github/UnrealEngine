// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMFunctionReferenceNode)

FString URigVMFunctionReferenceNode::GetNodeTitle() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		return Data->Header.NodeTitle;
	}
	return Super::GetNodeTitle();
}

FLinearColor URigVMFunctionReferenceNode::GetNodeColor() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		return Data->Header.NodeColor;
	}
	return Super::GetNodeColor();
}

FText URigVMFunctionReferenceNode::GetToolTipText() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		return Data->Header.Tooltip;
	}
	return Super::GetToolTipText();
}

FString URigVMFunctionReferenceNode::GetNodeCategory() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		return Data->Header.Category;
	}
	return Super::GetNodeCategory();
}

FString URigVMFunctionReferenceNode::GetNodeKeywords() const
{
	if (const FRigVMGraphFunctionData* Data = GetReferencedFunctionData())
	{
		return Data->Header.Keywords;
	}
	return Super::GetNodeKeywords();
}

bool URigVMFunctionReferenceNode::RequiresVariableRemapping() const
{
	TArray<FRigVMExternalVariable> InnerVariables;
	return RequiresVariableRemappingInternal(InnerVariables);
}

bool URigVMFunctionReferenceNode::RequiresVariableRemappingInternal(TArray<FRigVMExternalVariable>& InnerVariables) const
{
	bool bHostedInDifferencePackage = false;
	
	FRigVMGraphFunctionIdentifier LibraryPointer = ReferencedFunctionHeader.LibraryPointer;
	const FString& LibraryPackagePath = LibraryPointer.LibraryNode.GetLongPackageName();
	const FString& ThisPacakgePath = GetPackage()->GetPathName();
	bHostedInDifferencePackage = LibraryPackagePath != ThisPacakgePath;
		
	if(bHostedInDifferencePackage)
	{
		InnerVariables = ReferencedFunctionHeader.ExternalVariables;
		if(InnerVariables.Num() == 0)
		{
			return false;
		}
	}

	return bHostedInDifferencePackage;
}

bool URigVMFunctionReferenceNode::IsFullyRemapped() const
{
	TArray<FRigVMExternalVariable> InnerVariables;
	if(!RequiresVariableRemappingInternal(InnerVariables))
	{
		return true;
	}

	for(const FRigVMExternalVariable& InnerVariable : InnerVariables)
	{
		const FName InnerVariableName = InnerVariable.Name;
		const FName* OuterVariableName = VariableMap.Find(InnerVariableName);
		if(OuterVariableName == nullptr)
		{
			return false;
		}
		check(!OuterVariableName->IsNone());
	}

	return true;
}

TArray<FRigVMExternalVariable> URigVMFunctionReferenceNode::GetExternalVariables() const
{
	return GetExternalVariables(true);
}

TArray<FRigVMExternalVariable> URigVMFunctionReferenceNode::GetExternalVariables(bool bRemapped) const
{
	TArray<FRigVMExternalVariable> Variables;
	
	if(!bRemapped)
	{
		Variables = ReferencedFunctionHeader.ExternalVariables;
	}
	else
	{
		if(RequiresVariableRemappingInternal(Variables))
		{
			for(FRigVMExternalVariable& Variable : Variables)
			{
				const FName* OuterVariableName = VariableMap.Find(Variable.Name);
				if(OuterVariableName != nullptr)
				{
					check(!OuterVariableName->IsNone());
					Variable.Name = *OuterVariableName;
				}
			}
		}
	}
	
	return Variables; 
}

FName URigVMFunctionReferenceNode::GetOuterVariableName(const FName& InInnerVariableName) const
{
	if(const FName* OuterVariableName = VariableMap.Find(InInnerVariableName))
	{
		return *OuterVariableName;
	}
	return NAME_None;
}

const FRigVMGraphFunctionData* URigVMFunctionReferenceNode::GetReferencedFunctionData() const
{
	if (IRigVMGraphFunctionHost* Host = ReferencedFunctionHeader.GetFunctionHost())
	{
		return Host->GetRigVMGraphFunctionStore()->FindFunction(ReferencedFunctionHeader.LibraryPointer);
	}
	return nullptr;
}

FText URigVMFunctionReferenceNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	check(InPin);

	URigVMPin* RootPin = InPin->GetRootPin();
	const FRigVMGraphFunctionArgument* Argument = ReferencedFunctionHeader.Arguments.FindByPredicate([RootPin](const FRigVMGraphFunctionArgument& Argument)
	{
		return Argument.Name == RootPin->GetFName();
	});

	if (Argument)
	{
		if (const FText* Tooltip = Argument->PathToTooltip.Find(InPin->GetSegmentPath(false)))
		{
			return *Tooltip;
		}
	}
	
	return Super::GetToolTipTextForPin(InPin);
}

URigVMLibraryNode* URigVMFunctionReferenceNode::LoadReferencedNode() const
{
	UObject* LibraryNode = ReferencedFunctionHeader.LibraryPointer.LibraryNode.ResolveObject();
	if (!LibraryNode)
	{
		LibraryNode = ReferencedFunctionHeader.LibraryPointer.LibraryNode.TryLoad();
	}
	return Cast<URigVMLibraryNode>(LibraryNode);
	
}

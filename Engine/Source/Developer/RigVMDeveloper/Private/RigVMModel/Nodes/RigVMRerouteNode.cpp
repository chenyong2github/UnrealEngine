// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMRerouteNode.h"

const FString URigVMRerouteNode::RerouteName = TEXT("Reroute");
const FString URigVMRerouteNode::RerouteArrayName = TEXT("RerouteArray");
const FString URigVMRerouteNode::ValueName = TEXT("Value");

URigVMRerouteNode::URigVMRerouteNode()
: bShowAsFullNode(true)
{
}

FString URigVMRerouteNode::GetNodeTitle() const
{
	if(const URigVMPin* ValuePin = FindPin(ValueName))
	{
		FString TypeDisplayName;
		if(const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(ValuePin->GetCPPTypeObject()))
		{
			TypeDisplayName = ScriptStruct->GetDisplayNameText().ToString();
		}
		else if(const UEnum* Enum = Cast<UEnum>(ValuePin->GetCPPTypeObject()))
		{
			TypeDisplayName = Enum->GetName();
		}
		else if(const UClass* Class = Cast<UClass>(ValuePin->GetCPPTypeObject()))
		{
			TypeDisplayName = Class->GetDisplayNameText().ToString();
		}
		else if(ValuePin->IsArray())
		{
			TypeDisplayName = ValuePin->GetArrayElementCppType();
		}
		else
		{
			TypeDisplayName = ValuePin->GetCPPType();
		}

		if(TypeDisplayName.IsEmpty())
		{
			return RerouteName;
		}

		TypeDisplayName = TypeDisplayName.Left(1).ToUpper() + TypeDisplayName.Mid(1);

		if(ValuePin->IsArray())
		{
			TypeDisplayName += TEXT(" Array");
		}

		return TypeDisplayName;
	}
	return RerouteName;
}

bool URigVMRerouteNode::GetShowsAsFullNode() const
{
	return bShowAsFullNode;
}

FLinearColor URigVMRerouteNode::GetNodeColor() const
{
	return FLinearColor::White;
}

FName URigVMRerouteNode::GetNotation() const
{
	return GetTemplate()->GetNotation();
}

const FRigVMTemplate* URigVMRerouteNode::GetTemplate() const
{
	if(CachedTemplate == nullptr)
	{
		bool bIsArray = false;
		if(const URigVMPin* ValuePin = FindPin(ValueName))
		{
			bIsArray = ValuePin->IsArray();
		}
		CachedTemplate = FindOrAddTemplate(bIsArray);
	}
	return CachedTemplate;
}

const FRigVMTemplate* URigVMRerouteNode::FindOrAddTemplate(bool bIsArray)
{
	TArray<FRigVMTemplateArgument> Arguments;
	Arguments.Emplace(TEXT("Value"), ERigVMPinDirection::IO, bIsArray ? FRigVMTemplateArgument::FType::Array() : FRigVMTemplateArgument::FType());
		
	return FRigVMRegistry::Get().GetOrAddTemplateFromArguments(
		bIsArray ? *RerouteArrayName : *RerouteName, Arguments, true);
}

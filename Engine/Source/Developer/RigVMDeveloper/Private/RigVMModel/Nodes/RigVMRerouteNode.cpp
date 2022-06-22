// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMModel/RigVMGraph.h"

const FString URigVMRerouteNode::RerouteName = TEXT("Reroute");
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
	static constexpr TCHAR Format[] = TEXT("%s(io %s)");
	static const FName RerouteNotation = *FString::Printf(Format, *RerouteName, *ValueName);
	return RerouteNotation;
}

const FRigVMTemplate* URigVMRerouteNode::GetTemplate() const
{
	if(const FRigVMTemplate* SuperTemplate = Super::GetTemplate())
	{
		return SuperTemplate;
	}
	
	if(CachedTemplate == nullptr)
	{
		static const FRigVMTemplate* RerouteNodeTemplate = nullptr;
		if(RerouteNodeTemplate)
		{
			return RerouteNodeTemplate;
		}

		static TArray<FRigVMTemplateArgument> Arguments;
		if(Arguments.IsEmpty())
		{
			static TArray<FRigVMTemplateArgumentType> Types;
			if(Types.IsEmpty())
			{
				Types.Append(FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue));
				Types.Append(FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue));
				Types.Append(FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue));

				UScriptStruct* ExecuteStruct = GetGraph()->GetExecuteContextStruct();
				Types.Add(FRigVMTemplateArgumentType(ExecuteStruct->GetStructCPPName(), ExecuteStruct));
			}
		
			Arguments.Emplace(TEXT("Value"), ERigVMPinDirection::IO, Types);
		}
		RerouteNodeTemplate = CachedTemplate = FRigVMRegistry::Get().GetOrAddTemplateFromArguments(*RerouteName, Arguments);
	}
	return CachedTemplate;
}

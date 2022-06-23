// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMIfNode.h"

const FString URigVMIfNode::IfName = TEXT("If");
const FString URigVMIfNode::ConditionName = TEXT("Condition");
const FString URigVMIfNode::TrueName = TEXT("True");
const FString URigVMIfNode::FalseName = TEXT("False");
const FString URigVMIfNode::ResultName = TEXT("Result");


FName URigVMIfNode::GetNotation() const
{
	static constexpr TCHAR Format[] = TEXT("%s(in %s, in %s, in %s, out %s)");
	static const FName Notation = *FString::Printf(Format, *IfName, *ConditionName, *TrueName, *FalseName, *ResultName);
	return Notation;
}

const FRigVMTemplate* URigVMIfNode::GetTemplate() const
{
	if(const FRigVMTemplate* SuperTemplate = Super::GetTemplate())
	{
		return SuperTemplate;
	}
	
	if(CachedTemplate == nullptr)
	{
		static const FRigVMTemplate* IfNodeTemplate = nullptr;
		if(IfNodeTemplate)
		{
			return IfNodeTemplate;
		}
		
		static TArray<FRigVMTemplateArgument> Arguments;
		if(Arguments.IsEmpty())
		{
			static TArray<FRigVMTemplateArgumentType> Types;
			if(Types.IsEmpty())
			{
				Types.Reserve(3);
				Types.Append(FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue));
				Types.Append(FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue));
			}

			Arguments.Reserve(4);
			Arguments.Emplace(*ConditionName, ERigVMPinDirection::Input, FRigVMTemplateArgumentType(RigVMTypeUtils::BoolType, nullptr));
			Arguments.Emplace(*TrueName, ERigVMPinDirection::Input, Types);
			Arguments.Emplace(*FalseName, ERigVMPinDirection::Input, Types);
			Arguments.Emplace(*ResultName, ERigVMPinDirection::Output, Types);
		}
		
		IfNodeTemplate = CachedTemplate = FRigVMRegistry::Get().GetOrAddTemplateFromArguments(*IfName, Arguments);
	}
	return CachedTemplate;
}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMSelectNode.h"

const FString URigVMSelectNode::SelectName = TEXT("Select");
const FString URigVMSelectNode::IndexName = TEXT("Index");
const FString URigVMSelectNode::ValueName = TEXT("Values");
const FString URigVMSelectNode::ResultName = TEXT("Result");

bool URigVMSelectNode::AllowsLinksOn(const URigVMPin* InPin) const
{
	if(InPin->GetRootPin() == InPin)
	{
		if(InPin->GetName() == ValueName)
		{
			return false;
		}
	}

	return true;
}

FName URigVMSelectNode::GetNotation() const
{
	static constexpr TCHAR Format[] = TEXT("%s(in %s, in %s, out %s)");
	static const FName Notation = *FString::Printf(Format, *SelectName, *IndexName, *ValueName, *ResultName);
	return Notation;
}

const FRigVMTemplate* URigVMSelectNode::GetTemplate() const
{
	if(const FRigVMTemplate* SuperTemplate = Super::GetTemplate())
	{
		return SuperTemplate;
	}
	
	if(CachedTemplate == nullptr)
	{
		static const FRigVMTemplate* SelectNodeTemplate = nullptr;
		if(SelectNodeTemplate)
		{
			return SelectNodeTemplate;
		}

		static TArray<FRigVMTemplateArgument> Arguments;
		if(Arguments.IsEmpty())
		{
			static const TArray<FRigVMTemplateArgumentType>& SingleTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
			static const TArray<FRigVMTemplateArgumentType>& ArrayTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
			static const TArray<FRigVMTemplateArgumentType>& ArrayArrayTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue);

			static TArray<FRigVMTemplateArgumentType> ResultTypes, ValueTypes;
			if(ResultTypes.IsEmpty())
			{
				ResultTypes.Append(SingleTypes);
				ResultTypes.Append(ArrayTypes);
				ValueTypes.Append(ArrayTypes);
				ValueTypes.Append(ArrayArrayTypes);
			}

			Arguments.Reserve(3);
			Arguments.Emplace(*IndexName, ERigVMPinDirection::Input, FRigVMTemplateArgumentType(RigVMTypeUtils::Int32Type, nullptr));
			Arguments.Emplace(*ValueName, ERigVMPinDirection::Input, ValueTypes);
			Arguments.Emplace(*ResultName, ERigVMPinDirection::Output, ResultTypes);
		}
		SelectNodeTemplate = CachedTemplate = FRigVMRegistry::Get().GetOrAddTemplateFromArguments(*SelectName, Arguments);
	}
	return CachedTemplate;
}
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
		const TArray<FRigVMTemplateArgument::FType>& SingleTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
		const TArray<FRigVMTemplateArgument::FType>& ArrayTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
		const TArray<FRigVMTemplateArgument::FType>& ArrayArrayTypes = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue);

		TArray<FRigVMTemplateArgument::FType> ResultTypes = SingleTypes;
		ResultTypes.Append(ArrayTypes);
		TArray<FRigVMTemplateArgument::FType> ValueTypes = ArrayTypes;
		ValueTypes.Append(ArrayArrayTypes);		
		
		TArray<FRigVMTemplateArgument> Arguments;
		Arguments.Emplace(*IndexName, ERigVMPinDirection::Input, FRigVMTemplateArgument::FType(RigVMTypeUtils::Int32Type, nullptr));
		Arguments.Emplace(*ValueName, ERigVMPinDirection::Input, ValueTypes);
		Arguments.Emplace(*ResultName, ERigVMPinDirection::Output, ResultTypes);
		
		CachedTemplate = FRigVMRegistry::Get().GetOrAddTemplateFromArguments(*SelectName, Arguments);
	}
	return CachedTemplate;
}
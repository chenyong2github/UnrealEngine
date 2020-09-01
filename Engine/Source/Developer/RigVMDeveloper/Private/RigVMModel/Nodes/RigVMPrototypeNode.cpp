// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMPrototypeNode.h"

URigVMPrototypeNode::URigVMPrototypeNode()
	: Super()
	, PrototypeNotation(NAME_None)
	, CachedPrototype(nullptr)
{
}

FText URigVMPrototypeNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	if (const FRigVMPrototype* Prototype = GetPrototype())
	{
		const URigVMPin* RootPin = InPin->GetRootPin();
		if (RootPin->GetCPPType().IsEmpty())
		{
			if (const FRigVMPrototypeArg* Arg = Prototype->FindArg(RootPin->GetFName()))
			{
				FString SupportedTypesJoined = FString::Join(Arg->GetSupportedTypeStrings(), TEXT("\n"));
				return FText::FromString(TEXT("Supported Types:\n\n") + SupportedTypesJoined);
			}
		}
	}
	return Super::GetToolTipTextForPin(InPin);
}

FName URigVMPrototypeNode::GetNotation() const
{
	return PrototypeNotation;
}

bool URigVMPrototypeNode::SupportsType(const URigVMPin* InPin, const FString& InCPPType)
{
	const URigVMPin* RootPin = InPin->GetRootPin();
	if (RootPin->GetCPPType().IsEmpty())
	{
		FString CacheKey = RootPin->GetName() + TEXT("|") + InCPPType;
		const bool* CachedResult = SupportedTypesCache.Find(CacheKey);
		if (CachedResult)
		{
			return *CachedResult;
		}

		if (const FRigVMPrototype* Prototype = GetPrototype())
		{
			FRigVMPrototype::FTypeMap Types;

			for (URigVMPin* Pin : GetPins())
			{
				if (Pin == RootPin)
				{
					continue;
				}

				if (Pin->GetCPPType().IsEmpty())
				{
					continue;
				}

				FRigVMPrototypeArg::FType Type(Pin->GetCPPType(), Pin->GetCPPTypeObject());
				Types.Add(Pin->GetFName(), Type);
			}

			bool Result = Prototype->ArgSupportsType(RootPin->GetFName(), InCPPType, Types);
			SupportedTypesCache.Add(CacheKey, Result);
			return Result;
		}
	}

	return RootPin->GetCPPType() == InCPPType;
}

int32 URigVMPrototypeNode::GetResolvedFunctionIndex(FRigVMPrototype::FTypeMap* OutTypes)
{
	if (const FRigVMPrototype* Prototype = GetPrototype())
	{
		FRigVMPrototype::FTypeMap Types;

		for (URigVMPin* Pin : GetPins())
		{
			if (Pin->GetCPPType().IsEmpty())
			{
				continue;
			}

			FRigVMPrototypeArg::FType Type(Pin->GetCPPType(), Pin->GetCPPTypeObject());
			Types.Add(Pin->GetFName(), Type);
		}

		int32 FunctionIndex = INDEX_NONE;
		Prototype->Resolve(Types, FunctionIndex);

		if (OutTypes)
		{
			*OutTypes = Types;
		}

		return FunctionIndex;
	}

	return INDEX_NONE;
}

const FRigVMPrototype* URigVMPrototypeNode::GetPrototype() const
{
	if(CachedPrototype == nullptr)
	{
		CachedPrototype = FRigVMRegistry::Get().FindPrototype(PrototypeNotation);
	}
	return CachedPrototype;
}

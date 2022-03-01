// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMTemplateNode.h"

URigVMTemplateNode::URigVMTemplateNode()
	: Super()
	, TemplateNotation(NAME_None)
	, ResolvedFunctionName()
	, CachedTemplate(nullptr)
	, CachedFunction(nullptr)
{
}

UScriptStruct* URigVMTemplateNode::GetScriptStruct() const
{
	if(const FRigVMFunction* Function = GetResolvedFunction())
	{
		return Function->Struct;
	}
	return nullptr;
}

FString URigVMTemplateNode::GetNodeTitle() const
{
	if(!IsResolved())
	{
		if(const FRigVMTemplate* Template = GetTemplate())
		{
			return Template->GetName().ToString();
		}
	}
	
	FString ResolvedNodeTitle = Super::GetNodeTitle();

	const int32 BracePos = ResolvedNodeTitle.Find(TEXT(" ("));
	if(BracePos != INDEX_NONE)
	{
		ResolvedNodeTitle = ResolvedNodeTitle.Left(BracePos);
	}

	return ResolvedNodeTitle;
}

FName URigVMTemplateNode::GetMethodName() const
{
	if(const FRigVMFunction* Function = GetResolvedFunction())
	{
		return Function->GetMethodName();
	}
	return NAME_None;
}

FText URigVMTemplateNode::GetToolTipText() const
{
	if(const FRigVMTemplate* Template = GetTemplate())
	{
		const TArray<int32> PermutationIndices = GetResolvedPermutationIndices();
		return Template->GetTooltipText(PermutationIndices);
	}
	return Super::GetToolTipText();
}

FText URigVMTemplateNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	const FText SuperToolTip = Super::GetToolTipTextForPin(InPin);
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		const URigVMPin* RootPin = InPin->GetRootPin();
		if (RootPin->IsWildCard())
		{
			if (const FRigVMTemplateArgument* Arg = Template->FindArgument(RootPin->GetFName()))
			{
				const TArray<int32> PermutationIndices = GetResolvedPermutationIndices();
				const FString SupportedTypesJoined = FString::Join(Arg->GetSupportedTypeStrings(PermutationIndices), TEXT("\n"));
				FString Tooltip = TEXT("Supported Types:\n\n") + SupportedTypesJoined;

				if(!SuperToolTip.IsEmpty())
				{
					Tooltip += TEXT("\n\n") + SuperToolTip.ToString();
				}
				
				return FText::FromString(Tooltip);
			}
		}
	}
	return SuperToolTip;
}

FName URigVMTemplateNode::GetNotation() const
{
	return TemplateNotation;
}

bool URigVMTemplateNode::SupportsType(const URigVMPin* InPin, const FString& InCPPType, FString* OutCPPType)
{
	static const FString WildCardCPPType = RigVMTypeUtils::GetWildCardCPPType();
	static const FString WildCardArrayCPPType = RigVMTypeUtils::ArrayTypeFromBaseType(WildCardCPPType);

	// we always support the unknown type
	if(((InCPPType == WildCardCPPType) && !InPin->IsArray()) ||
		((InCPPType == WildCardArrayCPPType) && InPin->IsArray()))
	{
		if(OutCPPType)
		{
			*OutCPPType = InCPPType;
		}
		return true;
	}
	
	FString CPPType = InCPPType;
	const URigVMPin* RootPin = InPin->GetRootPin();

	if(InPin->GetParentPin() == RootPin && RootPin->IsArray())
	{
		CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(CPPType);
	}
	
	if (RootPin->IsWildCard())
	{
		const FString CacheKey = RootPin->GetName() + TEXT("|") + CPPType;
		if (const TPair<bool, FRigVMTemplateArgument::FType>* CachedResult = SupportedTypesCache.Find(CacheKey))
		{
			if(OutCPPType)
			{
				*OutCPPType = CachedResult->Value.CPPType;
			}
			return CachedResult->Key;
		}

		if (const FRigVMTemplate* Template = GetTemplate())
		{
			FRigVMTemplate::FTypeMap Types;

			for (URigVMPin* Pin : GetPins())
			{
				if (Pin == RootPin)
				{
					continue;
				}

				if (Pin->IsWildCard())
				{
					continue;
				}

				FRigVMTemplateArgument::FType Type(Pin->GetCPPType(), Pin->GetCPPTypeObject());
				Types.Add(Pin->GetFName(), Type);
			}

			FRigVMTemplateArgument::FType ResolvedType;
			bool Result = Template->ArgumentSupportsType(RootPin->GetFName(), CPPType, Types, &ResolvedType);
			SupportedTypesCache.Add(CacheKey, TPair<bool, FRigVMTemplateArgument::FType>(Result, ResolvedType));
			if(OutCPPType)
			{
				*OutCPPType = ResolvedType.CPPType;
			}
			return Result;
		}
	}

	if(RootPin->GetCPPType() == CPPType)
	{
		if(OutCPPType)
		{
			*OutCPPType = CPPType;
		}
		return true;
	}
	return false;
}

TArray<int32> URigVMTemplateNode::GetResolvedPermutationIndices(FRigVMTemplate::FTypeMap* OutTypes) const
{
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		FRigVMTemplate::FTypeMap Types;

		for (URigVMPin* Pin : GetPins())
		{
			if (Pin->IsWildCard())
			{
				continue;
			}

			FRigVMTemplateArgument::FType Type(Pin->GetCPPType(), Pin->GetCPPTypeObject());
			Types.Add(Pin->GetFName(), Type);
		}

		TArray<int32> PermutationIndices;
		Template->Resolve(Types, PermutationIndices, false);

		if (OutTypes)
		{
			*OutTypes = Types;
		}

		return PermutationIndices;
	}
	return TArray<int32>();
}

TArray<const FRigVMFunction*> URigVMTemplateNode::GetResolvedPermutations(FRigVMTemplate::FTypeMap* OutTypes) const
{
	TArray<int32> Indices = GetResolvedPermutationIndices(OutTypes);
	TArray<const FRigVMFunction*> Functions;
	for(const int32 Index : Indices)
	{
		Functions.Add(GetTemplate()->GetPermutation(Index));
	}
	return Functions;
}

const FRigVMTemplate* URigVMTemplateNode::GetTemplate() const
{
	if(CachedTemplate == nullptr)
	{
		CachedTemplate = FRigVMRegistry::Get().FindTemplate(TemplateNotation);
	}
	return CachedTemplate;
}

const FRigVMFunction* URigVMTemplateNode::GetResolvedFunction() const
{
	if(CachedFunction == nullptr)
	{
		if(!ResolvedFunctionName.IsEmpty())
		{
			CachedFunction = FRigVMRegistry::Get().FindFunction(*ResolvedFunctionName);
		}

		if(CachedFunction == nullptr)
		{
			TArray<int32> PermutationIndices = GetResolvedPermutationIndices();
			if(PermutationIndices.Num() == 1)
			{
				CachedFunction = GetTemplate()->GetPermutation(PermutationIndices[0]);
			}
		}
	}
	return CachedFunction;
}

bool URigVMTemplateNode::IsResolved() const
{
	return GetScriptStruct() != nullptr;
}

void URigVMTemplateNode::InvalidateCache()
{
	SupportedTypesCache.Reset();
	CachedFunction = nullptr;

	for(URigVMPin* Pin : GetPins())
	{
		if(Pin->IsWildCard())
		{
			ResolvedFunctionName.Reset();
			break;
		}
	}
}

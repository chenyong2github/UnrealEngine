// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMTemplateNode.h"

#include "RigVMModel/RigVMController.h"

URigVMTemplateNode::URigVMTemplateNode()
	: Super()
	, TemplateNotation(NAME_None)
	, ResolvedFunctionName()
	, CachedTemplate(nullptr)
	, CachedFunction(nullptr)
{
}

void URigVMTemplateNode::PostLoad()
{
	Super::PostLoad();

	// if there are brackets in the notation remove them
	const FString OriginalNotation = TemplateNotation.ToString();
	const FString SanitizedNotation = OriginalNotation.Replace(TEXT("[]"), TEXT(""));
	if(OriginalNotation != SanitizedNotation)
	{
		TemplateNotation = *SanitizedNotation;
	}
	
	InvalidateCache();
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
				FString Tooltip;

				const TArray<int32> PermutationIndices = GetResolvedPermutationIndices();
				if(PermutationIndices.Num() == GetTemplate()->NumPermutations())
				{
					if(Arg->GetTypes().Num() > 100)
					{
						Tooltip = TEXT("Supports any type.");
					}
				}
				
				if(Tooltip.IsEmpty())
				{
					const FString SupportedTypesJoined = FString::Join(Arg->GetSupportedTypeStrings(PermutationIndices), TEXT("\n"));
					Tooltip = TEXT("Supported Types:\n\n") + SupportedTypesJoined;
				}

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

	const URigVMPin* RootPin = InPin->GetRootPin();

	FString CPPType = InCPPType;

	if(InPin->GetParentPin() == RootPin && RootPin->IsArray())
	{
		CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(CPPType);
	}

	// we always support the unknown type
	if((InCPPType == WildCardCPPType) || (InCPPType == WildCardArrayCPPType))
	{
		if(const FRigVMTemplate* Template = GetTemplate())
		{
			if(const FRigVMTemplateArgument* Argument = Template->FindArgument(RootPin->GetFName()))
			{
				// support this only on non-singleton arguments
				if(Argument->IsSingleton())
				{
					return false;
				}

				if(RigVMTypeUtils::IsArrayType(CPPType))
				{
					if(Argument->GetArrayType() == FRigVMTemplateArgument::EArrayType_SingleValue)
					{
						return false;
					}
				}
				else
				{
					if(Argument->GetArrayType() == FRigVMTemplateArgument::EArrayType_ArrayValue)
					{
						return false;
					}
				}
				
				if(OutCPPType)
				{
					*OutCPPType = InCPPType;
				}
				return true;
			}
		}
		return false;
	}
	
	if (const FRigVMTemplate* Template = GetTemplate())
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
			Types.Add(Pin->GetFName(), Pin->GetTemplateArgumentType());
		}

		FRigVMTemplateArgument::FType ResolvedType;
		bool bSupportsType = false;
		if(RootPin->IsWildCard())
		{
			bSupportsType = Template->ArgumentSupportsType(RootPin->GetFName(), CPPType, Types, &ResolvedType);
		}
		else
		{
			FRigVMTemplate::FTypeMap ResolvedTypes;
			bSupportsType = Template->ResolveArgument(RootPin->GetFName(), FRigVMTemplateArgument::FType(CPPType), ResolvedTypes);
			if(bSupportsType)
			{
				ResolvedType = ResolvedTypes.FindChecked(RootPin->GetFName());
			}
		}
	
		SupportedTypesCache.Add(CacheKey, TPair<bool, FRigVMTemplateArgument::FType>(bSupportsType, ResolvedType));
		if(OutCPPType)
		{
			*OutCPPType = ResolvedType.CPPType;
		}
		return bSupportsType;
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

bool URigVMTemplateNode::GetTypeMapForNewPinType(const URigVMPin* InPin, const FString& InCPPType, UObject* InCPPTypeObject,
	FRigVMTemplate::FTypeMap& OutTypes) const
{
	check(InPin);
	check(InPin->GetNode() == this);
	check(InPin->IsRootPin());
	
	if(const FRigVMTemplate* Template = GetTemplate())
	{
		OutTypes = GetResolvedTypes();
		const FRigVMTemplateArgument::FType ExpectedType(InCPPType, InCPPTypeObject);
		return GetTemplate()->ResolveArgument(InPin->GetFName(), ExpectedType, OutTypes);
	}

	OutTypes.Reset();
	return false;
}

const TArray<int32>& URigVMTemplateNode::GetResolvedPermutationIndices(FRigVMTemplate::FTypeMap* OutTypes) const
{
	if(!ResolvedPermutations.IsEmpty())
	{
		return ResolvedPermutations;
	}
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		FRigVMTemplate::FTypeMap Types = GetResolvedTypes();
		Template->Resolve(Types, ResolvedPermutations, false);

		if (OutTypes)
		{
			*OutTypes = Types;
		}
	}
	return ResolvedPermutations;
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
		CachedTemplate = FRigVMRegistry::Get().FindTemplate(GetNotation());
	}
	return CachedTemplate;
}

FRigVMTemplate::FTypeMap URigVMTemplateNode::GetResolvedTypes() const
{
	FRigVMTemplate::FTypeMap Types;
	for (URigVMPin* Pin : GetPins())
	{
		if (Pin->IsWildCard())
		{
			continue;
		}
		Types.Add(Pin->GetFName(), Pin->GetTemplateArgumentType());
	}
	return Types;
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

bool URigVMTemplateNode::IsFullyUnresolved() const
{
	check(GetTemplate());

	// all permutations are available means we haven't resolved any wildcard pin
	return GetResolvedPermutations().Num() == GetTemplate()->NumPermutations();
}

FString URigVMTemplateNode::GetInitialDefaultValueForPin(const FName& InRootPinName, const TArray<int32>& InPermutationIndices) const
{
	if(GetTemplate() == nullptr)
	{
		return FString();
	}
	
	TArray<int32> PermutationIndices = InPermutationIndices;
	if(PermutationIndices.IsEmpty())
	{
		PermutationIndices = GetResolvedPermutationIndices();
	}

	FString DefaultValue;

	for(const int32 PermutationIndex : PermutationIndices)
	{
		FString NewDefaultValue;
		
		if(const FRigVMFunction* Permutation = GetTemplate()->GetPermutation(PermutationIndex))
		{
			const TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(Permutation->Struct));
			const FRigVMStruct* DefaultStruct = (const FRigVMStruct*)StructOnScope->GetStructMemory();

			NewDefaultValue = DefaultStruct->ExportToFullyQualifiedText(
				Cast<UScriptStruct>(StructOnScope->GetStruct()), InRootPinName);
		}
		else
		{
			const FRigVMTemplate* Template = GetTemplate();
			const FRigVMTemplateArgument* Argument = Template->FindArgument(InRootPinName);
			const FRigVMTemplateArgument::FType Type = Argument->GetTypes()[PermutationIndex];

			if (Type.IsArray())
			{
				NewDefaultValue = TEXT("()");
			}
			else
			{
				if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Type.CPPTypeObject))
				{
					TArray<uint8, TAlignedHeapAllocator<16>> TempBuffer;
					TempBuffer.AddUninitialized(ScriptStruct->GetStructureSize());

					// call the struct constructor to initialize the struct
					ScriptStruct->InitializeDefaultValue(TempBuffer.GetData());

					ScriptStruct->ExportText(NewDefaultValue, TempBuffer.GetData(), nullptr, nullptr, PPF_None, nullptr);
					ScriptStruct->DestroyStruct(TempBuffer.GetData());				
				}
				else if (UEnum* Enum = Cast<UEnum>(Type.CPPTypeObject))
				{
					NewDefaultValue = Enum->GetNameStringByValue(0);
				}
				else if(UClass* Class = Cast<UClass>(Type.CPPTypeObject))
				{
					// not supporting objects yet
					ensure(false);
				}
				else if (Type.CPPType == RigVMTypeUtils::FloatType)
				{
					NewDefaultValue = TEXT("0.000000");				
				}
				else if (Type.CPPType == RigVMTypeUtils::DoubleType)
				{
					NewDefaultValue = TEXT("0.000000");
				}
				else if (Type.CPPType == RigVMTypeUtils::Int32Type)
				{
					NewDefaultValue = TEXT("0");
				}
				else if (Type.CPPType == RigVMTypeUtils::BoolType)
				{
					NewDefaultValue = TEXT("False");
				}
				else if (Type.CPPType == RigVMTypeUtils::FStringType)
				{
					NewDefaultValue = TEXT("");
				}
				else if (Type.CPPType == RigVMTypeUtils::FNameType)
				{
					NewDefaultValue = TEXT("");
				}
			}			
		}

		if(!NewDefaultValue.IsEmpty())
		{
			if(DefaultValue.IsEmpty())
			{
				DefaultValue = NewDefaultValue;
			}
			else if(!NewDefaultValue.Equals(DefaultValue))
			{
				return FString();
			}
		}
	}
	
	return DefaultValue;
}

FName URigVMTemplateNode::GetDisplayNameForPin(const FName& InRootPinName,
	const TArray<int32>& InPermutationIndices) const
{
#if WITH_EDITOR
	if(const FRigVMTemplate* Template = GetTemplate())
	{
		const TArray<int32>* PermutationIndicesPtr = &InPermutationIndices;
		if(PermutationIndicesPtr->IsEmpty())
		{
			PermutationIndicesPtr = &GetResolvedPermutationIndices();
		}

		const FText DisplayNameText = Template->GetDisplayNameForArgument(InRootPinName, *PermutationIndicesPtr);
		if(DisplayNameText.IsEmpty())
		{
			return InRootPinName;
		}

		const FName DisplayName = *DisplayNameText.ToString();
		if(DisplayName.IsEqual(InRootPinName))
		{
			return NAME_None;
		}
		return DisplayName;
	}
#endif
	return NAME_None;
}

void URigVMTemplateNode::InvalidateCache()
{
	SupportedTypesCache.Reset();
	CachedFunction = nullptr;
	ResolvedPermutations.Reset();

	for(URigVMPin* Pin : GetPins())
	{
		if(Pin->IsWildCard())
		{
			ResolvedFunctionName.Reset();
			break;
		}
	}
}

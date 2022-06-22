// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMCore/RigVMTemplate.h"
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
		const TArray<int32> PermutationIndices = GetFilteredPermutationsIndices();
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

				const TArray<int32> PermutationIndices = GetFilteredPermutationsIndices();
				if(PermutationIndices.Num() == GetTemplate()->NumPermutations())
				{
					if(Arg->GetTypes().Num() > 100)
					{
						Tooltip = TEXT("Supports any type.");
					}
				}
				
				if(Tooltip.IsEmpty())
				{
					FString SupportedTypesJoined;
					for (int32 Index : PermutationIndices)
					{
						FString Type = Arg->GetTypes()[Index].CPPType;
						if (!FilteredPermutations.Contains(Index))
						{
							Type += TEXT(" : Breaks Connections");
						}
						SupportedTypesJoined += Type + TEXT("\n");
					}
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

bool URigVMTemplateNode::IsSingleton() const
{
	return GetTemplate() == nullptr;
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
		if (const TPair<bool, FRigVMTemplateArgumentType>* CachedResult = SupportedTypesCache.Find(CacheKey))
		{
			if(OutCPPType)
			{
				*OutCPPType = CachedResult->Value.CPPType;
			}
			return CachedResult->Key;
		}

		FRigVMTemplateArgumentType OutType;
		if (Template->ArgumentSupportsType(RootPin->GetFName(), CPPType, &OutType))
		{
			if (OutCPPType)
			{
				(*OutCPPType) = OutType.CPPType;
			}
			return true;
		}
		return false;
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

bool URigVMTemplateNode::FilteredSupportsType(const URigVMPin* InPin, const FString& InCPPType, FString* OutCPPType, bool bAllowFloatingPointCasts)
{
	if (OutCPPType)
	{
		*OutCPPType = FString(); 
	}

	const URigVMPin* RootPin = InPin;
	bool bIsArrayElement = false;
	bool bIsStructElement = false;
	if (URigVMPin* ParentPin = InPin->GetParentPin())
	{
		RootPin = ParentPin;
		if (ParentPin->IsArray())
		{
			bIsArrayElement = true;
		}
		else if (ParentPin->IsStruct())
		{
			bIsStructElement = true;
		}
	}

	if (bIsStructElement)
	{
		return InPin->GetCPPType() == InCPPType;
	}

	const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(RootPin->GetFName());
	if (Argument == nullptr)
	{
		return false;
	}

	FString RootCPPType = InCPPType;
	if (bIsArrayElement)
	{
		RootCPPType = RigVMTypeUtils::ArrayTypeFromBaseType(InCPPType);
	}

	if (FilteredPermutations.Num() == GetTemplate()->NumPermutations())
	{
		return GetTemplate()->ArgumentSupportsType(RootPin->GetFName(), InCPPType);
	}

	const TArray<FRigVMTemplateArgumentType>& Types = Argument->GetTypes();
	for (int32 PermutationIndex : FilteredPermutations)
	{
		const FRigVMTemplateArgumentType& FilteredType = Types[PermutationIndex];
		if (FilteredType.Matches(RootCPPType, bAllowFloatingPointCasts))
		{
			return true;
		}
	}

	return false;
}

TArray<const FRigVMFunction*> URigVMTemplateNode::GetResolvedPermutations() const
{
	TArray<int32> Indices = GetFilteredPermutationsIndices();
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
			TArray<int32> PermutationIndices = GetFilteredPermutationsIndices();
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
	return GetFilteredPermutationsIndices().Num() == GetTemplate()->NumPermutations();
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
		PermutationIndices = GetFilteredPermutationsIndices();
	}

	FString DefaultValue;

	for(const int32 PermutationIndex : PermutationIndices)
	{
		FString NewDefaultValue;

		const FRigVMTemplate* Template = GetTemplate();
		const FRigVMTemplateArgument* Argument = Template->FindArgument(InRootPinName);
		const FRigVMTemplateArgumentType Type = Argument->GetTypes()[PermutationIndex];

		if(const FRigVMFunction* Permutation = Template->GetPermutation(PermutationIndex))
		{
			const TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(Permutation->Struct));
			const FRigVMStruct* DefaultStruct = (const FRigVMStruct*)StructOnScope->GetStructMemory();

			const bool bUseQuotes = Type.CPPType != RigVMTypeUtils::FStringType && Type.CPPType != RigVMTypeUtils::FNameType;
			NewDefaultValue = DefaultStruct->ExportToFullyQualifiedText(
				Cast<UScriptStruct>(StructOnScope->GetStruct()), InRootPinName, nullptr, bUseQuotes);
		}
		else
		{
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
			PermutationIndicesPtr = &GetFilteredPermutationsIndices();
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

const TArray<int32>& URigVMTemplateNode::GetFilteredPermutationsIndices() const
{
	return FilteredPermutations;
}

TArray<FRigVMTemplateArgumentType> URigVMTemplateNode::GetFilteredTypesForPin(URigVMPin* InPin) const
{
	ensureMsgf(InPin->GetNode() == this, TEXT("GetFilteredTypesForPin of %s with pin from another node %s"), *GetNodePath(), *InPin->GetPinPath(true));
	TArray<FRigVMTemplateArgumentType> FilteredTypes;

	if (FilteredPermutations.IsEmpty())
	{
		return FilteredTypes;	
	}

	if (!PreferredPermutationTypes.IsEmpty())
	{
		for (const FString& PreferredType : PreferredPermutationTypes)
		{
			FString ArgName, CPPType;
			PreferredType.Split(TEXT(":"), &ArgName, &CPPType);
			if (InPin->GetName() == ArgName)
			{
				const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(*ArgName);
				for (const FRigVMTemplateArgumentType& Type : Argument->GetTypes())
				{
					if (Type.CPPType == CPPType)
					{
						return {Type};						
					}
				}				
			}
		}
	}
	
	URigVMPin* RootPin = InPin;
	bool bIsArrayElement = false;
	if (URigVMPin* ParentPin = InPin->GetParentPin())
	{
		RootPin = ParentPin;
		bIsArrayElement = true;
	}
	if (const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(RootPin->GetFName()))
	{
		FilteredTypes = Argument->GetSupportedTypes(FilteredPermutations);
		if (bIsArrayElement)
		{
			for (FRigVMTemplateArgumentType& ArrayType : FilteredTypes)
			{
				ArrayType.ConvertToBaseElement();
			}
		}
	}
	return FilteredTypes;
}

TArray<int32> URigVMTemplateNode::GetNewFilteredPermutations(URigVMPin* InPin, URigVMPin* LinkedPin)
{	
	TArray<int32> NewFilteredPermutations;
	if (InPin->GetNode() != this)
	{
		return NewFilteredPermutations;
	}

	NewFilteredPermutations.Reserve(FilteredPermutations.Num());
	
	bool bIsArrayElement = false;
	bool bIsStructElement = false;
	URigVMPin* RootPin = InPin;
	if (URigVMPin* ParentPin = InPin->GetParentPin())
	{
		RootPin = ParentPin;
		bIsArrayElement = RootPin->IsArray();
		bIsStructElement = RootPin->IsStruct();
	}

	if (bIsStructElement)
	{
		if (RigVMTypeUtils::AreCompatible(InPin->GetCPPType(), InPin->GetCPPTypeObject(), LinkedPin->GetCPPType(), LinkedPin->GetCPPTypeObject()))
		{
			return FilteredPermutations;
		}
	}

	TArray<int32> PermutationsToTry = FilteredPermutations;

	// Reduce permutations to the ones respecting the preferred types
	TArray<int32> PreferredPermutations = FindPermutationsForTypes(PreferredPermutationTypes, true);
	PermutationsToTry = PermutationsToTry.FilterByPredicate([&](const int32& OtherPermutation) { return PreferredPermutations.Contains(OtherPermutation); });
	
	bool bLinkedIsTemplate = false;
	if (URigVMTemplateNode* LinkedTemplate = Cast<URigVMTemplateNode>(LinkedPin->GetNode()))
	{
		if (!LinkedTemplate->IsSingleton() && !LinkedPin->IsStructMember())
		{
			bLinkedIsTemplate = true;
			if (const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(RootPin->GetFName()))
			{
				for(int32 PermutationIndex : PermutationsToTry)
				{
					FRigVMTemplateArgumentType Type = Argument->GetTypes()[PermutationIndex];
					if (bIsArrayElement)
					{
						Type.ConvertToBaseElement();
					}
					if (LinkedTemplate->FilteredSupportsType(LinkedPin, Type.CPPType))
					{
						NewFilteredPermutations.Add(PermutationIndex);
					}
				}
			}
		}
	}
	
	if (!bLinkedIsTemplate)
	{
		if (const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(RootPin->GetFName()))
		{
			const FString LinkedCPPType = LinkedPin->GetCPPType();
			for(int32 PermIndex : PermutationsToTry)
			{
				FRigVMTemplateArgumentType Type = Argument->GetTypes()[PermIndex];
				if (bIsArrayElement)
				{
					Type.ConvertToBaseElement();
				}
				if (Type.Matches(LinkedCPPType))
				{
					NewFilteredPermutations.Add(PermIndex);
				}
			}
		}
	}
	return NewFilteredPermutations;
}

TArray<int32> URigVMTemplateNode::GetNewFilteredPermutations(URigVMPin* InPin, const TArray<FRigVMTemplateArgumentType>& InTypes)
{
	TArray<int32> NewFilteredPermutations;
	NewFilteredPermutations.Reserve(FilteredPermutations.Num());

	URigVMPin* RootPin = InPin;
	TArray<FRigVMTemplateArgumentType> RootTypes = InTypes;
	if (URigVMPin* ParentPin = InPin->GetParentPin())
	{
		RootPin = ParentPin;
		for (FRigVMTemplateArgumentType& Type : RootTypes)
		{
			Type.ConvertToArray();
		}
	}

	TArray<int32> PermutationsToTry = FilteredPermutations;

	// Reduce permutations to the ones respecting the preferred types
	TArray<int32> PreferredPermutations = FindPermutationsForTypes(PreferredPermutationTypes, true);
	PermutationsToTry = PermutationsToTry.FilterByPredicate([&](const int32& OtherPermutation) { return PreferredPermutations.Contains(OtherPermutation); });
	
	if (const FRigVMTemplateArgument* Argument = GetTemplate()->FindArgument(RootPin->GetFName()))
	{
		const TArray<FRigVMTemplateArgumentType>& Types = Argument->GetTypes();
		for(int32 PermIndex : PermutationsToTry)
		{
			for (FRigVMTemplateArgumentType& RootType : RootTypes)
			{				
				if (Types[PermIndex].Matches(RootType.CPPType))
				{
					NewFilteredPermutations.Add(PermIndex);
					break;
				}
			}			
		}
	}
	return NewFilteredPermutations;
}

TArray<int32> URigVMTemplateNode::FindPermutationsForTypes(const TArray<FString>& ArgumentTypes, bool bAllowCasting)
{
	TArray<int32> Permutations;
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		TArray<const FRigVMTemplateArgument*> Args;
		TArray<FString> CPPTypes;
		for (const FString& TypePairString : ArgumentTypes)
		{
			FString ArgName, Type;
			if (!TypePairString.Split(TEXT(":"), &ArgName, &Type))
			{
				return {};
			}

			if (const FRigVMTemplateArgument* Argument = Template->FindArgument(*ArgName))
			{
				Args.Add(Argument);
				CPPTypes.Add(Type);
			}
			else
			{
				return {};
			}
		}
		
		for (int32 i=0; i<Template->NumPermutations(); ++i)
		{
			bool bAllArgsMatched = true;
			for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ++ArgIndex)
			{
				const FRigVMTemplateArgument* Argument = Args[ArgIndex];
				{
					if ((bAllowCasting && !Argument->GetTypes()[i].Matches(CPPTypes[ArgIndex])) ||
						(!bAllowCasting && Argument->GetTypes()[i].CPPType != CPPTypes[ArgIndex]))
					{
						bAllArgsMatched = false;
						break;
					}
				}
			}

			if (bAllArgsMatched)
			{
				Permutations.Add(i);
			}
		}
	}
	return Permutations;
}

TArray<FString> URigVMTemplateNode::GetArgumentTypesForPermutation(const int32 InPermutationIndex)
{
	TArray<FString> ArgTypes;
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		for (int32 ArgIndex = 0; ArgIndex < Template->NumArguments(); ++ArgIndex)
		{
			const FRigVMTemplateArgument* Argument = Template->GetArgument(ArgIndex);
			if (Argument->GetTypes().Num() > InPermutationIndex)
			{
				ArgTypes.Add(Argument->GetName().ToString() + TEXT(":") + Argument->GetTypes()[InPermutationIndex].CPPType);
			}
			else
			{
				ArgTypes.Reset();
				return ArgTypes;
			}
		}
	}

	return ArgTypes;
}

bool URigVMTemplateNode::PinNeedsFilteredTypesUpdate(URigVMPin* InPin, const TArray<FRigVMTemplateArgumentType>& InTypes)
{
	TArray<int32> NewFilteredPermutations = GetNewFilteredPermutations(InPin, InTypes);
	if (NewFilteredPermutations.Num() == FilteredPermutations.Num())
	{
		return false;
	}
	return true;
}

bool URigVMTemplateNode::PinNeedsFilteredTypesUpdate(URigVMPin* InPin, URigVMPin* LinkedPin)
{
	TArray<int32> NewFilteredPermutations = GetNewFilteredPermutations(InPin, LinkedPin);

	if (NewFilteredPermutations.Num() == FilteredPermutations.Num())
	{
		return false;
	}

	return true;
}

bool URigVMTemplateNode::UpdateFilteredPermutations(URigVMPin* InPin, URigVMPin* LinkedPin)
{
	ensureMsgf(InPin->GetNode() == this, TEXT("Updating filtered permutations of %s with pin from another node %s"), *GetNodePath(), *InPin->GetPinPath(true));
	ensureMsgf(LinkedPin->GetNode() != this, TEXT("Updating filtered permutations of %s with linked pin from same node %s"), *GetNodePath(), *LinkedPin->GetPinPath(true));

	TArray<int32> NewFilteredPermutations = GetNewFilteredPermutations(InPin, LinkedPin);	
	if (NewFilteredPermutations.Num() == FilteredPermutations.Num())
	{
		return false;
	}

	if (NewFilteredPermutations.IsEmpty())
	{
		return false;
	}
	
	FilteredPermutations = NewFilteredPermutations;
	return true;
}

bool URigVMTemplateNode::UpdateFilteredPermutations(URigVMPin* InPin, const TArray<FRigVMTemplateArgumentType>& InTypes)
{
	TArray<int32> NewFilteredPermutations = GetNewFilteredPermutations(InPin, InTypes);
	if (NewFilteredPermutations.Num() == FilteredPermutations.Num())
	{
		return false;
	}

	if (NewFilteredPermutations.IsEmpty())
	{
		return false;
	}
	
	FilteredPermutations = NewFilteredPermutations;
	return true;
}

void URigVMTemplateNode::InvalidateCache()
{
	Super::InvalidateCache();
	
	SupportedTypesCache.Reset();
	CachedFunction = nullptr;
	CachedTemplate = nullptr;
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

void URigVMTemplateNode::InitializeFilteredPermutations()
{
	if (const FRigVMTemplate* Template = GetTemplate())
	{
		if (!PreferredPermutationTypes.IsEmpty())
		{
			FilteredPermutations = FindPermutationsForTypes(PreferredPermutationTypes);
		}
		else
		{
			FilteredPermutations.SetNumUninitialized(Template->NumPermutations());
			for (int32 i=0; i<FilteredPermutations.Num(); ++i)
			{
				FilteredPermutations[i] = i;
			}
		}
	}	
}

void URigVMTemplateNode::InitializeFilteredPermutationsFromTypes()
{
	if (IsSingleton())
	{
		return;
	}

	if (const FRigVMTemplate* Template = GetTemplate())
	{
		TArray<FString> ArgTypes;
		for (int32 ArgIndex = 0; ArgIndex < Template->NumArguments(); ++ArgIndex)
		{
			const FRigVMTemplateArgument* Argument = Template->GetArgument(ArgIndex);
			if (URigVMPin* Pin = FindPin(Argument->GetName().ToString()))
			{
				if (!Pin->IsWildCard())
				{
					ArgTypes.Add(Argument->GetName().ToString() + TEXT(":") + Pin->GetCPPType());
				}
			}
		}

		TArray<int32> Permutations = FindPermutationsForTypes(ArgTypes);
		if (!Permutations.IsEmpty())
		{
			FilteredPermutations = Permutations;
			PreferredPermutationTypes = ArgTypes;
		}
		else
		{
			InitializeFilteredPermutations();
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMTemplate.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMModule.h"
#include "Algo/Sort.h"

////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMTemplateArgument::FRigVMTemplateArgument()
	: Index(INDEX_NONE)
	, Name(NAME_None)
	, Direction(ERigVMPinDirection::IO)
	, bSingleton(true)
{
}

FRigVMTemplateArgument::FRigVMTemplateArgument(FProperty* InProperty)
	: Index(INDEX_NONE)
	, Name(InProperty->GetFName())
	, Direction(ERigVMPinDirection::IO)
	, bSingleton(true)
{
#if WITH_EDITOR
	Direction = FRigVMStruct::GetPinDirectionFromProperty(InProperty);
#endif

	FString ExtendedType;
	const FString CPPType = InProperty->GetCPPType(&ExtendedType);
	FType Type(CPPType + ExtendedType);

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		InProperty = ArrayProperty->Inner;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
	{
		Type.CPPTypeObject = StructProperty->Struct;
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
	{
		Type.CPPTypeObject = EnumProperty->GetEnum();
	}

	Types.Add(Type);
}

bool FRigVMTemplateArgument::SupportsType(const FString& InCPPType, const TArray<int32>& InPermutationIndices, FType* OutType) const
{
	bool bFoundMatch = false;
	bool bFoundPerfectMatch = false;
	
	auto VisitType = [&bFoundMatch, &bFoundPerfectMatch, InCPPType, &OutType](const FType& InType)
	{
		if (InType.Matches(InCPPType))
		{
			if(!bFoundPerfectMatch)
			{
				if(OutType)
				{
					*OutType = InType;
				}
				if(InType.CPPType == InCPPType)
				{
					bFoundPerfectMatch = true;
				}
			}
			bFoundMatch = true;
		}
	};
	
	if(InPermutationIndices.IsEmpty())
	{
		for (const FType& Type : Types)
		{
			VisitType(Type);
		}
	}
	else
	{
		for (const int32 PermutationIndex : InPermutationIndices)
		{
			VisitType(Types[PermutationIndex]);
		}
	}
	return bFoundMatch;
}

bool FRigVMTemplateArgument::IsSingleton(const TArray<int32>& InPermutationIndices) const
{
	if (bSingleton)
	{
		return true;
	}
	else if(InPermutationIndices.Num() == 0)
	{
		return false;
	}

	const FType TypeToCheck = Types[InPermutationIndices[0]];
	for (int32 PermutationIndex = 1; PermutationIndex < InPermutationIndices.Num(); PermutationIndex++)
	{
		if (Types[InPermutationIndices[PermutationIndex]] != TypeToCheck)
		{
			return false;
		}
	}
	return true;
}

bool FRigVMTemplateArgument::IsArray() const
{
	if(Types.Num() > 0)
	{
		return Types[0].IsArray();
	}
	return false;
}

const TArray<FRigVMTemplateArgument::FType>& FRigVMTemplateArgument::GetTypes() const
{
	return Types;
}

TArray<FRigVMTemplateArgument::FType> FRigVMTemplateArgument::GetSupportedTypes(const TArray<int32>& InPermutationIndices) const
{
	TArray<FType> SupportedTypes;
	if(InPermutationIndices.IsEmpty())
	{
		for (const FType& Type : Types)
		{
			SupportedTypes.AddUnique(Type);
		}
	}
	else
	{
		for(const int32 PermutationIndex : InPermutationIndices)
		{
			SupportedTypes.AddUnique(Types[PermutationIndex]);
		}
	}
	return SupportedTypes;
}

TArray<FString> FRigVMTemplateArgument::GetSupportedTypeStrings(const TArray<int32>& InPermutationIndices) const
{
	TArray<FString> SupportedTypes;
	if(InPermutationIndices.IsEmpty())
	{
		for (const FType& Type : Types)
		{
			SupportedTypes.AddUnique(Type.CPPType);
		}
	}
	else
	{
		for(const int32 PermutationIndex : InPermutationIndices)
		{
			SupportedTypes.AddUnique(Types[PermutationIndex].CPPType);
		}
	}
	return SupportedTypes;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FRigVMTemplate::FRigVMTemplate()
	: Index(INDEX_NONE)
	, Notation(NAME_None)
{

}

FRigVMTemplate::FRigVMTemplate(UScriptStruct* InStruct, const FString& InTemplateName, int32 InFunctionIndex)
	: Index(INDEX_NONE)
	, Notation(NAME_None)
{
	TArray<FString> ArgumentNotations;
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FRigVMTemplateArgument Argument(*It);
		Argument.Index = Arguments.Num();

		if(IsValidArgumentForTemplate(Argument))
		{
			Arguments.Add(Argument);
			ArgumentNotations.Add(GetArgumentNotation(Argument));
		}
	}

	if (ArgumentNotations.Num() > 0)
	{
		FString NotationStr = FString::Printf(TEXT("%s(%s)"), *InTemplateName, *FString::Join(ArgumentNotations, TEXT(",")));
		Notation = *NotationStr;
		Permutations.Add(InFunctionIndex);
	}
}

bool FRigVMTemplate::IsValidArgumentForTemplate(const FRigVMTemplateArgument& InArgument)
{
	static const TArray<ERigVMPinDirection> ValidDirections = {
		ERigVMPinDirection::Input,
		ERigVMPinDirection::Output,
		ERigVMPinDirection::IO,
		ERigVMPinDirection::Visible
	};

	if(!ValidDirections.Contains(InArgument.Direction))
	{
		return false;
	}
	return true;
}


const FString& FRigVMTemplate::GetArgumentNotationPrefix(const FRigVMTemplateArgument& InArgument)
{
	static const FString EmptyPrefix = FString();
	static const FString InPrefix = TEXT("in ");
	static const FString OutPrefix = TEXT("out ");
	static const FString IOPrefix = TEXT("io ");

	switch(InArgument.Direction)
	{
		case ERigVMPinDirection::Input:
		case ERigVMPinDirection::Visible:
		{
			return InPrefix;
		}
		case ERigVMPinDirection::Output:
		{
			return OutPrefix;
		}
		case ERigVMPinDirection::IO:
		{
			return IOPrefix;
		}
		default:
		{
			break;
		}
	}

	return EmptyPrefix;
}

const FString& FRigVMTemplate::GetArgumentNotationSuffix(const FRigVMTemplateArgument& InArgument)
{
	static const FString EmptySuffix = FString();
	static const FString ArraySuffix = TEXT("[]");
	return InArgument.IsArray() ? ArraySuffix : EmptySuffix;
}

FString FRigVMTemplate::GetArgumentNotation(const FRigVMTemplateArgument& InArgument)
{
	return FString::Printf(TEXT("%s%s%s"),
		*GetArgumentNotationPrefix(InArgument),
		*InArgument.GetName().ToString(),
		*GetArgumentNotationSuffix(InArgument));
}

bool FRigVMTemplate::IsValid() const
{
	return !Notation.IsNone();
}

const FName& FRigVMTemplate::GetNotation() const
{
	return Notation;
}

FName FRigVMTemplate::GetName() const
{
	FString Left;
	if (GetNotation().ToString().Split(TEXT("::"), &Left, nullptr))
	{
		return *Left;
	}
	return NAME_None;
}

#if WITH_EDITOR

FLinearColor FRigVMTemplate::GetColor(const TArray<int32>& InPermutationIndices) const
{
	bool bFirstColorFound = false;
	FLinearColor ResolvedColor = FLinearColor::White;

	auto GetColorFromMetadata = [] (FString InMetadata) -> FLinearColor
	{
		FLinearColor Color = FLinearColor::Black;

		FString Metadata = InMetadata;
		Metadata.TrimStartAndEndInline();
		FString SplitString(TEXT(" "));
		FString Red, Green, Blue, GreenAndBlue;
		if (Metadata.Split(SplitString, &Red, &GreenAndBlue))
		{
			Red.TrimEndInline();
			GreenAndBlue.TrimStartInline();
			if (GreenAndBlue.Split(SplitString, &Green, &Blue))
			{
				Green.TrimEndInline();
				Blue.TrimStartInline();

				float RedValue = FCString::Atof(*Red);
				float GreenValue = FCString::Atof(*Green);
				float BlueValue = FCString::Atof(*Blue);
				Color = FLinearColor(RedValue, GreenValue, BlueValue);
			}
		}

		return Color;
	};

	auto VisitPermutation = [&bFirstColorFound, &ResolvedColor, GetColorFromMetadata, this](int32 InPermutationIndex) -> bool
	{
		static const FName NodeColorName = TEXT("NodeColor");
		FString NodeColorMetadata;

		const FRigVMFunction* ResolvedFunction = GetPermutation(InPermutationIndex);
		ResolvedFunction->Struct->GetStringMetaDataHierarchical(NodeColorName, &NodeColorMetadata);
		if (!NodeColorMetadata.IsEmpty())
		{
			if(bFirstColorFound)
			{
				const FLinearColor NodeColor = GetColorFromMetadata(NodeColorMetadata);
				if(!ResolvedColor.Equals(NodeColor, 0.01f))
				{
					ResolvedColor = FLinearColor::White;
					return false;
				}
			}
			else
			{
				ResolvedColor = GetColorFromMetadata(NodeColorMetadata);
				bFirstColorFound = true;
			}
		}
		return true;
	};

	if(InPermutationIndices.IsEmpty())
	{
		for(int32 PermutationIndex = 0; PermutationIndex < Permutations.Num(); PermutationIndex++)
		{
			if(!VisitPermutation(PermutationIndex))
			{
				break;
			}
		}
	}
	else
	{
		for(const int32 PermutationIndex : InPermutationIndices)
		{
			if(!VisitPermutation(PermutationIndex))
			{
				break;
			}
		}
	}
	return ResolvedColor;
}

FText FRigVMTemplate::GetTooltipText(const TArray<int32>& InPermutationIndices) const
{
	FText ResolvedTooltipText;

	auto VisitPermutation = [&ResolvedTooltipText, this](int32 InPermutationIndex) -> bool
	{
		const FRigVMFunction* ResolvedFunction = GetPermutation(InPermutationIndex);
		const FText TooltipText = ResolvedFunction->Struct->GetToolTipText();
		
		if (!ResolvedTooltipText.IsEmpty())
		{
			if(!ResolvedTooltipText.EqualTo(TooltipText))
			{
				ResolvedTooltipText = FText::FromName(GetName());
				return false;
			}
		}
		else
		{
			ResolvedTooltipText = TooltipText;
		}
		return true;
	};

	if(InPermutationIndices.IsEmpty())
	{
		for(int32 PermutationIndex = 0; PermutationIndex < Permutations.Num(); PermutationIndex++)
		{
			if(!VisitPermutation(PermutationIndex))
			{
				break;
			}
		}
	}
	else
	{
		for(const int32 PermutationIndex : InPermutationIndices)
		{
			if(!VisitPermutation(PermutationIndex))
			{
				break;
			}
		}
	}

	return ResolvedTooltipText;
}

#endif

bool FRigVMTemplate::IsCompatible(const FRigVMTemplate& InOther) const
{
	if (!IsValid() || !InOther.IsValid())
	{
		return false;
	}

	return Notation == InOther.Notation;
}

bool FRigVMTemplate::Merge(const FRigVMTemplate& InOther)
{
	if (!IsCompatible(InOther))
	{
		return false;
	}

	if (InOther.Permutations.Num() != 1)
	{
		return false;
	}

	// find colliding permutations
	for(int32 PermutationIndex = 0;PermutationIndex<NumPermutations();PermutationIndex++)
	{
		int32 MatchingArguments = 0;
		for(int32 ArgumentIndex = 0; ArgumentIndex < Arguments.Num(); ArgumentIndex++)
		{
			if(Arguments[ArgumentIndex].GetTypes()[PermutationIndex] ==
				InOther.Arguments[ArgumentIndex].Types[0])
			{
				MatchingArguments++;
			}
		}
		if(MatchingArguments == Arguments.Num())
		{
			// find the previously defined permutation.
			UE_LOG(LogRigVM, Display, TEXT("RigVMFunction '%s' cannot be merged into the '%s' template. It collides with '%s'."),
				InOther.GetPermutation(0)->Name,
				*GetNotation().ToString(),
				GetPermutation(PermutationIndex)->Name);
			return false;
		}
	}

	TArray<FRigVMTemplateArgument> NewArgs;

	for (int32 ArgumentIndex = 0; ArgumentIndex < Arguments.Num(); ArgumentIndex++)
	{
		if (InOther.Arguments[ArgumentIndex].Types.Num() != 1)
		{
			return false;
		}

		NewArgs.Add(Arguments[ArgumentIndex]);
		NewArgs[ArgumentIndex].Types.Add(InOther.Arguments[ArgumentIndex].Types[0]);

		if (!Arguments[ArgumentIndex].Types.Contains(InOther.Arguments[ArgumentIndex].Types[0]))
		{
			NewArgs[ArgumentIndex].bSingleton = false;
		}
	}

	Arguments = NewArgs;

	Permutations.Add(InOther.Permutations[0]);
	return true;
}

const FRigVMTemplateArgument* FRigVMTemplate::FindArgument(const FName& InArgumentName) const
{
	return Arguments.FindByPredicate([InArgumentName](const FRigVMTemplateArgument& Argument) -> bool
	{
		return Argument.GetName() == InArgumentName;
	});
}

bool FRigVMTemplate::ArgumentSupportsType(const FName& InArgumentName, const FString& InCPPType, const FRigVMTemplate::FTypeMap& InTypes, FRigVMTemplateArgument::FType* OutType) const
{
	if (const FRigVMTemplateArgument* Argument = FindArgument(InArgumentName))
	{
		if(InTypes.Num() == 0)
		{
			return Argument->SupportsType(InCPPType, TArray<int32>(), OutType);
		}

		FTypeMap Types = InTypes;
		Types.FindOrAdd(InArgumentName) = InCPPType;

		TArray<int32> PermutationIndices;
		if(Resolve(Types, PermutationIndices, true))
		{
			if(OutType)
			{
				*OutType = Types.FindChecked(InArgumentName);
			}
			return true;
		}
		return false;
	}
	return false;
}

const FRigVMFunction* FRigVMTemplate::GetPermutation(int32 InIndex) const
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
	return &Registry.GetFunctions()[Permutations[InIndex]];
}

bool FRigVMTemplate::ContainsPermutation(const FRigVMFunction* InPermutation) const
{
	return FindPermutation(InPermutation) != INDEX_NONE;
}

int32 FRigVMTemplate::FindPermutation(const FRigVMFunction* InPermutation) const
{
	check(InPermutation);
	return Permutations.Find(InPermutation->Index);
}

bool FRigVMTemplate::FullyResolve(FRigVMTemplate::FTypeMap& InOutTypes, int32& OutPermutationIndex) const
{
	TArray<int32> PermutationIndices;
	Resolve(InOutTypes, PermutationIndices, false);
	if(PermutationIndices.Num() == 1)
	{
		OutPermutationIndex = PermutationIndices[0];
	}
	else
	{
		OutPermutationIndex = INDEX_NONE;
	}
	return OutPermutationIndex != INDEX_NONE;
}

bool FRigVMTemplate::Resolve(FTypeMap& InOutTypes, TArray<int32>& OutPermutationIndices, bool bAllowFloatingPointCasts) const
{
	FTypeMap InputTypes = InOutTypes;
	InOutTypes.Reset();

	OutPermutationIndices.Reset();
	for (int32 PermutationIndex = 0; PermutationIndex < Permutations.Num(); PermutationIndex++)
	{
		OutPermutationIndices.Add(PermutationIndex);
	}
	
	for (const FRigVMTemplateArgument& Argument : Arguments)
	{
		if (Argument.bSingleton)
		{
			InOutTypes.Add(Argument.Name, Argument.Types[0]);
			continue;
		}
		else if (const FRigVMTemplateArgument::FType* InputType = InputTypes.Find(Argument.Name))
		{
			FRigVMTemplateArgument::FType MatchedType = *InputType;
			bool bFoundMatch = false;
			bool bFoundPerfectMatch = false;
			for (int32 PermutationIndex = 0; PermutationIndex < Argument.Types.Num(); PermutationIndex++)
			{
				if (!Argument.Types[PermutationIndex].Matches(InputType->CPPType, bAllowFloatingPointCasts))
				{
					OutPermutationIndices.Remove(PermutationIndex);
				}
				else
				{
					bFoundMatch = true;

					// if the type matches - but it's not the exact same
					if(!bFoundPerfectMatch)
					{
						MatchedType = Argument.Types[PermutationIndex];

						// if we found the perfect match - let's stop here
						if(Argument.Types[PermutationIndex] == *InputType)
						{
							bFoundPerfectMatch = true;
						}
					}
				}
			}
			
			if(bFoundMatch)
			{
				InOutTypes.Add(Argument.Name,MatchedType);
				continue;
			}
		}
		
		if(Argument.IsArray())
		{
			InOutTypes.Add(Argument.Name, FRigVMTemplateArgument::FType::Array());
		}
		else
		{
			InOutTypes.Add(Argument.Name, FRigVMTemplateArgument::FType());
		}
	}

	if (OutPermutationIndices.Num() == 1)
	{
		InOutTypes.Reset();
		for (const FRigVMTemplateArgument& Argument : Arguments)
		{
			InOutTypes.Add(Argument.Name, Argument.Types[OutPermutationIndices[0]]);
		}
	}
	else if (OutPermutationIndices.Num() > 1)
	{
		for (const FRigVMTemplateArgument& Argument : Arguments)
		{
			if (Argument.IsSingleton(OutPermutationIndices))
			{
				InOutTypes.FindChecked(Argument.Name) = Argument.Types[OutPermutationIndices[0]];
			}
		}
	}

	return !OutPermutationIndices.IsEmpty();
}

#if WITH_EDITOR

FString FRigVMTemplate::GetCategory() const
{
	FString Category;
	GetPermutation(0)->Struct->GetStringMetaDataHierarchical(FRigVMStruct::CategoryMetaName, &Category);

	if (Category.IsEmpty())
	{
		return Category;
	}

	for (int32 PermutationIndex = 1; PermutationIndex < NumPermutations(); PermutationIndex++)
	{
		FString OtherCategory;
		if (GetPermutation(PermutationIndex)->Struct->GetStringMetaDataHierarchical(FRigVMStruct::CategoryMetaName, &OtherCategory))
		{
			while (!OtherCategory.StartsWith(Category, ESearchCase::IgnoreCase))
			{
				FString Left;
				if (Category.Split(TEXT("|"), &Left, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					Category = Left;
				}
				else
				{
					return FString();
				}

			}
		}
	}

	return Category;
}

FString FRigVMTemplate::GetKeywords() const
{
	TArray<FString> KeywordsMetadata;
	KeywordsMetadata.Add(GetName().ToString());

	for (int32 PermutationIndex = 0; PermutationIndex < NumPermutations(); PermutationIndex++)
	{
		if (const FRigVMFunction* Function = GetPermutation(PermutationIndex))
		{
			KeywordsMetadata.Add(Function->Struct->GetDisplayNameText().ToString());
			
			FString FunctionKeyWordsMetadata;
			Function->Struct->GetStringMetaDataHierarchical(FRigVMStruct::KeywordsMetaName, &FunctionKeyWordsMetadata);
			if (!FunctionKeyWordsMetadata.IsEmpty())
			{
				KeywordsMetadata.Add(FunctionKeyWordsMetadata);
			}
		}
	}

	return FString::Join(KeywordsMetadata, TEXT(","));
}

#endif
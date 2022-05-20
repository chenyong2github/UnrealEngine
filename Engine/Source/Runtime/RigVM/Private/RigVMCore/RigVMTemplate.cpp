// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMTemplate.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMModule.h"
#include "Algo/Sort.h"
#include "UObject/UObjectIterator.h"

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
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
	{
		Type.CPPTypeObject = ByteProperty->Enum;
	}
	Type.CPPType = RigVMTypeUtils::PostProcessCPPType(Type.CPPType, Type.CPPTypeObject);

	Types.Add(Type);
	TypeToPermutations.Add(Type.CPPType, {0});
}

FRigVMTemplateArgument::FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, const FType& InType)
: Index(INDEX_NONE)
, Name(InName)
, Direction(InDirection)
, bSingleton(true)
, Types({InType})
{
	TypeToPermutations.Add(InType.CPPType, {0});	
}

FRigVMTemplateArgument::FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, const TArray<FType>& InTypes)
: Index(INDEX_NONE)
, Name(InName)
, Direction(InDirection)
, bSingleton(true)
, Types(InTypes)
{
	check(Types.Num() > 0);

	for(int32 TypeIndex=1;TypeIndex<Types.Num();TypeIndex++)
	{
		if(Types[TypeIndex] != Types[0])
		{
			bSingleton = false;
			break;
		}
	}

	for(int32 TypeIndex=0;TypeIndex<Types.Num();TypeIndex++)
	{
		if (TArray<int32>* Permutations = TypeToPermutations.Find(Types[TypeIndex].CPPType))
		{
			Permutations->Add(TypeIndex);
		}
		else
		{
			TypeToPermutations.Add(Types[TypeIndex].CPPType, {TypeIndex});
		}
	}
}

bool FRigVMTemplateArgument::SupportsType(const FString& InCPPType, FType* OutType) const
{
	if (const TArray<int32>* Permutations = TypeToPermutations.Find(InCPPType))
	{
		if(OutType)
		{
			(*OutType) = Types[(*Permutations)[0]];
		}
		return true;		
	}

	// Try to find compatible type
	TArray<FString> CompatibleTypes = FType::GetCompatibleTypes(InCPPType);
	for (const FString& Compatible : CompatibleTypes)
	{
		if (const TArray<int32>* Permutations = TypeToPermutations.Find(Compatible))
		{
			if(OutType)
			{
				(*OutType) = Types[(*Permutations)[0]];
			}
			return true;		
		}
	}

	return false;	
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

FRigVMTemplateArgument::EArrayType FRigVMTemplateArgument::GetArrayType() const
{
	if(Types.Num() > 0)
	{
		const EArrayType ArrayType = Types[0].IsArray() ? EArrayType::EArrayType_ArrayValue : EArrayType::EArrayType_SingleValue;
		
		if(IsSingleton())
		{
			return ArrayType;
		}

		for(int32 PermutationIndex=1;PermutationIndex<Types.Num();PermutationIndex++)
		{
			const EArrayType OtherArrayType = Types[PermutationIndex].IsArray() ? EArrayType::EArrayType_ArrayValue : EArrayType::EArrayType_SingleValue;
			if(OtherArrayType != ArrayType)
			{
				return EArrayType::EArrayType_Mixed;
			}
		}

		return ArrayType;
	}

	return EArrayType_Invalid;
}

const TArray<FRigVMTemplateArgument::FType> FRigVMTemplateArgument::GetCompatibleTypes(ETypeCategory InCategory)
{
	check(InCategory != ETypeCategory_Invalid);
	
	static TMap<ETypeCategory, TArray<FType>> CompatibleTypes;
	if(CompatibleTypes.IsEmpty())
	{
		CompatibleTypes.Add(ETypeCategory_SingleAnyValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArrayAnyValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_SingleSimpleValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArraySimpleValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_SingleMathStructValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArrayMathStructValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_SingleScriptStructValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArrayScriptStructValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_SingleEnumValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArrayEnumValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_SingleObjectValue, TArray<FType>());
		CompatibleTypes.Add(ETypeCategory_ArrayObjectValue, TArray<FType>());
		
		TArray<FType>& SingleAnyValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleAnyValue);
		TArray<FType>& ArrayAnyValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArrayAnyValue);
		TArray<FType>& SingleSimpleValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleSimpleValue);
		TArray<FType>& ArraySimpleValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArraySimpleValue);
		TArray<FType>& SingleMathStructValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleMathStructValue);
		TArray<FType>& ArrayMathStructValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArrayMathStructValue);
		TArray<FType>& SingleScriptStructValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleScriptStructValue);
		TArray<FType>& ArrayScriptStructValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArrayScriptStructValue);
		TArray<FType>& SingleEnumValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleEnumValue);
		TArray<FType>& ArrayEnumValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArrayEnumValue);
		TArray<FType>& SingleObjectValueTypes = CompatibleTypes.FindChecked(ETypeCategory_SingleObjectValue);
		TArray<FType>& ArrayObjectValueTypes = CompatibleTypes.FindChecked(ETypeCategory_ArrayObjectValue);
		
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::BoolType));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::Int32Type));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::UInt8Type));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::FloatType));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::DoubleType));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::FNameType));
		SingleSimpleValueTypes.Add(FType(RigVMTypeUtils::FStringType));

		for(const FType& Type : SingleSimpleValueTypes)
		{
			ArraySimpleValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(Type.CPPType)));
		}

		SingleAnyValueTypes = SingleSimpleValueTypes;
		ArrayAnyValueTypes = ArraySimpleValueTypes;

		static const TArray<UScriptStruct*> MathTypes = { 
			TBaseStructure<FRotator>::Get(),
			TBaseStructure<FQuat>::Get(),
			TBaseStructure<FTransform>::Get(),
			TBaseStructure<FLinearColor>::Get(),
			TBaseStructure<FColor>::Get(),
			TBaseStructure<FPlane>::Get(),
			TBaseStructure<FVector>::Get(),
			TBaseStructure<FVector2D>::Get(),
			TBaseStructure<FVector4>::Get(),
			TBaseStructure<FBox2D>::Get()
		};

		for(UScriptStruct* MathType : MathTypes)
		{
			SingleMathStructValueTypes.Add(FType(MathType->GetStructCPPName(), MathType));
			ArrayMathStructValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(MathType->GetStructCPPName()), MathType));
		}

		struct FTypeTraverser
		{
			static EObjectFlags DisallowedFlags()
			{
				return RF_BeginDestroyed | RF_FinishDestroyed;
			}

			static EObjectFlags NeededFlags()
			{
				return RF_Public;
			}
			
			static bool IsAllowedType(const FProperty* InProperty, bool bCheckFlags = true)
			{
				if(bCheckFlags)
				{
					if(!InProperty->HasAnyPropertyFlags(
						CPF_BlueprintVisible |
						CPF_BlueprintReadOnly |
						CPF_Edit))
					{
						return false;
					}
				}

				if(InProperty->IsA<FBoolProperty>() ||
					InProperty->IsA<FUInt32Property>() ||
					InProperty->IsA<FInt8Property>() ||
					InProperty->IsA<FInt16Property>() ||
					InProperty->IsA<FIntProperty>() ||
					InProperty->IsA<FInt64Property>() ||
					InProperty->IsA<FFloatProperty>() ||
					InProperty->IsA<FDoubleProperty>() ||
					InProperty->IsA<FNumericProperty>() ||
					InProperty->IsA<FNameProperty>() ||
					InProperty->IsA<FStrProperty>())
				{
					return true;
				}

				if(const FArrayProperty* ArrayProperty  = CastField<FArrayProperty>(InProperty))
				{
					return IsAllowedType(ArrayProperty->Inner, false);
				}
				if(const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
				{
					return IsAllowedType(StructProperty->Struct);
				}
				if(const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InProperty))
				{
					return IsAllowedType(ObjectProperty->PropertyClass);
				}
				if(const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
				{
					return IsAllowedType(EnumProperty->GetEnum());
				}
				if(const FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
				{
					if(const UEnum* Enum = ByteProperty->Enum)
					{
						return IsAllowedType(Enum);
					}
					return true;
				}
				return false;
			}

			static bool IsAllowedType(const UEnum* InEnum)
			{
				return !InEnum->HasAnyFlags(DisallowedFlags()) && InEnum->HasAllFlags(NeededFlags());
			}

			static bool IsAllowedType(const UStruct* InStruct)
			{
				if(InStruct->HasAnyFlags(DisallowedFlags()) || !InStruct->HasAllFlags(NeededFlags()))
				{
					return false;
				}
				if(InStruct->IsChildOf(FRigVMStruct::StaticStruct()))
				{
					return false;
				}
				if(InStruct->IsChildOf(FRigVMUnknownType::StaticStruct()))
				{
					return false;
				}
				for (TFieldIterator<FProperty> It(InStruct); It; ++It)
				{
					if(!IsAllowedType(*It))
					{
						return false;
					}
				}
				return true;
			}

			static bool IsAllowedType(const UClass* InClass)
			{
				if(InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_Abstract))
				{
					return false;
				}

				// note: currently we don't allow UObjects
				return false;
				//return IsAllowedType(Cast<UStruct>(InClass));
			}
		};

		// add all structs
		for (TObjectIterator<UScriptStruct> ScriptIt; ScriptIt; ++ScriptIt)
		{
			UScriptStruct* ScriptStruct = *ScriptIt;
			if(!FTypeTraverser::IsAllowedType(ScriptStruct))
			{
				continue;
			}

			const FString CPPType = ScriptStruct->GetStructCPPName();

			if(!ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				SingleAnyValueTypes.Add(FType(CPPType, ScriptStruct));
				ArrayAnyValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), ScriptStruct));
				SingleScriptStructValueTypes.Add(FType(CPPType, ScriptStruct));
				ArrayScriptStructValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), ScriptStruct));
			}
		}

		// add all enums
		for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
		{
			UEnum* Enum = (*EnumIt);
			if(!FTypeTraverser::IsAllowedType(Enum))
			{
				continue;
			}
			const FString CPPType = Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType;
			SingleAnyValueTypes.Add(FType(CPPType, Enum));
			ArrayAnyValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Enum));
			SingleEnumValueTypes.Add(FType(CPPType, Enum));
			ArrayEnumValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Enum));
		}

		// add all classes
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;

			// for now this always returns false
			if(!FTypeTraverser::IsAllowedType(Class))
			{
				continue;
			}

			const FString CPPType = Class->GetPrefixCPP() + Class->GetName();
			SingleAnyValueTypes.Add(FType(CPPType, Class));
			ArrayAnyValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Class));
			SingleObjectValueTypes.Add(FType(CPPType, Class));
			ArrayObjectValueTypes.Add(FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Class));
		}

		// check that matching categories have the same size
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleAnyValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArrayAnyValue).Num());
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleSimpleValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArraySimpleValue).Num());
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleMathStructValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArrayMathStructValue).Num());
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleScriptStructValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArrayScriptStructValue).Num());
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleEnumValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArrayEnumValue).Num());
		check(CompatibleTypes.FindChecked(ETypeCategory_SingleObjectValue).Num() == CompatibleTypes.FindChecked(ETypeCategory_ArrayObjectValue).Num());
	}

	return CompatibleTypes.FindChecked(InCategory);
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
		const FString NotationStr = FString::Printf(TEXT("%s(%s)"), *InTemplateName, *FString::Join(ArgumentNotations, TEXT(",")));
		Notation = *NotationStr;
		Permutations.Add(InFunctionIndex);
	}
}

FRigVMTemplate::FRigVMTemplate(const FName& InTemplateName, const TArray<FRigVMTemplateArgument>& InArguments, int32 InFunctionIndex)
	: Index(INDEX_NONE)
	, Notation(NAME_None)
{
	TArray<FString> ArgumentNotations;
	for (const FRigVMTemplateArgument& InArgument : InArguments)
	{
		FRigVMTemplateArgument Argument = InArgument;
		Argument.Index = Arguments.Num();

		if(IsValidArgumentForTemplate(Argument))
		{
			Arguments.Add(Argument);
			ArgumentNotations.Add(GetArgumentNotation(Argument));
		}
	}

	if (ArgumentNotations.Num() > 0)
	{
		const FString NotationStr = FString::Printf(TEXT("%s(%s)"), *InTemplateName.ToString(), *FString::Join(ArgumentNotations, TEXT(",")));
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

FString FRigVMTemplate::GetArgumentNotation(const FRigVMTemplateArgument& InArgument)
{
	return FString::Printf(TEXT("%s%s"),
		*GetArgumentNotationPrefix(InArgument),
		*InArgument.GetName().ToString());
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
	if (GetNotation().ToString().Split(TEXT("("), &Left, nullptr))
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
		if(ResolvedFunction == nullptr)
		{
			return false;
		}

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
		if(ResolvedFunction == nullptr)
		{
			return false;
		}
		
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

FText FRigVMTemplate::GetDisplayNameForArgument(const FName& InArgumentName, const TArray<int32>& InPermutationIndices) const
{
	if(const FRigVMTemplateArgument* Argument = FindArgument(InArgumentName))
	{
		FText ResolvedDisplayName;

		auto VisitPermutation = [InArgumentName, &ResolvedDisplayName, this](int32 InPermutationIndex) -> bool
		{
			const FRigVMFunction* ResolvedFunction = GetPermutation(InPermutationIndex);
			if(ResolvedFunction == nullptr)
			{
				return false;
			}
			
			if(const FProperty* Property = ResolvedFunction->Struct->FindPropertyByName(InArgumentName))
			{
				const FText DisplayName = Property->GetDisplayNameText();
				if (!ResolvedDisplayName.IsEmpty())
				{
					if(!ResolvedDisplayName.EqualTo(DisplayName))
					{
						ResolvedDisplayName = FText::FromName(InArgumentName);
						return false;
					}
				}
				else
				{
					ResolvedDisplayName = DisplayName;
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

		return ResolvedDisplayName;
	}
	return FText();
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

		// Add Other argument information into the TypeToPermutations map
		{
			const FString& OtherCPPType = InOther.Arguments[ArgumentIndex].Types[0].CPPType;
			const int32 NewPermutationIndex = NewArgs[ArgumentIndex].Types.Num();
			if (TArray<int32>* ArgTypePermutations = NewArgs[ArgumentIndex].TypeToPermutations.Find(OtherCPPType))
			{
				ArgTypePermutations->Add(NewPermutationIndex);
			}
			else
			{
				NewArgs[ArgumentIndex].TypeToPermutations.Add(OtherCPPType, {NewPermutationIndex});
			}
		}
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

bool FRigVMTemplate::ArgumentSupportsType(const FName& InArgumentName, const FString& InCPPType, FRigVMTemplateArgument::FType* OutType) const
{
	if (const FRigVMTemplateArgument* Argument = FindArgument(InArgumentName))
	{
		return Argument->SupportsType(InCPPType, OutType);		
		
	}
	return false;
}

const FRigVMFunction* FRigVMTemplate::GetPermutation(int32 InIndex) const
{
	const FRigVMRegistry& Registry = FRigVMRegistry::Get();
	const int32 FunctionIndex = Permutations[InIndex];
	if(Registry.GetFunctions().IsValidIndex(FunctionIndex))
	{
		return &Registry.GetFunctions()[Permutations[InIndex]];
	}
	return nullptr;
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

				// if we found a perfect match - remove all permutations which don't match this one
				if(bFoundPerfectMatch)
				{
					const TArray<int32> BeforePermutationIndices = OutPermutationIndices;
					OutPermutationIndices.RemoveAll([Argument, MatchedType](int32 PermutationIndex) -> bool
					{
						return Argument.Types[PermutationIndex] != MatchedType;
					});
					if (OutPermutationIndices.IsEmpty() && bAllowFloatingPointCasts)
					{
						OutPermutationIndices = BeforePermutationIndices;
						OutPermutationIndices.RemoveAll([Argument, MatchedType, InputType](int32 PermutationIndex) -> bool
						{
							return !Argument.Types[PermutationIndex].Matches(InputType->CPPType, true);
						});
					}
				}
				continue;
			}
		}

		const FRigVMTemplateArgument::EArrayType ArrayType = Argument.GetArrayType();
		if(ArrayType == FRigVMTemplateArgument::EArrayType_Mixed)
		{
			InOutTypes.Add(Argument.Name, FRigVMTemplateArgument::FType());

			if(const FRigVMTemplateArgument::FType* InputType = InputTypes.Find(Argument.Name))
			{
				if(InputType->IsArray())
				{
					InOutTypes.FindChecked(Argument.Name) = FRigVMTemplateArgument::FType::Array();
				}
			}
		}
		else if(ArrayType == FRigVMTemplateArgument::EArrayType_ArrayValue)
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

bool FRigVMTemplate::ResolveArgument(const FName& InArgumentName, const FRigVMTemplateArgument::FType& InType,
	FTypeMap& InOutTypes) const
{
	auto RemoveWildCardTypes = [](const FTypeMap& InTypes)
	{
		FTypeMap FilteredTypes;
		for(const FTypePair& Pair: InTypes)
		{
			if(!Pair.Value.IsWildCard())
			{
				FilteredTypes.Add(Pair);
			}
		}
		return FilteredTypes;
	};

	// remove all wildcards from map
	InOutTypes = RemoveWildCardTypes(InOutTypes);

	// first resolve with no types given except for the new argument type
	FTypeMap ResolvedTypes;
	ResolvedTypes.Add(InArgumentName, InType);
	TArray<int32> PermutationIndices;
	FTypeMap RemainingTypesToResolve;
	
	if(Resolve(ResolvedTypes, PermutationIndices, true))
	{
		// let's see if the input argument resolved into the expected type
		const FRigVMTemplateArgument::FType ResolvedInputType = ResolvedTypes.FindChecked(InArgumentName);
		if(!ResolvedInputType.Matches(InType.CPPType, true))
		{
			return false;
		}
		
		ResolvedTypes = RemoveWildCardTypes(ResolvedTypes);
		
		// remove all argument types from the reference list
		// provided from the outside. we cannot resolve these further
		auto RemoveResolvedTypesFromRemainingList = [](
			FTypeMap& InOutTypes, const FTypeMap& InResolvedTypes, FTypeMap& InOutRemainingTypesToResolve)
		{
			InOutRemainingTypesToResolve = InOutTypes;
			for(const FTypePair& Pair: InOutTypes)
			{
				if(InResolvedTypes.Contains(Pair.Key))
				{
					InOutRemainingTypesToResolve.Remove(Pair.Key);
				}
			}
			InOutTypes = InResolvedTypes;
		};

		RemoveResolvedTypesFromRemainingList(InOutTypes, ResolvedTypes, RemainingTypesToResolve);

		// if the type hasn't been specified we need to slowly resolve the template
		// arguments until we hit a match. for this we'll create a list of arguments
		// to resolve and reduce the list slowly.
		bool bSuccessFullyResolvedRemainingTypes = true;
		while(!RemainingTypesToResolve.IsEmpty())
		{
			PermutationIndices.Reset();

			const FTypePair TypeToResolve = *RemainingTypesToResolve.begin();
			FTypeMap NewResolvedTypes = RemoveWildCardTypes(ResolvedTypes);
			NewResolvedTypes.FindOrAdd(TypeToResolve.Key) = TypeToResolve.Value;

			if(Resolve(NewResolvedTypes, PermutationIndices, true))
			{
				ResolvedTypes = NewResolvedTypes;
				RemoveResolvedTypesFromRemainingList(InOutTypes, ResolvedTypes, RemainingTypesToResolve);
			}
			else
			{
				// we were not able to resolve this argument, remove it from the resolved types list.
				RemainingTypesToResolve.Remove(TypeToResolve.Key);
				bSuccessFullyResolvedRemainingTypes = false;
			}
		}

		// if there is nothing left to resolve we were successful
		return RemainingTypesToResolve.IsEmpty() && bSuccessFullyResolvedRemainingTypes;
	}

	return false;
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
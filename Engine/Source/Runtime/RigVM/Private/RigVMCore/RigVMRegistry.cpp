// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMTypeUtils.h"
#include "RigVMModule.h"
#include "UObject/UObjectIterator.h"

FRigVMRegistry FRigVMRegistry::s_RigVMRegistry;
const FName FRigVMRegistry::TemplateNameMetaName = TEXT("TemplateName");

FRigVMRegistry& FRigVMRegistry::Get()
{
	s_RigVMRegistry.InitializeIfNeeded();
	return s_RigVMRegistry;
}

const TArray<UScriptStruct*>& FRigVMRegistry::GetMathTypes()
{
	// The list of base math types to automatically register 
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

	return MathTypes;
}


void FRigVMRegistry::InitializeIfNeeded()
{
	if(!Types.IsEmpty())
	{
		return;
	}

	Types.Reserve(512);
	TypeToIndex.Reserve(512);
	TypesPerCategory.Reserve(19);
	ArgumentsPerCategory.Reserve(19);
	
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_Execute, TArray<int32>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, TArray<int32>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, TArray<int32>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, TArray<int32>()).Reserve(256);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, TArray<int32>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, TArray<int32>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, TArray<int32>()).Reserve(8);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, TArray<int32>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, TArray<int32>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, TArray<int32>()).Reserve(GetMathTypes().Num());
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, TArray<int32>()).Reserve(128);
	TypesPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, TArray<int32>()).Reserve(128);

	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_Execute, TArray<TPair<int32,int32>>()).Reserve(8);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, TArray<TPair<int32,int32>>()).Reserve(64);
	ArgumentsPerCategory.Add(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, TArray<TPair<int32,int32>>()).Reserve(64);

	RigVMTypeUtils::TypeIndex::Execute = FindOrAddType(FRigVMTemplateArgumentType(FRigVMExecuteContext::StaticStruct()));
	RigVMTypeUtils::TypeIndex::Bool = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::BoolTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::Float = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FloatTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::Double = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::DoubleTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::Int32 = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::Int32TypeName, nullptr));
	RigVMTypeUtils::TypeIndex::UInt8 = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt8TypeName, nullptr));
	RigVMTypeUtils::TypeIndex::FName = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FNameTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::FString = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FStringTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::WildCard = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::GetWildCardCPPTypeName(), RigVMTypeUtils::GetWildCardCPPTypeObject()));
	RigVMTypeUtils::TypeIndex::BoolArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::BoolArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::FloatArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FloatArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::DoubleArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::DoubleArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::Int32Array = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::Int32ArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::UInt8Array = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::UInt8ArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::FNameArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FNameArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::FStringArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::FStringArrayTypeName, nullptr));
	RigVMTypeUtils::TypeIndex::WildCardArray = FindOrAddType(FRigVMTemplateArgumentType(RigVMTypeUtils::GetWildCardArrayCPPTypeName(), RigVMTypeUtils::GetWildCardCPPTypeObject()));

	// register the default math types
	for(UScriptStruct* MathType : GetMathTypes())
	{
		FindOrAddType(FRigVMTemplateArgumentType(MathType));
	}

	// add all user defined structs
	for (TObjectIterator<UScriptStruct> ScriptIt; ScriptIt; ++ScriptIt)
	{
		UScriptStruct* ScriptStruct = *ScriptIt;
		if(!IsAllowedType(ScriptStruct))
		{
			continue;
		}

		// if this is a C++ type - skip it
		if(ScriptStruct->IsNative() && !ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
		{
			continue;
		}

		const FString CPPType = ScriptStruct->GetStructCPPName();
		FindOrAddType(FRigVMTemplateArgumentType(ScriptStruct));
	}

	// add all user defined enums
	for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
	{
		UEnum* Enum = (*EnumIt);
		if(!IsAllowedType(Enum))
		{
			continue;
		}

		// if this is a C++ type - skip it
		if(Enum->IsNative())
		{
			continue;
		}

		const FString CPPType = Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType;
		FindOrAddType(FRigVMTemplateArgumentType(*CPPType, Enum));
	}
}

void FRigVMRegistry::Refresh()
{
}

int32 FRigVMRegistry::FindOrAddType(const FRigVMTemplateArgumentType& InType)
{
	int32 Index = GetTypeIndex(InType);
	if(Index == INDEX_NONE)
	{
		FRigVMTemplateArgumentType ElementType;
		ElementType.CPPType = NAME_None;
		if(InType.IsArray())
		{
			ElementType = InType;
			ElementType.ConvertToBaseElement();
		}
		FRigVMTemplateArgumentType ArrayType = InType;
		ArrayType.ConvertToArray();

		{
			FTypeInfo Info;
			Info.Type = InType;
			Info.bIsArray = InType.IsArray();
			if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(InType.CPPTypeObject))
			{
				Info.bIsExecute = ScriptStruct->IsChildOf(FRigVMExecuteContext::StaticStruct());
			}

			if(!ElementType.CPPType.IsNone())
			{
				Info.BaseTypeIndex = GetTypeIndex(ElementType);
			}
			Info.ArrayTypeIndex = GetTypeIndex(ArrayType);
			
			Index = Types.Add(Info);
		}

		TypeToIndex.Add(InType, Index);

		// add the type to the category map
		static constexpr TCHAR ArrayArrayPrefix[] = TEXT("TArray<TArray<");
		const int32 ArrayDimension = Types[Index].bIsArray ?
			(InType.CPPType.ToString().StartsWith(ArrayArrayPrefix) ? 2 : 1) : 0;
		const UObject* CPPTypeObject = Types[Index].Type.CPPTypeObject;

		// simple types
		if(CPPTypeObject == nullptr)
		{
			switch(ArrayDimension)
			{
				default:
				case 0:
				{
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleSimpleValue, Index);
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
					break;
				}
				case 1:
				{
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArraySimpleValue, Index);
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
					break;
				}
				case 2:
				{
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArraySimpleValue, Index);
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
					break;
				}
			}
		}
		else if(const UClass* Class = Cast<UClass>(CPPTypeObject))
		{
			if(IsAllowedType(Class))
			{
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleObjectValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayObjectValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}	
		}
		else if(const UEnum* Enum = Cast<UEnum>(CPPTypeObject))
		{
			if(IsAllowedType(Enum))
			{
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleEnumValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayEnumValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayEnumValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
		}
		else if(const UStruct* Struct = Cast<UStruct>(CPPTypeObject))
		{
			if(IsAllowedType(Struct))
			{
				if(GetMathTypes().Contains(CPPTypeObject))
				{
					switch(ArrayDimension)
					{
						default:
						case 0:
						{
							RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleMathStructValue, Index);
							break;
						}
						case 1:
						{
							RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayMathStructValue, Index);
							break;
						}
						case 2:
						{
							RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayMathStructValue, Index);
							break;
						}
					}
				}
				
				switch(ArrayDimension)
				{
					default:
					case 0:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleScriptStructValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue, Index);
						break;
					}
					case 1:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayScriptStructValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue, Index);
						break;
					}
					case 2:
					{
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayScriptStructValue, Index);
						RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_ArrayArrayAnyValue, Index);
						break;
					}
				}
			}
			else if(Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				if(!Types[Index].bIsArray)
				{
					RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory_Execute, Index);
				}
			}
		}

		// register the opposing type
		if(!Types[Index].bIsArray)
		{
			Types[Index].ArrayTypeIndex = FindOrAddType(ArrayType);
			if(Types[Index].ArrayTypeIndex != INDEX_NONE)
			{
				Types[Types[Index].ArrayTypeIndex].BaseTypeIndex = Index;
			}
		}
		else
		{
			Types[Index].BaseTypeIndex = FindOrAddType(ElementType);
			if(Types[Index].BaseTypeIndex != INDEX_NONE)
			{
				Types[Types[Index].BaseTypeIndex].ArrayTypeIndex = Index;

				// also automatically do the two dimensional array
				if(GetArrayDimensionsForType(Index) == 1)
				{
					Types[Index].ArrayTypeIndex = FindOrAddType(ArrayType);
					if(Types[Index].ArrayTypeIndex != INDEX_NONE)
					{
						Types[Types[Index].ArrayTypeIndex].BaseTypeIndex = Index;
					}
				}
			}
		}

		// if the type is a structure
		// then add all of its sub property types
		if(!Types[Index].bIsArray)
		{
			if(const UStruct* Struct = Cast<UStruct>(Types[Index].Type.CPPTypeObject))
			{
				for (TFieldIterator<FProperty> It(Struct); It; ++It)
				{
					FProperty* Property = *It;
					if(IsAllowedType(Property, true))
					{
						// by creating a template argument for the child property
						// the type will be added by calling ::FindOrAddType recursively.
						FRigVMTemplateArgument DummyArgument(Property);
					}
				}
			}
		}
	}
	
	return Index;
}

void FRigVMRegistry::RegisterTypeInCategory(FRigVMTemplateArgument::ETypeCategory InCategory, int32 InTypeIndex)
{
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);

	TypesPerCategory.FindChecked(InCategory).Add(InTypeIndex);

	// when adding a new type - we need to update template arguments which expect to have access to that type 
	const TArray<TPair<int32,int32>>& ArgumentsToUseType = ArgumentsPerCategory.FindChecked(InCategory);
	for(const TPair<int32,int32>& Pair : ArgumentsToUseType)
	{
		FRigVMTemplate& Template = Templates[Pair.Key];
		const FRigVMTemplateArgument* Argument = Template.GetArgument(Pair.Value);
		Template.AddTypeForArgument(Argument->GetName(), InTypeIndex);
	}
}

int32 FRigVMRegistry::GetTypeIndex(const FRigVMTemplateArgumentType& InType) const
{
	if(const int32* Index = TypeToIndex.Find(InType))
	{
		return *Index;
	}
	return INDEX_NONE;
}

const FRigVMTemplateArgumentType& FRigVMRegistry::GetType(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].Type;
	}
	static FRigVMTemplateArgumentType EmptyType;
	return EmptyType;
}

const FRigVMTemplateArgumentType& FRigVMRegistry::FindTypeFromCPPType(const FString& InCPPType) const
{
	const int32 TypeIndex = GetTypeIndexFromCPPType(InCPPType);
	if(ensure(Types.IsValidIndex(TypeIndex)))
	{
		return Types[TypeIndex].Type;
	}

	static FRigVMTemplateArgumentType EmptyType;
	return EmptyType;
}

int32 FRigVMRegistry::GetTypeIndexFromCPPType(const FString& InCPPType) const
{
	if(ensure(!InCPPType.IsEmpty()))
	{
		const FName CPPTypeName = *InCPPType;
		return Types.IndexOfByPredicate([CPPTypeName](const FTypeInfo& Info) -> bool
		{
			return Info.Type.CPPType == CPPTypeName;
		});
	}
	return INDEX_NONE;
}

bool FRigVMRegistry::IsArrayType(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].bIsArray;
	}
	return false;
}

bool FRigVMRegistry::IsExecuteType(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].bIsExecute;
	}
	return false;
}

int32 FRigVMRegistry::GetArrayDimensionsForType(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		const FTypeInfo& Info = Types[InTypeIndex];
		if(Info.bIsArray)
		{
			return 1 + GetArrayDimensionsForType(Info.BaseTypeIndex);
		}
	}
	return 0;
}

bool FRigVMRegistry::IsWildCardType(int32 InTypeIndex) const
{
	return RigVMTypeUtils::TypeIndex::WildCard == InTypeIndex ||
		RigVMTypeUtils::TypeIndex::WildCardArray == InTypeIndex;
}

bool FRigVMRegistry::CanMatchTypes(int32 InTypeIndexA, int32 InTypeIndexB, bool bAllowFloatingPointCasts) const
{
	if(!ensure(Types.IsValidIndex(InTypeIndexA)) || !ensure(Types.IsValidIndex(InTypeIndexB)))
	{
		return false;
	}

	if(InTypeIndexA == InTypeIndexB)
	{
		return true;
	}

	if(bAllowFloatingPointCasts)
	{
		// swap order since float is known to registered before double
		if(InTypeIndexA > InTypeIndexB)
		{
			Swap(InTypeIndexA, InTypeIndexB);
		}
		if(InTypeIndexA == RigVMTypeUtils::TypeIndex::Float && InTypeIndexB == RigVMTypeUtils::TypeIndex::Double)
		{
			return true;
		}
		if(InTypeIndexA == RigVMTypeUtils::TypeIndex::FloatArray && InTypeIndexB == RigVMTypeUtils::TypeIndex::DoubleArray)
		{
			return true;
		}
	}
	return false;
}

const TArray<int32>& FRigVMRegistry::GetCompatibleTypes(int32 InTypeIndex) const
{
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::Float)
	{
		static const TArray<int32> CompatibleTypes = {RigVMTypeUtils::TypeIndex::Double};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::Double)
	{
		static const TArray<int32> CompatibleTypes = {RigVMTypeUtils::TypeIndex::Float};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::FloatArray)
	{
		static const TArray<int32> CompatibleTypes = {RigVMTypeUtils::TypeIndex::DoubleArray};
		return CompatibleTypes;
	}
	if(InTypeIndex == RigVMTypeUtils::TypeIndex::DoubleArray)
	{
		static const TArray<int32> CompatibleTypes = {RigVMTypeUtils::TypeIndex::FloatArray};
		return CompatibleTypes;
	}

	static const TArray<int32> EmptyTypes;
	return EmptyTypes;
}

const TArray<int32>& FRigVMRegistry::GetTypesForCategory(FRigVMTemplateArgument::ETypeCategory InCategory)
{
	check(InCategory != FRigVMTemplateArgument::ETypeCategory_Invalid);
	return TypesPerCategory.FindChecked(InCategory);
}

int32 FRigVMRegistry::GetArrayTypeFromBaseTypeIndex(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].ArrayTypeIndex;
	}
	return INDEX_NONE;
}

int32 FRigVMRegistry::GetBaseTypeFromArrayTypeIndex(int32 InTypeIndex) const
{
	if(ensure(Types.IsValidIndex(InTypeIndex)))
	{
		return Types[InTypeIndex].BaseTypeIndex;
	}
	return INDEX_NONE;
}

bool FRigVMRegistry::IsAllowedType(const FProperty* InProperty, bool bCheckFlags)
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

bool FRigVMRegistry::IsAllowedType(const UEnum* InEnum)
{
	return !InEnum->HasAnyFlags(DisallowedFlags()) && InEnum->HasAllFlags(NeededFlags());
}

bool FRigVMRegistry::IsAllowedType(const UStruct* InStruct)
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
	if(InStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
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

bool FRigVMRegistry::IsAllowedType(const UClass* InClass)
{
	if(InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_Abstract))
	{
		return false;
	}

	// note: currently we don't allow UObjects
	return false;
	//return IsAllowedType(Cast<UStruct>(InClass));
}


void FRigVMRegistry::Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct, const TArray<FRigVMFunctionArgument>& InArguments)
{
	if (FindFunction(InName) != nullptr)
	{
		return;
	}

	const FRigVMFunction Function(InName, InFunctionPtr, InStruct, Functions.Num(), InArguments);
	Functions.AddElement(Function);
	FunctionNameToIndex.Add(InName, Function.Index);

	// register all of the types used by the function
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		// creating the argument causes the registration
		FRigVMTemplateArgument Argument(*It);
	}

#if WITH_EDITOR
	
	FString TemplateMetadata;
	if (InStruct->GetStringMetaDataHierarchical(TemplateNameMetaName, &TemplateMetadata))
	{
		if(InStruct->HasMetaData(FRigVMStruct::DeprecatedMetaName))
		{
			return;
		}

		FString MethodName;
		if (FString(InName).Split(TEXT("::"), nullptr, &MethodName))
		{
			const FString TemplateName = FString::Printf(TEXT("%s::%s"), *TemplateMetadata, *MethodName);
			FRigVMTemplate Template(InStruct, TemplateName, Function.Index);
			if (Template.IsValid())
			{
				bool bWasMerged = false;

				const int32* ExistingTemplateIndexPtr = TemplateNotationToIndex.Find(Template.GetNotation());
				if(ExistingTemplateIndexPtr)
				{
					FRigVMTemplate& ExistingTemplate = Templates[*ExistingTemplateIndexPtr];
					if (ExistingTemplate.Merge(Template))
					{
						Functions[Function.Index].TemplateIndex = ExistingTemplate.Index;
						bWasMerged = true;
					}
				}

				if (!bWasMerged)
				{
					Template.Index = Templates.Num();
					Functions[Function.Index].TemplateIndex = Template.Index;
					Templates.AddElement(Template);
					
					if(ExistingTemplateIndexPtr == nullptr)
					{
						TemplateNotationToIndex.Add(Template.GetNotation(), Template.Index);
					}
				}
			}
		}
	}

#endif
}

const FRigVMFunction* FRigVMRegistry::FindFunction(const TCHAR* InName) const
{
	if(const int32* FunctionIndexPtr = FunctionNameToIndex.Find(InName))
	{
		return &Functions[*FunctionIndexPtr];
	}
	return nullptr;
}

const FRigVMFunction* FRigVMRegistry::FindFunction(UScriptStruct* InStruct, const TCHAR* InName) const
{
	check(InStruct);
	check(InName);
	
	const FString FunctionName = FString::Printf(TEXT("%s::%s"), *InStruct->GetStructCPPName(), InName);
	return FindFunction(*FunctionName);
}

const TChunkedArray<FRigVMFunction>& FRigVMRegistry::GetFunctions() const
{
	return Functions;
}

const FRigVMTemplate* FRigVMRegistry::FindTemplate(const FName& InNotation) const
{
	if (InNotation.IsNone())
	{
		return nullptr;
	}

	if(const int32* TemplateIndexPtr = TemplateNotationToIndex.Find(InNotation))
	{
		return &Templates[*TemplateIndexPtr];
	}

	return nullptr;
}

const TChunkedArray<FRigVMTemplate>& FRigVMRegistry::GetTemplates() const
{
	return Templates;
}

const FRigVMTemplate* FRigVMRegistry::GetOrAddTemplateFromArguments(const FName& InName, const TArray<FRigVMTemplateArgument>& InArguments, const FRigVMTemplateDelegates& InDelegates)
{
	FRigVMTemplate Template(InName, InArguments, INDEX_NONE);
	if(const FRigVMTemplate* ExistingTemplate = FindTemplate(Template.GetNotation()))
	{
		return ExistingTemplate;
	}

	// we only support to ask for templates here which provide singleton types
	int32 NumPermutations = 1;
	for(const FRigVMTemplateArgument& Argument : InArguments)
	{
		if(!Argument.IsSingleton() && NumPermutations > 1)
		{
			if(Argument.TypeIndices.Num() != NumPermutations)
			{
				UE_LOG(LogRigVM, Error, TEXT("Failed to add template '%s' since the arguments' types counts don't match."), *InName.ToString());
				return nullptr;
			}
		}
		NumPermutations = FMath::Max(NumPermutations, Argument.TypeIndices.Num()); 
	}

	// if any of the arguments are wildcards we'll need to update the types
	for(FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		if(Argument.TypeIndices.Num() == 1 && IsWildCardType(Argument.TypeIndices[0]))
		{
			if(IsArrayType(Argument.TypeIndices[0]))
			{
				Argument.TypeIndices = GetTypesForCategory(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
				Argument.TypeCategories.Add(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
			}
			else
			{
				Argument.TypeIndices = GetTypesForCategory(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
				Argument.TypeCategories.Add(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
			}

			for (int32 i = 0; i < Argument.TypeIndices.Num(); ++i)
			{
				Argument.TypeToPermutations.Add(Argument.TypeIndices[i], {i});				
			}
			
			NumPermutations = FMath::Max(NumPermutations, Argument.TypeIndices.Num()); 
		}
	}

	// if we have more than one permutation we may need to upgrade the types for singleton args
	if(NumPermutations > 1)
	{
		for(FRigVMTemplateArgument& Argument : Template.Arguments)
		{
			if(Argument.TypeIndices.Num() == 1)
			{
				const int32 TypeIndex = Argument.TypeIndices[0];
				Argument.TypeIndices.SetNum(NumPermutations);
				TArray<int32> ArgTypePermutations;
				ArgTypePermutations.SetNum(NumPermutations);
				for(int32 Index=0;Index<NumPermutations;Index++)
				{
					Argument.TypeIndices[Index] = TypeIndex;
					ArgTypePermutations[Index] = Index;
				}
				Argument.TypeToPermutations.Add(TypeIndex, ArgTypePermutations);
			}
		}
	}

	Template.Permutations.SetNum(NumPermutations);
	for(int32 Index=0;Index<NumPermutations;Index++)
	{
		Template.Permutations[Index] = INDEX_NONE;
	}

	const int32 Index = Templates.AddElement(Template);
	Templates[Index].Index = Index;
	Templates[Index].OnNewArgumentType() = InDelegates.NewArgumentTypeDelegate;
	TemplateNotationToIndex.Add(Template.GetNotation(), Index);

	for(int32 ArgumentIndex=0; ArgumentIndex < Templates[Index].Arguments.Num(); ArgumentIndex++)
	{
		for(const FRigVMTemplateArgument::ETypeCategory& ArgumentTypeCategory : Templates[Index].Arguments[ArgumentIndex].TypeCategories)
		{
			ArgumentsPerCategory.FindChecked(ArgumentTypeCategory).AddUnique(TPair<int32, int32>(Index, ArgumentIndex));
		}
	}
	
	return &Templates[Index];
}

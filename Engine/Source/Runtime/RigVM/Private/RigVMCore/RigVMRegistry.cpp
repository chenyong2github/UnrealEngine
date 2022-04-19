// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "UObject/UObjectIterator.h"

FRigVMRegistry FRigVMRegistry::s_RigVMRegistry;
const FName FRigVMRegistry::TemplateNameMetaName = TEXT("TemplateName");

FRigVMRegistry& FRigVMRegistry::Get()
{
	return s_RigVMRegistry;
}

void FRigVMRegistry::Refresh()
{
}

void FRigVMRegistry::Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct, const TArray<FRigVMFunctionArgument>& InArguments)
{
	if (FindFunction(InName) != nullptr)
	{
		return;
	}

	const FRigVMFunction Function(InName, InFunctionPtr, InStruct, Functions.Num(), InArguments);
	Functions.Add(Function);
	FunctionNameToIndex.Add(InName, Function.Index);

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
					Templates.Add(Template);

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

const TArray<FRigVMFunction>& FRigVMRegistry::GetFunctions() const
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

const TArray<FRigVMTemplate>& FRigVMRegistry::GetTemplates() const
{
	return Templates;
}

const FRigVMTemplate* FRigVMRegistry::GetOrAddTemplateFromArguments(const FName& InName, const TArray<FRigVMTemplateArgument>& InArguments)
{
	FRigVMTemplate Template(InName, InArguments, INDEX_NONE);
	if(const FRigVMTemplate* ExistingTemplate = FindTemplate(Template.GetNotation()))
	{
		return ExistingTemplate;
	}

	// we only support to ask for templates here which provide singleton types
	for(const FRigVMTemplateArgument& Argument : InArguments)
	{
		if(!Argument.bSingleton || Argument.Types.Num() > 1)
		{
			return nullptr;
		}
	}

	int32 NumPermutations = 1;

	// if any of the arguments are wildcards we'll need to update the types
	for(FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		if(Argument.Types[0].IsWildCard())
		{
			static TArray<FRigVMTemplateArgument::FType> AllTypes, AllArrayTypes;
			if(AllTypes.IsEmpty())
			{
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::BoolType));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::Int32Type));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::UInt8Type));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FloatType));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::DoubleType));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FNameType));
				AllTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FStringType));

				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::BoolArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::Int32ArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::UInt8ArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FloatArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::DoubleArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FNameArrayType));
				AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::FStringArrayType));

				struct FTypeTraverser
				{
					static EObjectFlags DisallowedFlags()
					{
						return RF_BeginDestroyed | RF_FinishDestroyed | RF_PendingKill;
					}

					static EObjectFlags NeededFlags()
					{
						return RF_Public;
					}
					
					static bool IsAllowedType(const FProperty* InProperty)
					{
						if(!InProperty->HasAnyPropertyFlags(
							CPF_BlueprintVisible |
							CPF_BlueprintReadOnly |
							CPF_Edit))
						{
							return false;
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
							return IsAllowedType(ArrayProperty->Inner);
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
						if(InStruct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
						{
							return false;
						}
						if(InStruct->IsChildOf(FRigVMStruct::StaticStruct()))
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
					AllTypes.Add(FRigVMTemplateArgument::FType(CPPType, ScriptStruct));
					AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), ScriptStruct));
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
					AllTypes.Add(FRigVMTemplateArgument::FType(CPPType, Enum));
					AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Enum));
				}

				// add all classes
				for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
				{
					UClass* Class = *ClassIt;
					if(!FTypeTraverser::IsAllowedType(Class))
					{
						continue;
					}

					const FString CPPType = Class->GetPrefixCPP() + Class->GetName();
					AllTypes.Add(FRigVMTemplateArgument::FType(CPPType, Class));
					AllArrayTypes.Add(FRigVMTemplateArgument::FType(RigVMTypeUtils::ArrayTypeFromBaseType(CPPType), Class));
				}
			}

			Argument.Types = Argument.Types[0].IsArray() ? AllArrayTypes : AllTypes;
			Argument.bSingleton = false;
			NumPermutations = Argument.Types.Num(); 
		}
	}

	// if we have more than one permutation we may need to upgrade the types for singleton args
	if(NumPermutations > 1)
	{
		for(FRigVMTemplateArgument& Argument : Template.Arguments)
		{
			if(Argument.Types.Num() == 1)
			{
				const FRigVMTemplateArgument::FType Type = Argument.Types[0];
				Argument.Types.SetNum(NumPermutations);
				for(int32 Index=0;Index<NumPermutations;Index++)
				{
					Argument.Types[Index] = Type;
				}
				Argument.bSingleton = true;
			}
		}
	}

	Template.Permutations.SetNum(NumPermutations);
	for(int32 Index=0;Index<NumPermutations;Index++)
	{
		Template.Permutations[Index] = INDEX_NONE;
	}

	const int32 Index = Templates.Add(Template);
	Templates[Index].Index = Index;
	TemplateNotationToIndex.Add(Template.GetNotation(), Index);
	return &Templates[Index];
}

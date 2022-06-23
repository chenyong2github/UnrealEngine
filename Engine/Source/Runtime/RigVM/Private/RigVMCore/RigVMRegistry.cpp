// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMModule.h"
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
	Functions.AddElement(Function);
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

const FRigVMTemplate* FRigVMRegistry::GetOrAddTemplateFromArguments(const FName& InName, const TArray<FRigVMTemplateArgument>& InArguments)
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
			if(Argument.Types.Num() != NumPermutations)
			{
				UE_LOG(LogRigVM, Error, TEXT("Failed to add template '%s' since the arguments' types counts don't match."), *InName.ToString());
				return nullptr;
			}
		}
		NumPermutations = FMath::Max(NumPermutations, Argument.Types.Num()); 
	}

	// if any of the arguments are wildcards we'll need to update the types
	for(FRigVMTemplateArgument& Argument : Template.Arguments)
	{
		if(Argument.Types.Num() == 1 && Argument.Types[0].IsWildCard())
		{
			if(Argument.Types[0].IsArray())
			{
				Argument.Types = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue);
			}
			else
			{
				Argument.Types = FRigVMTemplateArgument::GetCompatibleTypes(FRigVMTemplateArgument::ETypeCategory_SingleAnyValue);
			}

			for (int32 i = 0; i < Argument.Types.Num(); ++i)
			{
				Argument.TypeToPermutations.Add(Argument.Types[i].CPPType, {i});				
			}
			
			NumPermutations = FMath::Max(NumPermutations, Argument.Types.Num()); 
		}
	}

	// if we have more than one permutation we may need to upgrade the types for singleton args
	if(NumPermutations > 1)
	{
		for(FRigVMTemplateArgument& Argument : Template.Arguments)
		{
			if(Argument.Types.Num() == 1)
			{
				const FRigVMTemplateArgumentType Type = Argument.Types[0];
				Argument.Types.SetNum(NumPermutations);
				TArray<int32> ArgTypePermutations;
				ArgTypePermutations.SetNum(NumPermutations);
				for(int32 Index=0;Index<NumPermutations;Index++)
				{
					Argument.Types[Index] = Type;
					ArgTypePermutations[Index] = Index;
				}
				Argument.TypeToPermutations.Add(Type.CPPType, ArgTypePermutations);
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
	TemplateNotationToIndex.Add(Template.GetNotation(), Index);
	return &Templates[Index];
}

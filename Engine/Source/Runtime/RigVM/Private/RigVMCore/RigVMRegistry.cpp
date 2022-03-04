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

void FRigVMRegistry::Register(const TCHAR* InName, FRigVMFunctionPtr InFunctionPtr, UScriptStruct* InStruct)
{
	if (FindFunction(InName) != nullptr)
	{
		return;
	}

	const FRigVMFunction Function(InName, InFunctionPtr, InStruct, Functions.Num());
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



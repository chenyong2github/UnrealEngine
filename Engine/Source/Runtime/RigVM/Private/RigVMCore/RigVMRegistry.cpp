// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "UObject/UObjectIterator.h"

FRigVMRegistry FRigVMRegistry::s_RigVMRegistry;
const FName FRigVMRegistry::PrototypeNameMetaName = TEXT("PrototypeName");

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

	FRigVMFunction Function(InName, InFunctionPtr, InStruct, Functions.Num());
	Functions.Add(Function);

#if WITH_EDITOR
	
	FString PrototypeMetadata;
	if (InStruct->GetStringMetaDataHierarchical(PrototypeNameMetaName, &PrototypeMetadata))
	{
		if(InStruct->HasMetaData(FRigVMStruct::DeprecatedMetaName))
		{
			return;
		}

		FString MethodName;
		if (FString(InName).Split(TEXT("::"), nullptr, &MethodName))
		{
			FString PrototypeName = FString::Printf(TEXT("%s::%s"), *PrototypeMetadata, *MethodName);
			FRigVMPrototype Prototype(InStruct, PrototypeName, Function.Index);
			if (Prototype.IsValid())
			{
				bool bWasMerged = false;
				for (FRigVMPrototype& ExistingPrototype : Prototypes)
				{
					if (ExistingPrototype.Merge(Prototype))
					{
						Functions[Function.Index].PrototypeIndex = ExistingPrototype.Index;
						bWasMerged = true;
					}
				}

				if (!bWasMerged)
				{
					Prototype.Index = Prototypes.Num();
					Functions[Function.Index].PrototypeIndex = Prototype.Index;
					Prototypes.Add(Prototype);
				}
			}
		}
	}

#endif
}

FRigVMFunctionPtr FRigVMRegistry::FindFunction(const TCHAR* InName) const
{
	for (const FRigVMFunction& Function : Functions)
	{
		if (FCString::Strcmp(Function.Name, InName) == 0)
		{
			return Function.FunctionPtr;
		}
	}

	return nullptr;
}

const FRigVMPrototype* FRigVMRegistry::FindPrototype(const FName& InNotation) const
{
	if (InNotation.IsNone())
	{
		return nullptr;
	}

	for (const FRigVMPrototype& Prototype : Prototypes)
	{
		if (Prototype.GetNotation() == InNotation)
		{
			return &Prototype;
		}
	}

	return nullptr;
}

// Returns a prototype pointer given its notation (or nullptr)
const FRigVMPrototype* FRigVMRegistry::FindPrototype(UScriptStruct* InStruct, const FString& InPrototypeName) const
{
	FName Notation = FRigVMPrototype::GetNotationFromStruct(InStruct, InPrototypeName);
	return FindPrototype(Notation);
}

const TArray<FRigVMFunction>& FRigVMRegistry::GetFunctions() const
{
	return Functions;
}

const TArray<FRigVMPrototype>& FRigVMRegistry::GetPrototypes() const
{
	return Prototypes;
}



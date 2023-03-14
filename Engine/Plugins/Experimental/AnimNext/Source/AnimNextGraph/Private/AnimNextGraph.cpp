// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextGraph.h"
#include "RigVMCore/RigVMMemoryStorage.h"
#include "RigUnit_AnimNextBeginExecution.h"
#include "UnitContext.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextGraph)

namespace UE::AnimNext::Graph
{
const FName EntryPointName("GetData");
const FName ResultName("Result");
}

UE::AnimNext::FParamTypeHandle UAnimNextGraph::GetReturnTypeHandleImpl() const
{
	return ReturnType.GetHandle();
}

bool UAnimNextGraph::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	bool bResult = true;
	
	if(RigVM)
	{
		FAnimNextExecuteContext& AnimNextInterfaceContext = RigVM->GetContext().GetPublicDataSafe<FAnimNextExecuteContext>();

		AnimNextInterfaceContext.SetContextData(this, Context, bResult);

		bResult &= (RigVM->Execute(TArray<URigVMMemoryStorage*>(), FRigUnit_AnimNextBeginExecution::EventName) != ERigVMExecuteResult::Failed);
	}
	
	return bResult;
}

void UAnimNextGraph::SetReturnTypeHandle(UE::AnimNext::FParamTypeHandle InHandle)
{
	ReturnType = InHandle.GetType();
}

TArray<FRigVMExternalVariable> UAnimNextGraph::GetRigVMExternalVariables()
{
	return TArray<FRigVMExternalVariable>(); 
}

static TArray<UClass*> GetClassObjectsInPackage(UPackage* InPackage)
{
	TArray<UObject*> Objects;
	GetObjectsWithOuter(InPackage, Objects, false);

	TArray<UClass*> ClassObjects;
	for (UObject* Object : Objects)
	{
		if (UClass* Class = Cast<UClass>(Object))
		{
			ClassObjects.Add(Class);
		}
	}

	return ClassObjects;
}

void UAnimNextGraph::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	// Whenever the asset is renamed/moved, generated classes parented to the old package
	// are not moved to the new package automatically (see FAssetRenameManager), so we
	// have to manually perform the move/rename, to avoid invalid reference to the old package

	// Note: while asset duplication doesn't duplicate the classes either, it is not a problem there
	// because we always recompile in post duplicate.
	TArray<UClass*> ClassObjects = GetClassObjectsInPackage(OldOuter->GetPackage());

	for (UClass* ClassObject : ClassObjects)
	{
		if (URigVMMemoryStorageGeneratorClass* MemoryClass = Cast<URigVMMemoryStorageGeneratorClass>(ClassObject))
		{
			MemoryClass->Rename(nullptr, GetPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}
}

void UAnimNextGraph::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	TArray<UClass*> ClassObjects = GetClassObjectsInPackage(GetPackage());

	for (UClass* ClassObject : ClassObjects)
	{
		if (URigVMMemoryStorageGeneratorClass* MemoryClass = Cast<URigVMMemoryStorageGeneratorClass>(ClassObject))
		{
			OutDeps.Add(MemoryClass);
		}
	}
}

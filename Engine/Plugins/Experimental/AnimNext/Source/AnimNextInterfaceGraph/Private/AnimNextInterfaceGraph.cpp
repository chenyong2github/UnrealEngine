// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextInterfaceGraph.h"
#include "RigVMCore/RigVMMemoryStorage.h"
#include "RigUnit_AnimNextInterfaceBeginExecution.h"
#include "AnimNextInterfaceUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextInterfaceGraph)

namespace UE::AnimNext::InterfaceGraph
{
const FName EntryPointName("GetData");
const FName ResultName("Result");
}

FName UAnimNextInterfaceGraph::GetReturnTypeNameImpl() const
{
	// TODO: this needs to be user-defined & determined at creation time, using float for now
	return TNameOf<float>::GetName();
}

const UScriptStruct* UAnimNextInterfaceGraph::GetReturnTypeStructImpl() const
{
	// TODO: this needs to be user-defined & determined at creation time, using nullptr (for float) for now
	return nullptr;
}

bool UAnimNextInterfaceGraph::GetDataImpl(const UE::AnimNext::Interface::FContext& Context) const
{
	bool bResult = true;
	
	if(RigVM)
	{
		FRigUnitContext RigUnitContext;
		FAnimNextInterfaceUnitContext AnimNextInterfaceUnitContext(this, Context, bResult);
		bResult &=
			(RigVM->Execute(TArray<URigVMMemoryStorage*>(), FRigUnit_AnimNextInterfaceBeginExecution::EventName) != ERigVMExecuteResult::Failed);
	}
	
	return bResult;
}

TArray<FRigVMExternalVariable> UAnimNextInterfaceGraph::GetRigVMExternalVariables()
{
	return TArray<FRigVMExternalVariable>(); 
}

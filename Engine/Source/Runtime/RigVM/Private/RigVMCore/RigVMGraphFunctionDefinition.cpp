// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMGraphFunctionDefinition)

FRigVMExternalVariable FRigVMGraphFunctionArgument::GetExternalVariable() const
{
	FRigVMExternalVariable Variable;
	Variable.Name = Name;
	Variable.bIsArray = bIsArray;
	Variable.TypeName = CPPType;
	
	if(CPPTypeObject.IsValid())
	{
		Variable.TypeObject = CPPTypeObject.Get();
	}

	return Variable;
}

bool FRigVMGraphFunctionHeader::IsMutable() const
{
	for(const FRigVMGraphFunctionArgument& Arg : Arguments)
	{
		if(Arg.CPPTypeObject.IsValid())
		{
			if(UScriptStruct* Struct = Cast<UScriptStruct>(Arg.CPPTypeObject.Get()))
			{
				if(Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					return true;
				}
			}
		}
	}
	return false;
}

IRigVMGraphFunctionHost* FRigVMGraphFunctionHeader::GetFunctionHost() const
{
	UObject* HostObj = LibraryPointer.HostObject.ResolveObject();
	if (!HostObj)
	{
		HostObj = LibraryPointer.HostObject.TryLoad();
	}
	if (HostObj)
	{
		return Cast<IRigVMGraphFunctionHost>(HostObj);
	}
	return nullptr;
}

FRigVMGraphFunctionData* FRigVMGraphFunctionHeader::GetFunctionData() const
{
	if (IRigVMGraphFunctionHost* Host = GetFunctionHost())
	{
		return Host->GetRigVMGraphFunctionStore()->FindFunction(LibraryPointer);
	}
	return nullptr;
}

bool FRigVMGraphFunctionData::IsMutable() const
{
	return Header.IsMutable();
}

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

	if(IsCPPTypeObjectValid())
	{
		Variable.TypeObject = CPPTypeObject.Get();
	}

	return Variable;
}

bool FRigVMGraphFunctionArgument::IsCPPTypeObjectValid() const
{
	if(!CPPTypeObject.IsValid())
	{
		// this is potentially a user defined struct or user defined enum
		// so we have to try to load it.
		(void)CPPTypeObject.LoadSynchronous();
	}
	return CPPTypeObject.IsValid();
}

bool FRigVMGraphFunctionHeader::IsMutable() const
{
	for(const FRigVMGraphFunctionArgument& Arg : Arguments)
	{
		if(Arg.IsCPPTypeObjectValid())
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

IRigVMGraphFunctionHost* FRigVMGraphFunctionHeader::GetFunctionHost(bool bLoadIfNecessary) const
{
	UObject* HostObj = LibraryPointer.HostObject.ResolveObject();
	if (!HostObj && bLoadIfNecessary)
	{
		HostObj = LibraryPointer.HostObject.TryLoad();
	}
	if (HostObj)
	{
		return Cast<IRigVMGraphFunctionHost>(HostObj);
	}
	return nullptr;
}

FRigVMGraphFunctionData* FRigVMGraphFunctionHeader::GetFunctionData(bool bLoadIfNecessary) const
{
	if (IRigVMGraphFunctionHost* Host = GetFunctionHost(bLoadIfNecessary))
	{
		return Host->GetRigVMGraphFunctionStore()->FindFunction(LibraryPointer);
	}
	return nullptr;
}

void FRigVMGraphFunctionHeader::PostDuplicateHost(const FString& InOldPathName, const FString& InNewPathName)
{
	const FString OldPathName = InOldPathName + TEXT(":");
	const FString NewPathName = InNewPathName + TEXT(":");

	auto ReplacePathName = [InOldPathName, InNewPathName, OldPathName, NewPathName](FSoftObjectPath& InOutObjectPath)
	{
		FString PathName = InOutObjectPath.ToString();
		if(PathName.Equals(InOldPathName, ESearchCase::CaseSensitive))
		{
			InOutObjectPath = FSoftObjectPath(InNewPathName);
		}
		else if(PathName.StartsWith(OldPathName, ESearchCase::CaseSensitive))
		{
			PathName = NewPathName + PathName.Mid(OldPathName.Len());
			InOutObjectPath = FSoftObjectPath(PathName);
		}
	};

	ReplacePathName(LibraryPointer.LibraryNode);
	ReplacePathName(LibraryPointer.HostObject);
	for (TPair<FRigVMGraphFunctionIdentifier, uint32>& Pair : Dependencies)
	{
		ReplacePathName(Pair.Key.LibraryNode);
		ReplacePathName(Pair.Key.HostObject);
	}
}

bool FRigVMGraphFunctionData::IsMutable() const
{
	return Header.IsMutable();
}

void FRigVMGraphFunctionData::PostDuplicateHost(const FString& InOldHostPathName, const FString& InNewHostPathName)
{
	Header.PostDuplicateHost(InOldHostPathName, InNewHostPathName);
}

FRigVMGraphFunctionData* FRigVMGraphFunctionData::FindFunctionData(const FRigVMGraphFunctionIdentifier& InIdentifier, bool* bOutIsPublic)
{
	IRigVMGraphFunctionHost* FunctionHost = nullptr;
	if (UObject* FunctionHostObj = InIdentifier.HostObject.TryLoad())
	{
		FunctionHost = Cast<IRigVMGraphFunctionHost>(FunctionHostObj);									
	}

	if (!FunctionHost)
	{
		return nullptr;
	}

	FRigVMGraphFunctionStore* FunctionStore = FunctionHost->GetRigVMGraphFunctionStore();
	if (!FunctionStore)
	{
		return nullptr;
	}

	return FunctionStore->FindFunction(InIdentifier, bOutIsPublic);
}


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMCore/RigVMStruct.h"
#include "DataInterfaceUnitContext.h"
#include "DataInterfaceExecuteContext.generated.h"

class IDataInterface;

namespace UE::DataInterface
{
	struct FContext;
}

USTRUCT(BlueprintType)
struct FDataInterfaceExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FDataInterfaceExecuteContext()
		: FRigVMExecuteContext()
	, DataInterfaceContext(nullptr)
	, Interface(nullptr)
	, ResultPtr(nullptr)
	, UnitContext()
	{
	}

	const UE::DataInterface::FContext& GetContext() const
	{
		check(DataInterfaceContext);
		return *DataInterfaceContext;
	}

	const FDataInterfaceUnitContext& GetUnitContext() const
	{
		return UnitContext;
	}

	void SetResult(bool bInResult) const
	{
		check(ResultPtr);
		*ResultPtr &= bInResult;
	}

	const IDataInterface* GetInterface() const
	{
		check(Interface);
		return Interface;
	}
	
	virtual void Copy(const FRigVMExecuteContext* InOtherContext) override
	{
		Super::Copy(InOtherContext);

		const FDataInterfaceExecuteContext* OtherContext = (const FDataInterfaceExecuteContext*)InOtherContext; 
		DataInterfaceContext = OtherContext->DataInterfaceContext;
		Interface = OtherContext->Interface; 
		ResultPtr = OtherContext->ResultPtr; 
	}


private:
	const UE::DataInterface::FContext* DataInterfaceContext;
	const IDataInterface* Interface;
	bool* ResultPtr;
	FDataInterfaceUnitContext UnitContext;
};

USTRUCT(meta=(ExecuteContext="FDataInterfaceExecuteContext"))
struct FRigUnit_DataInterfaceBase : public FRigVMStruct
{
	GENERATED_BODY()
};

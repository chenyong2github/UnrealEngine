// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Units/RigUnit.h"
#include "UnitContext.h"
#include "ExecuteContext.generated.h"

class IAnimNextInterface;

namespace UE::AnimNext
{
	struct FContext;
}

USTRUCT(BlueprintType)
struct FAnimNextExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FAnimNextExecuteContext()
		: FRigVMExecuteContext()
	, InterfaceContext(nullptr)
	, Interface(nullptr)
	, ResultPtr(nullptr)
	, UnitContext()
	{
	}

	const UE::AnimNext::FContext& GetContext() const
	{
		check(InterfaceContext);
		return *InterfaceContext;
	}

	const FRigUnitContext& GetUnitContext() const
	{
		return UnitContext;
	}

	void SetContextData(const IAnimNextInterface* InInterface, const UE::AnimNext::FContext& InInterfaceContext, bool& bInResult)
	{
		Interface = InInterface;
		InterfaceContext = &InInterfaceContext;
		ResultPtr = &bInResult;
	}

	void SetResult(bool bInResult) const
	{
		check(ResultPtr);
		*ResultPtr &= bInResult;
	}

	const IAnimNextInterface* GetInterface() const
	{
		check(Interface);
		return Interface;
	}
	
	virtual void Copy(const FRigVMExecuteContext* InOtherContext) override
	{
		Super::Copy(InOtherContext);

		const FAnimNextExecuteContext* OtherContext = (const FAnimNextExecuteContext*)InOtherContext; 
		InterfaceContext = OtherContext->InterfaceContext;
		Interface = OtherContext->Interface;
		ResultPtr = OtherContext->ResultPtr;
	}


private:
	const UE::AnimNext::FContext* InterfaceContext;
	const IAnimNextInterface* Interface;
	bool* ResultPtr;
	FRigUnitContext UnitContext;
};

USTRUCT(meta=(ExecuteContext="FAnimNextExecuteContext"))
struct FRigUnit_AnimNextBase : public FRigUnit
{
	GENERATED_BODY()
};

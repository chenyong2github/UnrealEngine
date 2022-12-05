// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMCore/RigVMStruct.h"
#include "AnimNextInterfaceUnitContext.h"
#include "AnimNextInterfaceExecuteContext.generated.h"

class IAnimNextInterface;

namespace UE::AnimNext::Interface
{
	struct FContext;
}

USTRUCT(BlueprintType)
struct FAnimNextInterfaceExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FAnimNextInterfaceExecuteContext()
		: FRigVMExecuteContext()
	, AnimNextInterfaceContext(nullptr)
	, Interface(nullptr)
	, ResultPtr(nullptr)
	, UnitContext()
	{
	}

	const UE::AnimNext::Interface::FContext& GetContext() const
	{
		check(AnimNextInterfaceContext);
		return *AnimNextInterfaceContext;
	}

	const FAnimNextInterfaceUnitContext& GetUnitContext() const
	{
		return UnitContext;
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

		const FAnimNextInterfaceExecuteContext* OtherContext = (const FAnimNextInterfaceExecuteContext*)InOtherContext; 
		AnimNextInterfaceContext = OtherContext->AnimNextInterfaceContext;
		Interface = OtherContext->Interface; 
		ResultPtr = OtherContext->ResultPtr; 
	}


private:
	const UE::AnimNext::Interface::FContext* AnimNextInterfaceContext;
	const IAnimNextInterface* Interface;
	bool* ResultPtr;
	FAnimNextInterfaceUnitContext UnitContext;
};

USTRUCT(meta=(ExecuteContext="FAnimNextInterfaceExecuteContext"))
struct FRigUnit_AnimNextInterfaceBase : public FRigVMStruct
{
	GENERATED_BODY()
};

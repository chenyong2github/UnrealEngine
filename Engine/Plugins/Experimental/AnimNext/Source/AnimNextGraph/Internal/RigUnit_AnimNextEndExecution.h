// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ExecuteContext.h"
#include "UnitContext.h"
#include "AnimNext_LODPose.h"
#include "RigUnit_AnimNextEndExecution.generated.h"

struct FRigUnitContext;

/** Event for writing back calculated results to external variables */
USTRUCT(meta = (DisplayName = "End Execute", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct ANIMNEXTGRAPH_API FRigUnit_AnimNextEndExecution : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	template<typename ValueType>
	static void SetResult(const FRigVMExecuteContext& InExecuteContext, const ValueType& InResult)
	{
		const FAnimNextExecuteContext& ExecuteContext = static_cast<const FAnimNextExecuteContext&>(InExecuteContext);
		ExecuteContext.GetContext().SetResult<ValueType>(InResult);
	}

public:
	// The execution result
	UPROPERTY(EditAnywhere, Transient, DisplayName = "End Execute", Category = "EndExecution", meta = (Input))
	FAnimNextExecuteContext ExecuteContext;
};

/** Event for writing back a calculated bool */
USTRUCT(meta = (DisplayName = "End Execute Bool", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct ANIMNEXTGRAPH_API FRigUnit_AnimNextEndExecution_Bool : public FRigUnit_AnimNextEndExecution
{
	GENERATED_BODY()
	
	FRigUnit_AnimNextEndExecution_Bool()
		: FRigUnit_AnimNextEndExecution()
		, Result(false)
	{}
	
	RIGVM_METHOD()
	virtual void Execute() override;

	virtual bool CanOnlyExistOnce() const override { return true; }
	
	UPROPERTY(EditAnywhere, Category = Result, meta = (Input))
	bool Result;
};

/** Event for writing back a calculated float */
USTRUCT(meta = (DisplayName = "End Execute Float", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct ANIMNEXTGRAPH_API FRigUnit_AnimNextEndExecution_Float : public FRigUnit_AnimNextEndExecution
{
	GENERATED_BODY()

	FRigUnit_AnimNextEndExecution_Float()
		: FRigUnit_AnimNextEndExecution()
		, Result(0.f)
	{}

	RIGVM_METHOD()
	virtual void Execute() override;

	virtual bool CanOnlyExistOnce() const override { return true; }

	UPROPERTY(EditAnywhere, Category = Result, meta = (Input))
	float Result;
};

/** Event for writing back a calculated pose */
USTRUCT(meta = (DisplayName = "End Execute LODPose", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct ANIMNEXTGRAPH_API FRigUnit_AnimNextEndExecution_LODPose : public FRigUnit_AnimNextEndExecution
{
	GENERATED_BODY()

	FRigUnit_AnimNextEndExecution_LODPose()
		: FRigUnit_AnimNextEndExecution()
	{}

	RIGVM_METHOD()
	virtual void Execute() override;

	virtual bool CanOnlyExistOnce() const override { return true; }

	UPROPERTY(EditAnywhere, Category = Result, meta = (Input))
	FAnimNextGraphLODPose Result;
};

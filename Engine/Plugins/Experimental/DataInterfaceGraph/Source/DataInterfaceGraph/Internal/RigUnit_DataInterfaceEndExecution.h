// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "DataInterfaceExecuteContext.h"
#include "DataInterfaceUnitContext.h"
#include "DataInterfaceTypes.h"
#include "RigUnit_DataInterfaceEndExecution.generated.h"

struct FRigUnitContext;

/** Event for writing back calculated results to external variables */
USTRUCT(meta = (DisplayName = "End Execute Data Interface", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct DATAINTERFACEGRAPH_API FRigUnit_DataInterfaceEndExecution : public FRigUnit_DataInterfaceBase
{
	GENERATED_BODY()

	template<typename ValueType>
	static void SetResult(const FRigVMExecuteContext& InExecuteContext, const ValueType& InResult)
	{
		const FDataInterfaceExecuteContext& DataInterfaceExecuteContext = static_cast<const FDataInterfaceExecuteContext&>(InExecuteContext);
		DataInterfaceExecuteContext.GetContext().SetResult<ValueType>(InResult);
	}

public:
	// The execution result
	UPROPERTY(EditAnywhere, Transient, DisplayName = "End Execute", Category = "EndExecution", meta = (Input))
	FDataInterfaceExecuteContext ExecuteContext;
};

/** Event for writing back a calculated bool */
USTRUCT(meta = (DisplayName = "End Execute Data Interface Bool", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct DATAINTERFACEGRAPH_API FRigUnit_DataInterfaceEndExecution_Bool : public FRigUnit_DataInterfaceEndExecution
{
	GENERATED_BODY()
	
	FRigUnit_DataInterfaceEndExecution_Bool()
		: FRigUnit_DataInterfaceEndExecution()
		, Result(false)
	{}
	
	RIGVM_METHOD()
	void Execute();
	
	UPROPERTY(EditAnywhere, Category = Result, meta = (Input))
	bool Result;
};

/** Event for writing back a calculated float */
USTRUCT(meta = (DisplayName = "End Execute Data Interface Float", Category = "Events", TitleColor = "1 0 0", NodeColor = "1 1 1"))
struct DATAINTERFACEGRAPH_API FRigUnit_DataInterfaceEndExecution_Float : public FRigUnit_DataInterfaceEndExecution
{
	GENERATED_BODY()

	FRigUnit_DataInterfaceEndExecution_Float()
		: FRigUnit_DataInterfaceEndExecution()
		, Result(0.f)
	{}

	RIGVM_METHOD()
	void Execute();

	UPROPERTY(EditAnywhere, Category = Result, meta = (Input))
	float Result;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosLog.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

#include "DataflowOverrideNode.generated.h"

struct FDataflowInput;
struct FDataflowOutput;

/**
* FDataflowOverrideNode
*		Base class for override nodes within the Dataflow graph. 
* 
*		Override Nodes allow to access to Override property on
*		the asset. They can read the values by the key.
*/
USTRUCT()
struct DATAFLOWCORE_API FDataflowOverrideNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()

	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowOverrideNode, "DataflowOverrideNode", "BaseClass", "")

public:
	FDataflowOverrideNode(const Dataflow::FNodeParameters& Param, FGuid InGuid = FGuid::NewGuid())
		: Super(Param,InGuid) 
	{
		RegisterInputConnection(&Key);
		RegisterInputConnection(&Default);
	}

	virtual ~FDataflowOverrideNode() { }

	bool ShouldInvalidate(FName InKey) const;

	FString GetDefaultValue(Dataflow::FContext& Context) const;

	FString GetValueFromAsset(Dataflow::FContext& Context, const UObject* InOwner) const;

	//
	// Evaluate
	//
	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const { ensure(false); };

public:
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (DataflowInput))
	FName Key = "Key";

	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (DataflowInput))
	FString Default = FString("0");
};



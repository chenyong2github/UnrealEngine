// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsTypeComponent.h"

#include "Containers/Array.h"
#include "Engine/EngineTypes.h"
#include "LearningArray.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsController.generated.h"

class ULearningAgentsType;

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsController : public ULearningAgentsTypeComponent
{
	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsController();
	ULearningAgentsController(FVTableHelper& Helper);
	virtual ~ULearningAgentsController();

public:

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetActions(const TArray<int32>& AgentIds);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeActions();

};

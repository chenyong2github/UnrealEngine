// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/4MLAgentElement.h"
#include "4MLTypes.h"
#include "4MLActuator.generated.h"


class AActor;
struct F4MLDescription;

UCLASS(Abstract)
class U4MLActuator : public U4MLAgentElement
{
	GENERATED_BODY()
public:
	U4MLActuator(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	virtual void Act(const float DeltaTime) {}
	virtual void DigestInputData(F4MLMemoryReader& ValueStream) {}

protected:
	mutable FCriticalSection ActionCS;
};
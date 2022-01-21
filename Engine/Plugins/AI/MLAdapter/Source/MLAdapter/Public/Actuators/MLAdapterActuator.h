// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Agents/MLAdapterAgentElement.h"
#include "MLAdapterTypes.h"
#include "MLAdapterActuator.generated.h"


class AActor;
struct FMLAdapterDescription;

UCLASS(Abstract, Blueprintable, EditInlineNew)
class MLADAPTER_API UMLAdapterActuator : public UMLAdapterAgentElement
{
	GENERATED_BODY()
public:
	UMLAdapterActuator(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	virtual void Act(const float DeltaTime) {}
	virtual void DigestInputData(FMLAdapterMemoryReader& ValueStream) {}

protected:
	mutable FCriticalSection ActionCS;
};
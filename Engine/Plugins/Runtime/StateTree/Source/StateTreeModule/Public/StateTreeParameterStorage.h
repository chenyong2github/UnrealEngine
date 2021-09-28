// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeParameterLayout.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeParameterStorage.generated.h"

USTRUCT()
struct STATETREEMODULE_API FStateTreeParameterStorage
{
	GENERATED_BODY()

public:
	void Reset();
	void SetLayout(const FStateTreeParameterLayout& NewLayout);
	void SetLayout(const FStateTreeVariableLayout& NewLayout);

	void MapVariables(FStateTreeParameterLayout& MappedLayout, const FStateTreeParameterLayout& ExpectedLayout) const;
	bool IsCompatible(const FStateTreeParameterLayout& ParameterLayout) const;

	uint32 GetParameterNum() const { return (uint32)Layout.Variables.Num(); }
	uint8* GetParameterPtr(uint32 Index, EStateTreeVariableType ExpectedType);
	const uint8* GetParameterPtr(uint32 Index, EStateTreeVariableType ExpectedType) const;

	// TODO: manual read/write API

protected:
	UPROPERTY(Transient)
	TArray<uint8> Memory;

	UPROPERTY()
	FStateTreeVariableLayout Layout;
};

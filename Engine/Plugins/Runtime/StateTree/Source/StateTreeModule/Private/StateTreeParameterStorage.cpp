// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeParameterStorage.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeInstance.h"
#include "StateTreeTypes.h"

void FStateTreeParameterStorage::Reset()
{
	Layout.Reset();
	Memory.Reset(0);
}

void FStateTreeParameterStorage::SetLayout(const FStateTreeParameterLayout& NewLayout)
{
	// Create partial variable map from parameer set binding
	Layout.Variables.Reset(NewLayout.Parameters.Num());
	for (const FStateTreeParameter& Param : NewLayout.Parameters)
	{
		FStateTreeVariableDesc& Desc = Layout.Variables.AddDefaulted_GetRef();
		Desc.Name = Param.Name;
		Desc.Type = Param.Variable.Type;
#if WITH_EDITORONLY_DATA
		Desc.BaseClass = Param.Variable.BaseClass;
		Desc.ID = FGuid();
#endif
	}

	const uint16 MemoryUsage = Layout.CalculateOffsetsAndMemoryUsage();
	Memory.SetNumZeroed(MemoryUsage);
}

void FStateTreeParameterStorage::SetLayout(const FStateTreeVariableLayout& NewLayout)
{
	Layout = NewLayout;
#if WITH_EDITORONLY_DATA
	// Cannot bind to storage with ID, make sure we don't accidentally carry these around.
	for (FStateTreeVariableDesc& Desc : Layout.Variables)
	{
		Desc.ID = FGuid();
	}
#endif
	uint16 MemoryUsage = Layout.CalculateOffsetsAndMemoryUsage();
	Memory.SetNumZeroed(MemoryUsage);
}

void FStateTreeParameterStorage::MapVariables(FStateTreeParameterLayout& MappedLayout, const FStateTreeParameterLayout& ExpectedLayout) const
{
	MappedLayout.Parameters.Reset(Layout.Variables.Num());

	// Optimize for the case that the bindings are sequentially equal in layout.
	int32 Index = 0;
	const int32 ExpectedParametersNum = ExpectedLayout.Parameters.Num();

	// Create binding for all parameters in storages parameter set, find matching binding from input.
	for (const FStateTreeVariableDesc& Desc : Layout.Variables)
	{
		FStateTreeParameter& Param = MappedLayout.Parameters.AddDefaulted_GetRef();
		Param.Name = Desc.Name;
		Param.Variable.Type = Desc.Type;
		Param.Variable.Handle = FStateTreeHandle::Invalid;

		// Try to find matching binding
		for (int32 j = 0; j < ExpectedParametersNum; j++)
		{
			const FStateTreeParameter& ExpectedParam = ExpectedLayout.Parameters[Index];
			Index = (Index + 1) < ExpectedParametersNum ? (Index + 1) : 0;	// Incement first, so that the counters advance together on success.
			if (ExpectedParam.Variable.Type == Param.Variable.Type && ExpectedParam.Name == Param.Name)
			{
				Param.Variable.Handle = ExpectedParam.Variable.Handle;
				break;
			}
		}
	}
}

bool FStateTreeParameterStorage::IsCompatible(const FStateTreeParameterLayout& ParameterLayout) const
{
	if (Layout.Variables.Num() != ParameterLayout.Parameters.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Layout.Variables.Num(); i++)
	{
		if (Layout.Variables[i].Type != ParameterLayout.Parameters[i].Variable.Type)
		{
			return false;
		}
	}
	return true;
}

uint8* FStateTreeParameterStorage::GetParameterPtr(uint32 Index, EStateTreeVariableType ExpectedType)
{
	if (Index >= (uint32)Layout.Variables.Num() || Layout.Variables[Index].Type != ExpectedType)
	{
		return nullptr;
	}
	return &Memory[Layout.Variables[Index].Offset];
}

const uint8* FStateTreeParameterStorage::GetParameterPtr(uint32 Index, EStateTreeVariableType ExpectedType) const
{
	if (Index >= (uint32)Layout.Variables.Num() || Layout.Variables[Index].Type != ExpectedType)
	{
		return nullptr;
	}
	return &Memory[Layout.Variables[Index].Offset];
}

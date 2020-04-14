// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraRendererProperties.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"

#if WITH_EDITORONLY_DATA
const TArray<FNiagaraVariable>& UNiagaraRendererProperties::GetBoundAttributes()
{
	CurrentBoundAttributes.Reset();

	for (const FNiagaraVariableAttributeBinding* AttributeBinding : AttributeBindings)
	{
		if (AttributeBinding->BoundVariable.IsValid())
		{
			CurrentBoundAttributes.Add(AttributeBinding->BoundVariable);
		}
		else if (AttributeBinding->DataSetVariable.IsValid())
		{
			CurrentBoundAttributes.Add(AttributeBinding->DataSetVariable);
		}
		else
		{
			CurrentBoundAttributes.Add(AttributeBinding->DefaultValueIfNonExistent);
		}
	}

	return CurrentBoundAttributes;
}
#endif

uint32 UNiagaraRendererProperties::ComputeMaxUsedComponents(const FNiagaraDataSet& DataSet) const
{
	enum BaseType
	{
		BaseType_Int,
		BaseType_Float,
		BaseType_Half,
		BaseType_NUM
	};

	uint32 MaxNumComponents = 0;

	TArray<int32, TInlineAllocator<32>> SeenOffsets[BaseType_NUM];
	uint32 NumComponents[BaseType_NUM] = { 0, 0 };
	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		const FNiagaraVariable& Var = Binding->DataSetVariable;

		int32 FloatOffset, IntOffset, HalfOffset;
		DataSet.GetVariableComponentOffsets(Var, FloatOffset, IntOffset, HalfOffset);

		int32 VarOffset, VarBaseSize;
		BaseType VarBaseType;
		if (FloatOffset != INDEX_NONE)
		{
			VarBaseType = BaseType_Float;
			VarOffset = FloatOffset;
			VarBaseSize = sizeof(float);
		}
		else if (IntOffset != INDEX_NONE)
		{
			VarBaseType = BaseType_Int;
			VarOffset = IntOffset;
			VarBaseSize = sizeof(int32);
		}
		else if (HalfOffset != INDEX_NONE)
		{
			VarBaseType = BaseType_Half;
			VarOffset = HalfOffset;
			VarBaseSize = sizeof(float) / 2;
		}
		else
		{
			continue;
		}

		if (!SeenOffsets[VarBaseType].Contains(VarOffset))
		{
			SeenOffsets[VarBaseType].Add(VarOffset);
			NumComponents[VarBaseType] += Var.GetSizeInBytes() / VarBaseSize;
			MaxNumComponents = FMath::Max(MaxNumComponents, NumComponents[VarBaseType]);
		}
	}

	return MaxNumComponents;
}

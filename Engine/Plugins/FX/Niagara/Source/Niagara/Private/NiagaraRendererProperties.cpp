// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraRendererProperties.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "Interfaces/ITargetPlatform.h"
#include "Styling/SlateIconFinder.h"

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

const FSlateBrush* UNiagaraRendererProperties::GetStackIcon() const
{
	return FSlateIconFinder::FindIconBrushForClass(GetClass());
}

#endif

uint32 UNiagaraRendererProperties::ComputeMaxUsedComponents(const FNiagaraDataSetCompiledData* CompiledDataSetData) const
{
	enum BaseType
	{
		BaseType_Int,
		BaseType_Float,
		BaseType_Half,
		BaseType_NUM
	};

	TArray<int32, TInlineAllocator<32>> SeenOffsets[BaseType_NUM];
	uint32 NumComponents[BaseType_NUM] = { 0 };

	auto AccumulateUniqueComponents = [&](BaseType Type, uint32 ComponentCount, int32 ComponentOffset)
	{
		if (!SeenOffsets[Type].Contains(ComponentOffset))
		{
			SeenOffsets[Type].Add(ComponentOffset);
			NumComponents[Type] += ComponentCount;
		}
	};

	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		const FNiagaraVariable& Var = Binding->DataSetVariable;

		const int32 VariableIndex = CompiledDataSetData->Variables.IndexOfByKey(Var);
		if ( VariableIndex != INDEX_NONE )
		{
			const FNiagaraVariableLayoutInfo& DataSetVarLayout = CompiledDataSetData->VariableLayouts[VariableIndex];

			if (const uint32 FloatCount = DataSetVarLayout.GetNumFloatComponents())
			{
				AccumulateUniqueComponents(BaseType_Float, FloatCount, DataSetVarLayout.FloatComponentStart);
			}

			if (const uint32 IntCount = DataSetVarLayout.GetNumInt32Components())
			{
				AccumulateUniqueComponents(BaseType_Int, IntCount, DataSetVarLayout.Int32ComponentStart);
			}

			if (const uint32 HalfCount = DataSetVarLayout.GetNumHalfComponents())
			{
				AccumulateUniqueComponents(BaseType_Half, HalfCount, DataSetVarLayout.HalfComponentStart);
			}
		}
	}

	uint32 MaxNumComponents = 0;

	for (uint32 ComponentCount : NumComponents)
	{
		MaxNumComponents = FMath::Max(MaxNumComponents, ComponentCount);
	}

	return MaxNumComponents;
}

bool UNiagaraRendererProperties::NeedsLoadForTargetPlatform(const class ITargetPlatform* TargetPlatform)const
{
	return bIsEnabled && Platforms.IsEnabledForPlatform(TargetPlatform->IniPlatformName());
}
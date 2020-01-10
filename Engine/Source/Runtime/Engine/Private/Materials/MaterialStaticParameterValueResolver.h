// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"

template <typename ParameterType, typename OutputType>
class TMaterialStaticParameterValueResolver
{
public:
	TMaterialStaticParameterValueResolver(UMaterialInterface::FStaticParamEvaluationContext& InEvalContext, OutputType& InValues, FGuid* InExpressionGuids)
	: EvalContext(InEvalContext)
	, OutValues(InValues)
	, OutExpressionGuids(InExpressionGuids)
	{
	}

	/**
	 * Tries to resolve a single parameter from the set of parameters in the originally provided evaluation context by obtaining it from a Function.  The parameter may be resolved immediately or queued for resolving later.
	 * @param ParamIndex The index of the parameter to try to resolve.
	 * @param ParameterInfo The info associated with the parameter to try to resolve.
	 * @param Function The function from which to try and obtain the value for the parameter.
	 */
	void AttemptResolve(int32 ParamIndex, const FMaterialParameterInfo& ParameterInfo, UMaterialFunctionInterface* Function)
	{
		if (!Function)
		{
			return;
		}

		if (ExtractValueFromFunction(ParamIndex, ParameterInfo, Function, nullptr))
		{
			EvalContext.MarkParameterResolved(ParamIndex, true);
			return;
		}

		FunctionMap.FindOrAdd(Function).Add(ParamIndex);
	}

	/**
	 * Resolves all parameters that we have previously attempted to resolve but weren't able to immedately resolve.
	 */
	void ResolveQueued()
	{
		for (auto& FunctionMapPair : FunctionMap)
		{
			UMaterialFunctionInterface* Function = FunctionMapPair.Key;
			TArray<int32, TInlineAllocator<16>>& ParameterIndicesForFunction = FunctionMapPair.Value;
			int32 PendingParametersForFunction = ParameterIndicesForFunction.Num();

			if (PendingParametersForFunction == 0)
				continue;

			if (UMaterialFunctionInterface* ParameterFunction = Function->GetBaseFunction())
			{
				struct FParameterAndOwner
				{
					ParameterType* Parameter;
					UMaterialFunctionInterface* Owner;
				};

				TArray<FParameterAndOwner, TInlineAllocator<16>> ParametersAndOwners;
				ParametersAndOwners.AddZeroed(PendingParametersForFunction);

				const UClass* TargetClass = ParameterType::StaticClass();

				auto GetExpressionParameterByNamePredicate =
					[this, &PendingParametersForFunction, &ParameterIndicesForFunction, &ParametersAndOwners, TargetClass](UMaterialFunctionInterface* InFunction) -> bool
				{
					for (UMaterialExpression* FunctionExpression : *InFunction->GetFunctionExpressions())
					{
						if (ParameterType* ExpressionParameter = (FunctionExpression && FunctionExpression->IsA(TargetClass)) ? (ParameterType*)FunctionExpression : nullptr)
						{
							for (int32 FunctionParameterIdx = 0; FunctionParameterIdx < ParameterIndicesForFunction.Num(); ++FunctionParameterIdx)
							{
								if (ParametersAndOwners[FunctionParameterIdx].Owner)
									continue;

								if (ExpressionParameter->ParameterName == EvalContext.GetParameterInfo(ParameterIndicesForFunction[FunctionParameterIdx])->Name)
								{
									ParametersAndOwners[FunctionParameterIdx] = {ExpressionParameter, InFunction};

									if (--PendingParametersForFunction == 0)
									{
										return false;
									}
								}
							}
						}
					}

					return true; // not found, continue iterating
				};

				if (ParameterFunction->IterateDependentFunctions(GetExpressionParameterByNamePredicate))
				{
					GetExpressionParameterByNamePredicate(ParameterFunction);
				}

				for (int32 FunctionParameterIdx = 0; FunctionParameterIdx < ParameterIndicesForFunction.Num(); ++FunctionParameterIdx)
				{
					const FParameterAndOwner& ParameterAndOwner = ParametersAndOwners[FunctionParameterIdx];
					if (!ParameterAndOwner.Owner)
						continue;

					int32 ParamIndex = ParameterIndicesForFunction[FunctionParameterIdx];
					const FMaterialParameterInfo* ParameterInfo = EvalContext.GetParameterInfo(ParamIndex);
					bool bIsOverride = ExtractValueFromFunction(ParamIndex, *ParameterInfo, ParameterAndOwner.Owner, ParameterAndOwner.Parameter);
					EvalContext.MarkParameterResolved(ParamIndex, bIsOverride);
				}
			}
		}
	}


private:
	/**
	 * Obtains a parameter value for a single parameter from the given Function.  Also accepts a fallback parameter for the default value to be used if the Function doesn't have any overrides for the parameter.
	 * @param ParamIndex The index of the parameter to try to resolve.
	 * @param ParameterInfo The info associated with the parameter to try to resolve.
	 * @param Function The function from which to try and obtain the value for the parameter.
	 * @param Parameter If non-null, the parameter from which to try and obtain the default value if no overrides exist for the parameter on the function.
	 * @return true if a *non-default* or override value was obtained for the parameter, if no value was obtained, or if we fell back to obtaining the default value, then false will be returned.
	 */
	bool ExtractValueFromFunction(int32 ParamIndex, const FMaterialParameterInfo& ParameterInfo, UMaterialFunctionInterface* Function, ParameterType* Parameter)
	{
		static_assert(TIsSame<ParameterType,void>::Value, "Must provide explicit specialization for value retrieval method when using TMaterialStaticParameterValueResolver");
		return false;
	}

	UMaterialInterface::FStaticParamEvaluationContext& EvalContext;
	OutputType& OutValues;
	FGuid* OutExpressionGuids;
	TSortedMap<UMaterialFunctionInterface*, TArray<int32, TInlineAllocator<16>>, TInlineAllocator<16>> FunctionMap;
};


template<>
inline bool TMaterialStaticParameterValueResolver<UMaterialExpressionStaticBoolParameter, TBitArray<>>::ExtractValueFromFunction(int32 ParamIndex, const FMaterialParameterInfo& ParameterInfo, UMaterialFunctionInterface* Function, UMaterialExpressionStaticBoolParameter* Parameter)
{
	bool bTempVal;
	if (Function->OverrideNamedStaticSwitchParameter(ParameterInfo, bTempVal, OutExpressionGuids[ParamIndex]))
	{
		OutValues[ParamIndex] = bTempVal;
		return true;
	}

	if (Parameter)
	{
		bTempVal = OutValues[ParamIndex];
		ensure(Parameter->IsNamedParameter(ParameterInfo, bTempVal, OutExpressionGuids[ParamIndex]));
		OutValues[ParamIndex] = bTempVal;
	}

	return false;
}

template<>
inline bool TMaterialStaticParameterValueResolver<UMaterialExpressionStaticComponentMaskParameter, TBitArray<>>::ExtractValueFromFunction(int32 ParamIndex, const FMaterialParameterInfo& ParameterInfo, UMaterialFunctionInterface* Function, UMaterialExpressionStaticComponentMaskParameter* Parameter)
{
	bool bTempR, bTempG, bTempB, bTempA;
	if (Function->OverrideNamedStaticComponentMaskParameter(ParameterInfo, bTempR, bTempG, bTempB, bTempA, OutExpressionGuids[ParamIndex]))
	{
		int32 ParamRGBAIndex = ParamIndex * 4;
		OutValues[ParamRGBAIndex + 0] = bTempR;
		OutValues[ParamRGBAIndex + 1] = bTempG;
		OutValues[ParamRGBAIndex + 2] = bTempB;
		OutValues[ParamRGBAIndex + 3] = bTempA;
		return true;
	}

	if (Parameter)
	{
		ensure(Parameter->IsNamedParameter(ParameterInfo, bTempR, bTempG, bTempB, bTempA, OutExpressionGuids[ParamIndex]));
		int32 ParamRGBAIndex = ParamIndex * 4;
		OutValues[ParamRGBAIndex + 0] = bTempR;
		OutValues[ParamRGBAIndex + 1] = bTempG;
		OutValues[ParamRGBAIndex + 2] = bTempB;
		OutValues[ParamRGBAIndex + 3] = bTempA;
	}

	return false;
}

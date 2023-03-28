// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode_RemapCurves.h"

#include "Animation/AnimInstanceProxy.h"

void FAnimNode_RemapCurves::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_RemapCurvesBase::Initialize_AnyThread(Context);

	const USkeleton* Skeleton = Context.AnimInstanceProxy->GetSkeleton();
	if (ensure(Skeleton))
	{
		const FSmartNameMapping* CurveNameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);

		CurveNameMapping->Iterate([this](const FSmartNameMappingIterator& Iterator)
		{
			if (FName CurveName; Iterator.GetName(CurveName))
			{
				CurveNameToUIDMap.Add(CurveName, Iterator.GetIndex());
			}
		});
	}
	
	CurveEvalResult.Reserve(GetCompiledAssignments().Num());
}

void FAnimNode_RemapCurves::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	
	using namespace CurveExpression::Evaluator;

	FPoseContext SourceData(Output);
	SourcePose.Evaluate(SourceData);
	
	CurveEvalResult.Reset();

	for (const TTuple<FName, FExpressionObject>& Assignment: GetCompiledAssignments())
	{
		if (const SmartName::UID_Type* UID = CurveNameToUIDMap.Find(Assignment.Key))
		{
			CurveEvalResult.Add({*UID, FEngine().Execute(Assignment.Value,
				[&CurveIDMap = CurveNameToUIDMap, &CurveValues = SourceData.Curve](const FName InName) -> TOptional<float>
				{
					if (const SmartName::UID_Type* SrcUID = CurveIDMap.Find(InName))
					{
						return CurveValues.Get(*SrcUID);
					}
					return {};
				}
			)});
		}
	}
	
	Output = SourceData;

	for( const TTuple<SmartName::UID_Type, float>& CurveValue: CurveEvalResult)
	{
		Output.Curve.Set(CurveValue.Key, CurveValue.Value);
	}
}

bool FAnimNode_RemapCurves::Serialize(FArchive& Ar)
{
	SerializeNode(Ar, this, StaticStruct());
	return true;
}

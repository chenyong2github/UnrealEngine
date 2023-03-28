// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveExpressionsDataAsset.h"

#if WITH_EDITOR
#include "ExpressionEvaluator.h"
#endif

#include "String/ParseLines.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CurveExpressionsDataAsset)


TArray<FCurveExpressionAssignment> FCurveExpressionList::GetAssignments() const
{
	TArray<FCurveExpressionAssignment> ExpressionAssignments;
	int32 LineIndex = 0;
	
	UE::String::ParseLines(AssignmentExpressions,
		[&ExpressionAssignments, &LineIndex](FStringView InLine)
	{
		int32 AssignmentPos;
		if (InLine.FindChar('=', AssignmentPos))
		{
			const FStringView AssignmentTarget = InLine.Left(AssignmentPos).TrimStartAndEnd();
			const FStringView SourceExpression = InLine.Mid(AssignmentPos + 1).TrimStartAndEnd();
			if (!AssignmentTarget.IsEmpty() && !SourceExpression.IsEmpty())
			{
				ExpressionAssignments.Add({LineIndex, FName(AssignmentTarget), SourceExpression});
			}
		}
		LineIndex++;
	});
	return ExpressionAssignments;
}

TArray<FCurveExpressionParsedAssignment> FCurveExpressionList::GetParsedAssignments() const
{
	using namespace CurveExpression::Evaluator;
	
	TArray<FCurveExpressionParsedAssignment> ParsedAssignments;
	FEngine ExpressionEngine;

	for (const FCurveExpressionAssignment& Assignment: GetAssignments())
	{
		TVariant<FExpressionObject, FParseError> Result = ExpressionEngine.Parse(Assignment.Expression);
		ParsedAssignments.Add({Assignment.LineIndex, Assignment.TargetName, MoveTemp(Result)});
	}
	
	return ParsedAssignments;
}

struct FCompiledExpressionScratchArea : public TThreadSingleton<FCompiledExpressionScratchArea>
{
	// Shadowed data copy for extraction from non-game threads.
	uint32 SerialNumber{UINT_MAX};
	TArray<FName> NamedConstants;
	TMap<FName, CurveExpression::Evaluator::FExpressionObject> ExpressionMap;
};


void UCurveExpressionsDataAsset::SynchronizeThreadLocalData() const
{
	FCompiledExpressionScratchArea& ThreadLocalData = FCompiledExpressionScratchArea::Get();
		
	const uint32 Serial = SerialNumber.load();
	if (ThreadLocalData.SerialNumber != Serial)
	{
		FScopeLock ScopeLock(&ExpressionMapWriteLock);

		ThreadLocalData.SerialNumber = Serial;
		ThreadLocalData.NamedConstants = NamedConstants;
		ThreadLocalData.ExpressionMap = ExpressionMap;
	}
}


const TArray<FName>& UCurveExpressionsDataAsset::GetCompiledExpressionConstants() const
{
	if (IsInGameThread())
	{
		return NamedConstants;
	}
	else
	{
		SynchronizeThreadLocalData();
		return FCompiledExpressionScratchArea::Get().NamedConstants;
	}
}


const TMap<FName, CurveExpression::Evaluator::FExpressionObject>& UCurveExpressionsDataAsset::GetCompiledExpressionMap() const
{
	if (IsInGameThread())
	{
		return ExpressionMap;
	}
	else
	{
		SynchronizeThreadLocalData();
		return FCompiledExpressionScratchArea::Get().ExpressionMap;
	}
}


void UCurveExpressionsDataAsset::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		// Make sure the expressions are compiled.
		CompileExpressions();
	}
#endif

	// Serialize the compiled map so we can read it in cooked builds.
	Ar << ExpressionMap;
}


#if WITH_EDITOR
void UCurveExpressionsDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FCurveExpressionList, AssignmentExpressions))
	{
		CompileExpressions();
	}
}


void UCurveExpressionsDataAsset::CompileExpressions()
{
	using namespace CurveExpression::Evaluator;
	
	FScopeLock ScopeLock(&ExpressionMapWriteLock);

	TSet<FName> ConstantNames;
	
	ExpressionMap.Reset();
	for (const FCurveExpressionParsedAssignment& Assignment: Expressions.GetParsedAssignments())
	{
		if (const FExpressionObject* Expression = Assignment.Result.TryGet<FExpressionObject>())
		{
			ConstantNames.Append(Expression->GetUsedConstants());
			ExpressionMap.Add(Assignment.TargetName, *Expression);
		}
	}
	
	NamedConstants = ConstantNames.Array();
	SerialNumber += 1;
}

#endif

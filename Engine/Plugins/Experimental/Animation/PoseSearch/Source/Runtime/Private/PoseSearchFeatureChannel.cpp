// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Animation/Skeleton.h"

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// class ICostBreakDownData
static inline float ArraySum(TConstArrayView<float> View, int32 StartIndex, int32 Offset)
{
	float Sum = 0.f;
	const int32 EndIndex = StartIndex + Offset;
	for (int i = StartIndex; i < EndIndex; ++i)
	{
		Sum += View[i];
	}
	return Sum;
}

void ICostBreakDownData::AddEntireBreakDownSection(const FText& Label, const UPoseSearchSchema* Schema, int32 DataOffset, int32 Cardinality)
{
	BeginBreakDownSection(Label);

	const int32 Count = Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (IsCostVectorFromSchema(i, Schema))
		{
			const float CostBreakdown = ArraySum(GetCostVector(i, Schema), DataOffset, Cardinality);
			SetCostBreakDown(CostBreakdown, i, Schema);
		}
	}

	EndBreakDownSection(Label);
}

//////////////////////////////////////////////////////////////////////////
// FFeatureVectorHelper
void FFeatureVectorHelper::EncodeQuat(TArrayView<float> Values, int32& DataOffset, const FQuat& Quat)
{
	const FVector X = Quat.GetAxisX();
	const FVector Y = Quat.GetAxisY();

	Values[DataOffset + 0] = X.X;
	Values[DataOffset + 1] = X.Y;
	Values[DataOffset + 2] = X.Z;
	Values[DataOffset + 3] = Y.X;
	Values[DataOffset + 4] = Y.Y;
	Values[DataOffset + 5] = Y.Z;

	DataOffset += EncodeQuatCardinality;
}

void FFeatureVectorHelper::EncodeQuat(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	FQuat Quat = DecodeQuatInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Quat = FQuat::Slerp(Quat, DecodeQuatInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Quat = FQuat::Slerp(Quat, DecodeQuatInternal(NextValues, DataOffset), LerpValue);
		}
	}

	// @todo: do we need to add options for cubic interpolation?
	EncodeQuat(Values, DataOffset, Quat);
}

FQuat FFeatureVectorHelper::DecodeQuat(TConstArrayView<float> Values, int32& DataOffset)
{
	const FQuat Quat = DecodeQuatInternal(Values, DataOffset);
	DataOffset += EncodeQuatCardinality;
	return Quat;
}

FQuat FFeatureVectorHelper::DecodeQuatInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	const FVector X(Values[DataOffset + 0], Values[DataOffset + 1], Values[DataOffset + 2]);
	const FVector Y(Values[DataOffset + 3], Values[DataOffset + 4], Values[DataOffset + 5]);
	const FVector Z = FVector::CrossProduct(X, Y);

	FMatrix M(FMatrix::Identity);
	M.SetColumn(0, X);
	M.SetColumn(1, Y);
	M.SetColumn(2, Z);

	return FQuat(M);
}

void FFeatureVectorHelper::EncodeVector(TArrayView<float> Values, int32& DataOffset, const FVector& Vector)
{
	Values[DataOffset + 0] = Vector.X;
	Values[DataOffset + 1] = Vector.Y;
	Values[DataOffset + 2] = Vector.Z;
	DataOffset += EncodeVectorCardinality;
}

void FFeatureVectorHelper::EncodeVector(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue, bool bNormalize)
{
	FVector Vector = DecodeVectorInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Vector = FMath::Lerp(Vector, DecodeVectorInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Vector = FMath::Lerp(Vector, DecodeVectorInternal(NextValues, DataOffset), LerpValue);
		}
	}

	// @todo: do we need to add options for cubic interpolation?
	if (bNormalize)
	{
		Vector = Vector.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
	}

	EncodeVector(Values, DataOffset, Vector);
}

FVector FFeatureVectorHelper::DecodeVector(TConstArrayView<float> Values, int32& DataOffset)
{
	const FVector Vector = DecodeVectorInternal(Values, DataOffset);
	DataOffset += EncodeVectorCardinality;
	return Vector;
}

FVector FFeatureVectorHelper::DecodeVectorInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return FVector(Values[DataOffset + 0], Values[DataOffset + 1], Values[DataOffset + 2]);
}

void FFeatureVectorHelper::EncodeVector2D(TArrayView<float> Values, int32& DataOffset, const FVector2D& Vector2D)
{
	Values[DataOffset + 0] = Vector2D.X;
	Values[DataOffset + 1] = Vector2D.Y;
	DataOffset += EncodeVector2DCardinality;
}

void FFeatureVectorHelper::EncodeVector2D(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	FVector2D Vector2D = DecodeVector2DInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Vector2D = FMath::Lerp(Vector2D, DecodeVector2DInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Vector2D = FMath::Lerp(Vector2D, DecodeVector2DInternal(NextValues, DataOffset), LerpValue);
		}
	}

	// @todo: do we need to add options for cubic interpolation?
	EncodeVector2D(Values, DataOffset, Vector2D);
}

FVector2D FFeatureVectorHelper::DecodeVector2D(TConstArrayView<float> Values, int32& DataOffset)
{
	const FVector2D Vector2D = DecodeVector2DInternal(Values, DataOffset);
	DataOffset += EncodeVector2DCardinality;
	return Vector2D;
}

FVector2D FFeatureVectorHelper::DecodeVector2DInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return FVector2D(Values[DataOffset + 0], Values[DataOffset + 1]);
}

void FFeatureVectorHelper::EncodeFloat(TArrayView<float> Values, int32& DataOffset, const float Value)
{
	Values[DataOffset + 0] = Value;
	DataOffset += EncodeFloatCardinality;
}

void FFeatureVectorHelper::EncodeFloat(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	float Value = DecodeFloatInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Value = FMath::Lerp(Value, DecodeFloatInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Value = FMath::Lerp(Value, DecodeFloatInternal(NextValues, DataOffset), LerpValue);
		}
	}

	// @todo: do we need to add options for cubic interpolation?
	EncodeFloat(Values, DataOffset, Value);
}

float FFeatureVectorHelper::DecodeFloat(TConstArrayView<float> Values, int32& DataOffset)
{
	const float Value = DecodeFloatInternal(Values, DataOffset);
	DataOffset += EncodeFloatCardinality;
	return Value;
}

float FFeatureVectorHelper::DecodeFloatInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return Values[DataOffset];
}

} // namespace UE::PoseSearch

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel
#if WITH_EDITOR
void UPoseSearchFeatureChannel::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	FeatureChannelLayoutSet.Add(GetName(), UE::PoseSearch::FKeyBuilder(this).Finalize(), ChannelDataOffset, ChannelCardinality);
}

void UPoseSearchFeatureChannel::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	CostBreakDownData.AddEntireBreakDownSection(FText::FromString(GetName()), Schema, ChannelDataOffset, ChannelCardinality);
}
#endif // WITH_EDITOR

class USkeleton* UPoseSearchFeatureChannel::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;

	UObject* Outer = GetOuter();
	while (Outer != nullptr)
	{
		if (const UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(Outer))
		{
			return Schema->Skeleton;
		}
		Outer = Outer->GetOuter();
	}
	return nullptr;
}
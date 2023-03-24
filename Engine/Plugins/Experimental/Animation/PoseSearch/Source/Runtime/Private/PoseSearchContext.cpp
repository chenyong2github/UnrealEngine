// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchContext.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_Position.h"

namespace UE::PoseSearch
{
	
#if ENABLE_DRAW_DEBUG
//////////////////////////////////////////////////////////////////////////
// FDebugDrawParams
FDebugDrawParams::FDebugDrawParams(FAnimInstanceProxy* InAnimInstanceProxy, const UPoseSearchDatabase* InDatabase, EDebugDrawFlags InFlags)
: AnimInstanceProxy(InAnimInstanceProxy)
, World(nullptr)
, Mesh(nullptr)
, Database(InDatabase)
, Flags(InFlags)
{
}

FDebugDrawParams::FDebugDrawParams(const UWorld* InWorld, const USkinnedMeshComponent* InMesh, const UPoseSearchDatabase* InDatabase, EDebugDrawFlags InFlags)
: AnimInstanceProxy(nullptr)
, World(InWorld)
, Mesh(InMesh)
, Database(InDatabase)
, Flags(InFlags)
{
}

bool FDebugDrawParams::CanDraw() const
{
	return (AnimInstanceProxy || World) && Database && Database->Schema && Database->Schema->IsValid();
}

const FPoseSearchIndex* FDebugDrawParams::GetSearchIndex() const
{
	return Database ? &Database->GetSearchIndex() : nullptr;
}

const UPoseSearchSchema* FDebugDrawParams::GetSchema() const
{
	return Database ? Database->Schema : nullptr;
}

FVector FDebugDrawParams::ExtractPosition(TConstArrayView<float> PoseVector, const UPoseSearchFeatureChannel_Position* Position) const
{
	check(Position);
	const FVector BonePosition = FFeatureVectorHelper::DecodeVector(PoseVector, Position->GetChannelDataOffset(), Position->ComponentStripping);
	const FVector WorldBonePosition = GetRootTransform().TransformPosition(BonePosition);
	return WorldBonePosition;
}

FVector FDebugDrawParams::ExtractPosition(TConstArrayView<float> PoseVector, float SampleTimeOffset, int8 SchemaBoneIdx, EPermutationTimeType PermutationTimeType) const
{
	// we don't wanna ask for a SchemaOriginBoneIdx in the future or past
	check(PermutationTimeType != EPermutationTimeType::UsePermutationTime);
	if (const UPoseSearchSchema* Schema = GetSchema())
	{
		// looking for a UPoseSearchFeatureChannel_Position that matches the TimeOffset and SchemaBoneIdx,
		// with SchemaOriginBoneIdx to be the root bone and the appropriate PermutationTimeType 
		if (const UPoseSearchFeatureChannel_Position* FoundPosition = static_cast<const UPoseSearchFeatureChannel_Position*>(
				Schema->FindChannel([SampleTimeOffset, SchemaBoneIdx, PermutationTimeType, Schema](const UPoseSearchFeatureChannel* Channel) -> const UPoseSearchFeatureChannel_Position*
			{
				if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(Channel))
				{
					if (Position->SchemaBoneIdx == SchemaBoneIdx &&
						Position->SampleTimeOffset == SampleTimeOffset &&
						Position->PermutationTimeType == PermutationTimeType &&
						Schema->IsRootBone(Position->SchemaOriginBoneIdx))
					{
						return Position;
					}
				}
		return nullptr;
			})))
		{
			return ExtractPosition(PoseVector, FoundPosition);
		}

		if (Mesh && SchemaBoneIdx >= 0)
		{
			return Mesh->GetSocketTransform(Schema->BoneReferences[SchemaBoneIdx].BoneName).GetTranslation();
		}
	}
	return GetRootTransform().GetTranslation();
}

const FTransform& FDebugDrawParams::GetRootTransform() const
{
	if (AnimInstanceProxy)
	{
		return AnimInstanceProxy->GetComponentTransform();
	}
	
	if (Mesh)
	{
		return Mesh->GetComponentTransform();
	}

	return FTransform::Identity;
}

void FDebugDrawParams::DrawLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, float Thickness) const
{
	if (Color.A > 0)
	{
		if (AnimInstanceProxy)
		{
			AnimInstanceProxy->AnimDrawDebugLine(LineStart, LineEnd, Color, false, 0.f, Thickness, SDPG_Foreground);
		}
		else if (World)
		{
			DrawDebugLine(World, LineStart, LineEnd, Color, false, 0.f, SDPG_Foreground, Thickness);
		}
	}
}

void FDebugDrawParams::DrawPoint(const FVector& Position, const FColor& Color, float Thickness) const
{
	if (Color.A > 0)
	{
		if (AnimInstanceProxy)
		{
			AnimInstanceProxy->AnimDrawDebugPoint(Position, Thickness, Color, false, 0.f, SDPG_Foreground);
		}
		else if (World)
		{
			DrawDebugPoint(World, Position, Thickness, Color, false, 0.f, SDPG_Foreground);
		}
	}
}

void FDebugDrawParams::DrawCircle(const FMatrix& TransformMatrix, float Radius, int32 Segments, const FColor& Color, float Thickness) const
{
	if (Color.A > 0)
	{
		if (AnimInstanceProxy)
		{
			AnimInstanceProxy->AnimDrawDebugCircle(TransformMatrix.GetOrigin(), Radius, Segments, Color, TransformMatrix.GetScaledAxis(EAxis::X), false, 0.f, SDPG_Foreground, Thickness);
		}
		else if (World)
		{
			// @todo: use the DrawDebugCircle API with the up vector to communize with the AnimInstanceProxy call
			DrawDebugCircle(World, TransformMatrix, Radius, Segments, Color, false, 0.f, SDPG_Foreground, Thickness);
		}
	}
}

void FDebugDrawParams::DrawCentripetalCatmullRomSpline(TConstArrayView<FVector> Points, TConstArrayView<FColor> Colors, float Alpha, int32 NumSamplesPerSegment, float Thickness) const
{
	const int32 NumPoints = Points.Num();
	const int32 NumColors = Colors.Num();
	if (NumPoints > 1)
	{
		auto GetT = [](float T, float Alpha, const FVector& P0, const FVector& P1)
		{
			const FVector P1P0 = P1 - P0;
			const float Dot = P1P0 | P1P0;
			const float Pow = FMath::Pow(Dot, Alpha * .5f);
			return Pow + T;
		};

		auto LerpColor = [](FColor A, FColor B, float T) -> FColor
		{
			return FColor(
				FMath::RoundToInt(float(A.R) * (1.f - T) + float(B.R) * T),
				FMath::RoundToInt(float(A.G) * (1.f - T) + float(B.G) * T),
				FMath::RoundToInt(float(A.B) * (1.f - T) + float(B.B) * T),
				FMath::RoundToInt(float(A.A) * (1.f - T) + float(B.A) * T));
		};

		FVector PrevPoint = Points[0];
		for (int i = 0; i < NumPoints - 1; ++i)
		{
			const FVector& P0 = Points[FMath::Max(i - 1, 0)];
			const FVector& P1 = Points[i];
			const FVector& P2 = Points[i + 1];
			const FVector& P3 = Points[FMath::Min(i + 2, NumPoints - 1)];

			const float T0 = 0.0f;
			const float T1 = GetT(T0, Alpha, P0, P1);
			const float T2 = GetT(T1, Alpha, P1, P2);
			const float T3 = GetT(T2, Alpha, P2, P3);

			const float T1T0 = T1 - T0;
			const float T2T1 = T2 - T1;
			const float T3T2 = T3 - T2;
			const float T2T0 = T2 - T0;
			const float T3T1 = T3 - T1;

			const bool bIsNearlyZeroT1T0 = FMath::IsNearlyZero(T1T0, UE_KINDA_SMALL_NUMBER);
			const bool bIsNearlyZeroT2T1 = FMath::IsNearlyZero(T2T1, UE_KINDA_SMALL_NUMBER);
			const bool bIsNearlyZeroT3T2 = FMath::IsNearlyZero(T3T2, UE_KINDA_SMALL_NUMBER);
			const bool bIsNearlyZeroT2T0 = FMath::IsNearlyZero(T2T0, UE_KINDA_SMALL_NUMBER);
			const bool bIsNearlyZeroT3T1 = FMath::IsNearlyZero(T3T1, UE_KINDA_SMALL_NUMBER);

			const FColor Color1 = Colors[FMath::Min(i, NumColors - 1)];
			const FColor Color2 = Colors[FMath::Min(i + 1, NumColors - 1)];

			for (int SampleIndex = 1; SampleIndex < NumSamplesPerSegment; ++SampleIndex)
			{
				const float ParametricDistance = float(SampleIndex) / float(NumSamplesPerSegment - 1);

				const float T = FMath::Lerp(T1, T2, ParametricDistance);

				const FVector A1 = bIsNearlyZeroT1T0 ? P0 : (T1 - T) / T1T0 * P0 + (T - T0) / T1T0 * P1;
				const FVector A2 = bIsNearlyZeroT2T1 ? P1 : (T2 - T) / T2T1 * P1 + (T - T1) / T2T1 * P2;
				const FVector A3 = bIsNearlyZeroT3T2 ? P2 : (T3 - T) / T3T2 * P2 + (T - T2) / T3T2 * P3;
				const FVector B1 = bIsNearlyZeroT2T0 ? A1 : (T2 - T) / T2T0 * A1 + (T - T0) / T2T0 * A2;
				const FVector B2 = bIsNearlyZeroT3T1 ? A2 : (T3 - T) / T3T1 * A2 + (T - T1) / T3T1 * A3;
				const FVector Point = bIsNearlyZeroT2T1 ? B1 : (T2 - T) / T2T1 * B1 + (T - T1) / T2T1 * B2;

				DrawLine(PrevPoint, Point, LerpColor(Color1, Color2, ParametricDistance));

				PrevPoint = Point;
			}
		}
	}
}

void FDebugDrawParams::DrawFeatureVector(TConstArrayView<float> PoseVector)
{
	if (CanDraw())
	{
		const UPoseSearchSchema* Schema = GetSchema();
		check(Schema);

		if (PoseVector.Num() == Schema->SchemaCardinality)
		{
			for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : Schema->GetChannels())
			{
				ChannelPtr->DebugDraw(*this, PoseVector);
			}
		}
	}
}

void FDebugDrawParams::DrawFeatureVector(int32 PoseIdx)
{
	// if we're editing the schema while in PIE with Rewind Debugger active, PoseIdx could be out of bound / stale
	if (CanDraw() && PoseIdx >= 0 && PoseIdx < GetSearchIndex()->GetNumPoses())
	{
		DrawFeatureVector(GetSearchIndex()->GetPoseValues(PoseIdx));
	}
}
#endif // ENABLE_DRAW_DEBUG

//////////////////////////////////////////////////////////////////////////
// FSearchContext
FQuat FSearchContext::GetSampleRotation(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot, EPermutationTimeType PermutationTimeType)
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	UPoseSearchFeatureChannel::GetPermutationTimeOffsets(PermutationTimeType, DesiredPermutationTimeOffset, PermutationSampleTimeOffset, PermutationOriginTimeOffset);

	// @todo: add support for SchemaSampleBoneIdx
	if (SchemaOriginBoneIdx != RootSchemaBoneIdx)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FSearchContext::GetSampleRotation: support for non root origin bones not implemented (bone: '%s', schema: '%s'"),
			*Schema->BoneReferences[SchemaOriginBoneIdx].BoneName.ToString(), *GetNameSafe(Schema));
	}

	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;

	// @todo: add support for OriginTime != 0 (like in GetSamplePosition and GetSampleVelocity)
	if (OriginTime != 0.f)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FSearchContext::GetSampleRotation: support for OriginTime != 0 not implemented (bone: '%s', schema: '%s'"),
			SchemaOriginBoneIdx >= 0 ? *Schema->BoneReferences[SchemaOriginBoneIdx].BoneName.ToString() : TEXT("RootBone"), *GetNameSafe(Schema));
	}

	return GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetRotation();
}

FVector FSearchContext::GetSamplePosition(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot, EPermutationTimeType PermutationTimeType)
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	UPoseSearchFeatureChannel::GetPermutationTimeOffsets(PermutationTimeType, DesiredPermutationTimeOffset, PermutationSampleTimeOffset, PermutationOriginTimeOffset);

	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;
	return GetSamplePositionInternal(SampleTime, OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);
}

FVector FSearchContext::GetSampleVelocity(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseCharacterSpaceVelocities, bool bUseHistoryRoot, EPermutationTimeType PermutationTimeType)
{
	float PermutationSampleTimeOffset = 0.f;
	float PermutationOriginTimeOffset = 0.f;
	UPoseSearchFeatureChannel::GetPermutationTimeOffsets(PermutationTimeType, DesiredPermutationTimeOffset, PermutationSampleTimeOffset, PermutationOriginTimeOffset);

	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;
	const float FiniteDelta = History ? History->GetSampleTimeInterval() : 1 / 60.0f;
	check(FiniteDelta > UE_KINDA_SMALL_NUMBER);

	// calculating the Position in component space for the bone indexed by SchemaSampleBoneIdx
	const FVector PreviousTranslation = GetSamplePositionInternal(SampleTime - FiniteDelta, bUseCharacterSpaceVelocities ? OriginTime - FiniteDelta : OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);
	const FVector CurrentTranslation = GetSamplePositionInternal(SampleTime, OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);

	const FVector LinearVelocity = (CurrentTranslation - PreviousTranslation) / FiniteDelta;
	return LinearVelocity;
}

FTransform FSearchContext::GetTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx, bool bUseHistoryRoot)
{
	// collecting the RootTransform from the IPoseHistory
	FTransform RootTransform = FTransform::Identity;
	if (bUseHistoryRoot)
	{
		check(History);
		History->GetRootTransformAtTime(SampleTime, RootTransform);
	}
	else
	{
		check(Trajectory);
		const FTrajectorySample TrajectorySample = Trajectory->GetSampleAtTime(SampleTime);
		RootTransform = TrajectorySample.Transform;
	}

	const FBoneIndexType BoneIndexType = Schema->GetBoneIndexType(SchemaBoneIdx);
	if (BoneIndexType != RootBoneIndexType)
	{
		const FTransform BoneTransform = GetComponentSpaceTransform(SampleTime, Schema, SchemaBoneIdx);
		return BoneTransform * RootTransform;
	}

	return RootTransform;
}

FTransform FSearchContext::GetComponentSpaceTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx)
{
	check(Schema);

	const FBoneIndexType BoneIndexType = Schema->GetBoneIndexType(SchemaBoneIdx);
	if (BoneIndexType != RootBoneIndexType)
	{
		if (const FCachedTransform<FTransform>* CachedTransform = CachedTransforms.Find(SampleTime, BoneIndexType))
		{
			return CachedTransform->Transform;
		}
	
		// collecting the local bone transforms from the IPoseHistory
		check(History);
		FTransform BoneComponentSpaceTransform;
		if (!History->GetComponentSpaceTransformAtTime(SampleTime, BoneIndexType, BoneComponentSpaceTransform))
		{
			FName BoneName;
			if (const USkeleton* Skeleton = Schema->Skeleton)
			{
				BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneIndexType);
			}

			UE_LOG(LogPoseSearch, Warning, TEXT("FSearchContext::GetComponentSpaceTransform - Couldn't find BoneIndexType %d (%s) requested by %s"), BoneIndexType, *BoneName.ToString(), *Schema->GetName());
		}

		CachedTransforms.Add(SampleTime, BoneIndexType, BoneComponentSpaceTransform);
		return BoneComponentSpaceTransform;
	}

	return FTransform::Identity;
}

FVector FSearchContext::GetSamplePositionInternal(float SampleTime, float OriginTime, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot)
{
	if (SampleTime == OriginTime)
	{
		if (Schema->IsRootBone(SchemaOriginBoneIdx))
		{
			return GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetTranslation();
		}

		const FVector SampleBonePosition = GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetTranslation();
		const FVector OriginBonePosition = GetComponentSpaceTransform(OriginTime, Schema, SchemaOriginBoneIdx).GetTranslation();
		return SampleBonePosition - OriginBonePosition;
	}

	const FTransform RootBoneTransform = GetTransform(OriginTime, Schema, RootSchemaBoneIdx, bUseHistoryRoot);
	const FTransform SampleBoneTransform = GetTransform(SampleTime, Schema, SchemaSampleBoneIdx, bUseHistoryRoot);
	if (Schema->IsRootBone(SchemaOriginBoneIdx))
	{
		return RootBoneTransform.InverseTransformPosition(SampleBoneTransform.GetTranslation());
	}

	const FTransform OriginBoneTransform = GetTransform(OriginTime, Schema, SchemaOriginBoneIdx, bUseHistoryRoot);
	const FVector DeltaBoneTranslation = SampleBoneTransform.GetTranslation() - OriginBoneTransform.GetTranslation();
	return RootBoneTransform.InverseTransformVector(DeltaBoneTranslation);
}

void FSearchContext::ClearCachedEntries()
{
	CachedTransforms.Reset();
}

void FSearchContext::ResetCurrentBestCost()
{
	CurrentBestTotalCost = MAX_flt;
}

void FSearchContext::UpdateCurrentBestCost(const FPoseSearchCost& PoseSearchCost)
{
	check(PoseSearchCost.IsValid());

	if (PoseSearchCost.GetTotalCost() < CurrentBestTotalCost)
	{
		CurrentBestTotalCost = PoseSearchCost.GetTotalCost();
	};
}

const FPoseSearchFeatureVectorBuilder* FSearchContext::GetCachedQuery(const UPoseSearchSchema* Schema) const
{
	const FPoseSearchFeatureVectorBuilder* CachedQuery = CachedQueries.FindByPredicate([Schema](const FPoseSearchFeatureVectorBuilder& CachedQuery)
	{
		return CachedQuery.GetSchema() == Schema;
	});

	if (CachedQuery)
	{
		return CachedQuery;
	}
	return nullptr;
}

void FSearchContext::GetOrBuildQuery(const UPoseSearchSchema* Schema, FPoseSearchFeatureVectorBuilder& FeatureVectorBuilder)
{
	check(Schema && Schema->IsValid());
	const FPoseSearchFeatureVectorBuilder* CachedFeatureVectorBuilder = GetCachedQuery(Schema);
	if (CachedFeatureVectorBuilder)
	{
		FeatureVectorBuilder = *CachedFeatureVectorBuilder;
	}
	else
	{
		FPoseSearchFeatureVectorBuilder& NewCachedQuery = CachedQueries[CachedQueries.AddDefaulted()];
		Schema->BuildQuery(*this, NewCachedQuery);
		FeatureVectorBuilder = NewCachedQuery;
	}
}

bool FSearchContext::IsCurrentResultFromDatabase(const UPoseSearchDatabase* Database) const
{
	return CurrentResult.IsValid() && CurrentResult.Database == Database;
}

TConstArrayView<float> FSearchContext::GetCurrentResultPrevPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.PrevPoseIdx);
}

TConstArrayView<float> FSearchContext::GetCurrentResultPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.PoseIdx);
}

TConstArrayView<float> FSearchContext::GetCurrentResultNextPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.NextPoseIdx);
}

FTransform MirrorTransform(const FTransform& InTransform, EAxis::Type MirrorAxis, const FQuat& ReferenceRotation)
{
	const FVector T = FAnimationRuntime::MirrorVector(InTransform.GetTranslation(), MirrorAxis);
	const FQuat Q = FAnimationRuntime::MirrorQuat(InTransform.GetRotation(), MirrorAxis);
	const FQuat QR = Q * FAnimationRuntime::MirrorQuat(ReferenceRotation, MirrorAxis).Inverse() * ReferenceRotation;
	const FTransform Result = FTransform(QR, T, InTransform.GetScale3D());
	return Result;
}

} // namespace UE::PoseSearch
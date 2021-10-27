// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineLogs.h"
#include "EngineUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "PlatformInfo.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "GPUSkinCache.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshModel.h"
#include "MeshUtilities.h"
#endif // WITH_EDITOR

int32 GStripSkeletalMeshLodsDuringCooking = 0;
static FAutoConsoleVariableRef CVarStripSkeletalMeshLodsBelowMinLod(
	TEXT("r.SkeletalMesh.StripMinLodDataDuringCooking"),
	GStripSkeletalMeshLodsDuringCooking,
	TEXT("If set will strip skeletal mesh LODs under the minimum renderable LOD for the target platform during cooking.")
);

extern int32 GForceRecomputeTangents;
extern int32 GSkinCacheRecomputeTangents;

namespace
{
	struct FReverseOrderBitArraysBySetBits
	{
		FORCEINLINE bool operator()(const TBitArray<>& Lhs, const TBitArray<>& Rhs) const
		{
			//sort by length
			if (Lhs.Num() != Rhs.Num())
			{
				return Lhs.Num() > Rhs.Num();
			}

			uint32 NumWords = FMath::DivideAndRoundUp(Lhs.Num(), NumBitsPerDWORD);
			const uint32* Data0 = Lhs.GetData();
			const uint32* Data1 = Rhs.GetData();

			//sort by num bits active
			int32 Count0 = 0, Count1 = 0;
			for (uint32 i = 0; i < NumWords; i++)
			{
				Count0 += FPlatformMath::CountBits(Data0[i]);
				Count1 += FPlatformMath::CountBits(Data1[i]);
			}

			if (Count0 != Count1)
			{
				return Count0 > Count1;
			}

			//sort by big-num value
			for (uint32 i = NumWords - 1; i != ~0u; i--)
			{
				if (Data0[i] != Data1[i])
				{
					return Data0[i] > Data1[i];
				}
			}
			return false;
		}
	};
}

static bool IsGPUSkinCacheAvailable(const ITargetPlatform& TargetPlatform)
{
	TArray<FName> TargetedShaderFormats;
	TargetPlatform.GetAllTargetedShaderFormats(TargetedShaderFormats);
	for (int32 FormatIndex = 0; FormatIndex < TargetedShaderFormats.Num(); ++FormatIndex)
	{
		const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(TargetedShaderFormats[FormatIndex]);
		if (IsGPUSkinCacheAvailable(LegacyShaderPlatform))
		{
			return true;
		}
	}
	return false;
}

// Serialization.
FArchive& operator<<(FArchive& Ar, FSkelMeshRenderSection& S)
{
	const uint8 DuplicatedVertices = 1;
	
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FRecomputeTangentCustomVersion::GUID);

	// DuplicatedVerticesBuffer is used only for SkinCache and Editor features which is SM5 only
	uint8 ClassDataStripFlags = 0;
	if (Ar.IsCooking() && 
		!(Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DeferredRendering) || IsGPUSkinCacheAvailable(*Ar.CookingTarget())))
	{
		ClassDataStripFlags |= DuplicatedVertices;
	}

	// When data is cooked for server platform some of the
	// variables are not serialized so that they're always
	// set to their initial values (for safety)
	FStripDataFlags StripFlags(Ar, ClassDataStripFlags);

	Ar << S.MaterialIndex;
	Ar << S.BaseIndex;
	Ar << S.NumTriangles;
	Ar << S.bRecomputeTangent;
	if (Ar.CustomVer(FRecomputeTangentCustomVersion::GUID) >= FRecomputeTangentCustomVersion::RecomputeTangentVertexColorMask)
	{
		Ar << S.RecomputeTangentsVertexMaskChannel;
	}
	else
	{
		// Our default is not to use vertex color as mask
		S.RecomputeTangentsVertexMaskChannel = ESkinVertexColorChannel::None;
	}
	Ar << S.bCastShadow;
	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkelMeshSectionVisibleInRayTracingFlagAdded)
	{
		Ar << S.bVisibleInRayTracing;
	}
	else
	{
		// default is to be visible in ray tracing - which is consistent with behaviour before adding this member
		S.bVisibleInRayTracing = true;
	}
	Ar << S.BaseVertexIndex;
	Ar << S.ClothMappingData;
	Ar << S.BoneMap;
	Ar << S.NumVertices;
	Ar << S.MaxBoneInfluences;
	Ar << S.CorrespondClothAssetIndex;
	Ar << S.ClothingData;
	if (!StripFlags.IsClassDataStripped(DuplicatedVertices))
	{
		Ar << S.DuplicatedVerticesBuffer;
	}
	Ar << S.bDisabled;

	return Ar;
}

class FDwordBitWriter
{
public:
	FDwordBitWriter(TArray<uint32>& Buffer) :
		Buffer(Buffer),
		PendingBits(0ull),
		NumPendingBits(0)
	{
	}

	void PutBits(uint32 Bits, uint32 NumBits)
	{
		check((uint64)Bits < (1ull << NumBits));
		PendingBits |= (uint64)Bits << NumPendingBits;
		NumPendingBits += NumBits;

		while (NumPendingBits >= 32)
		{
			Buffer.Add((uint32)PendingBits);
			PendingBits >>= 32;
			NumPendingBits -= 32;
		}
	}

	void Flush()
	{
		if (NumPendingBits > 0)
			Buffer.Add((uint32)PendingBits);
		PendingBits = 0;
		NumPendingBits = 0;
	}

private:
	TArray<uint32>&	Buffer;
	uint64 			PendingBits;
	int32 			NumPendingBits;
};


void FSkeletalMeshLODRenderData::InitResources(bool bNeedsVertexColors, int32 LODIndex, TArray<UMorphTarget*>& InMorphTargets, USkeletalMesh* Owner)
{
	IncrementMemoryStats(bNeedsVertexColors);

	MorphTargetVertexInfoBuffers.Reset();
	MultiSizeIndexContainer.InitResources();

	BeginInitResource(&StaticVertexBuffers.PositionVertexBuffer);
	BeginInitResource(&StaticVertexBuffers.StaticMeshVertexBuffer);

	SkinWeightVertexBuffer.BeginInitResources();

	if (bNeedsVertexColors)
	{
		// Only init the color buffer if the mesh has vertex colors
		BeginInitResource(&StaticVertexBuffers.ColorVertexBuffer);
	}

	if (ClothVertexBuffer.GetNumVertices() > 0)
	{
		// Only init the clothing buffer if the mesh has clothing data
		BeginInitResource(&ClothVertexBuffer);
	}

	// DuplicatedVerticesBuffer is used only for SkinCache and Editor features which is SM5 only
    if (IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform) || IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5))
	{
		const bool bSkinCacheNeedsDuplicatedVertices = GPUSkinCacheNeedsDuplicatedVertices();
		for (auto& RenderSection : RenderSections)
		{
			if (bSkinCacheNeedsDuplicatedVertices)
			{
				// No need to discard CPU data in cooked builds as bNeedsCPUAccess is false (see FDuplicatedVerticesBuffer constructor), 
				// so it'd be auto-discarded after the RHI has copied the resource data. Keep CPU data when in the editor for geometry operations.
				check(RenderSection.DuplicatedVerticesBuffer.DupVertData.Num());
				BeginInitResource(&RenderSection.DuplicatedVerticesBuffer);
			}
			else
			{
#if !WITH_EDITOR
				// Discard CPU data in cooked builds. Keep CPU data when in the editor for geometry operations.
				RenderSection.DuplicatedVerticesBuffer.ReleaseCPUResources();
#endif
			}
		}
    }

	// UseGPUMorphTargets() can be toggled only on SM5 atm
	if (IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5) && InMorphTargets.Num() > 0)
	{
		// Simple Morph compression 0.1
		// Instead of storing vertex deltas individually they are organized into batches of 64.
		// Each batch has a header that describes how many bits are allocated to each of the vertex components.
		// Batches also store an explicit offset to its associated data. This makes it trivial to decode batches
		// in parallel, and because deltas are fixed-width inside a batch, deltas can also be decoded in parallel.
		// The result is a semi-adaptive encoding that functions as a crude substitute for entropy coding, that is
		// fast to decode on parallel hardware.
		
		// Quantization still happens globally to avoid issues with cracks at duplicate vertices.
		// The quantization is artist controlled on a per LOD basis. Higher error tolerance results in smaller deltas
		// and a smaller compressed size.

		const FSkeletalMeshLODInfo* SkeletalMeshLODInfo = Owner->GetLODInfo(LODIndex);
		
		const float UnrealUnitPerMeter		= 100.0f;
		const float PositionPrecision		= SkeletalMeshLODInfo->MorphTargetPositionErrorTolerance * 2.0f * 1e-6f * UnrealUnitPerMeter;	// * 2.0 because correct rounding guarantees error is at most half of the cell size.
		const float RcpPositionPrecision	= 1.0f / PositionPrecision;

		const float TangentZPrecision		= 1.0f / 2048.0f;				// Object scale irrelevant here. Let's assume ~12bits per component is plenty.
		const float RcpTangentZPrecision	= 1.0f / TangentZPrecision;

		const uint32 BatchSize				= 64;
		const uint32 NumBatchHeaderDwords	= 10u;
		
		const uint32 IndexMaxBits			= 31u;

		const uint32 PositionMaxBits		= 28u;				// Probably more than we need, but let's just allow it to go this high to be safe for now.
																// For larger deltas this can even be more precision than what was in the float input data!
																// Maybe consider float-like or exponential encoding of large values?
		const float PositionMinValue		= -134217728.0f;	// -2^(MaxBits-1)
		const float PositionMaxValue		=  134217720.0f;	// Largest float smaller than 2^(MaxBits-1)-1
																// Using 134217727.0f would NOT work as it would be rounded up to 134217728.0f, which is
																// outside the range.

		const uint32 TangentZMaxBits		= 16u;
		const float TangentZMinValue		= -32768.0f;		// -2^(MaxBits-1)
		const float TangentZMaxValue		=  32767.0f;		//  2^(MaxBits-1)-1

		struct FBatchHeader
		{
			uint32		DataOffset;
			uint32		NumElements;
			bool		bTangents;

			uint32		IndexBits;
			FIntVector	PositionBits;
			FIntVector	TangentZBits;

			uint32		IndexMin;
			FIntVector	PositionMin;
			FIntVector	TangentZMin;
		};

		// uint32 StartTime = FPlatformTime::Cycles();

		MorphTargetVertexInfoBuffers.MorphData.Empty();
		MorphTargetVertexInfoBuffers.NumTotalBatches = 0;
		MorphTargetVertexInfoBuffers.PositionPrecision = PositionPrecision;
		MorphTargetVertexInfoBuffers.TangentZPrecision = TangentZPrecision;

		MorphTargetVertexInfoBuffers.BatchStartOffsetPerMorph.Empty(InMorphTargets.Num());
		MorphTargetVertexInfoBuffers.BatchesPerMorph.Empty(InMorphTargets.Num());
		MorphTargetVertexInfoBuffers.MaximumValuePerMorph.Empty(InMorphTargets.Num());
		MorphTargetVertexInfoBuffers.MinimumValuePerMorph.Empty(InMorphTargets.Num());

		// Mark vertices that are in a section that doesn't recompute tangents as needing tangents
		const int32 RecomputeTangentsMode = GForceRecomputeTangents > 0 ? 1 : GSkinCacheRecomputeTangents;
		TBitArray<> VertexNeedsTangents;
		VertexNeedsTangents.Init(false, StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
		for (const FSkelMeshRenderSection& RenderSection : RenderSections)
		{
			const bool bRecomputeTangents = RecomputeTangentsMode > 0 && (RenderSection.bRecomputeTangent || RecomputeTangentsMode == 1);
			if (!bRecomputeTangents)
			{
				for (uint32 i = 0; i < RenderSection.NumVertices; i++)
				{
					VertexNeedsTangents[RenderSection.BaseVertexIndex + i] = true;
				}
			}
		}

		// Populate the arrays to be filled in later in the render thread
		TArray<FBatchHeader> BatchHeaders;
		TArray<uint32> BitstreamData;
		for (int32 AnimIdx = 0; AnimIdx < InMorphTargets.Num(); ++AnimIdx)
		{
			uint32 BatchStartOffset = MorphTargetVertexInfoBuffers.NumTotalBatches;

			float MaximumValues[4] = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
			float MinimumValues[4] = { +FLT_MAX, +FLT_MAX, +FLT_MAX, +FLT_MAX };
			UMorphTarget* MorphTarget = InMorphTargets[AnimIdx];
			int32 NumSrcDeltas = 0;
			const FMorphTargetDelta* MorphDeltas = MorphTarget->GetMorphTargetDelta(LODIndex, NumSrcDeltas);

			//Make sure the morphtarget data vertex indices fit the geometry
			//If a missmatch happen, set the NumSrcDelta to 0 so the morph target is skipped
			for (int32 DeltaIndex = 0; DeltaIndex < NumSrcDeltas; DeltaIndex++)
			{
				const auto& MorphDelta = MorphDeltas[DeltaIndex];
				if (!VertexNeedsTangents.IsValidIndex(MorphDelta.SourceIdx))
				{
					NumSrcDeltas = 0;
					UE_ASSET_LOG(LogSkeletalMesh, Warning, Owner, TEXT("Skipping morph target %s for LOD %d. The morph target data is incompatible with the mesh data"), *MorphTarget->GetName(), LODIndex);
					break;
				}
			}

			if (NumSrcDeltas == 0)
			{
				MaximumValues[0] = 0.0f;
				MaximumValues[1] = 0.0f;
				MaximumValues[2] = 0.0f;
				MaximumValues[3] = 0.0f;

				MinimumValues[0] = 0.0f;
				MinimumValues[1] = 0.0f;
				MinimumValues[2] = 0.0f;
				MinimumValues[3] = 0.0f;
			}
			else
			{
				struct FQuantizedDelta
				{
					FIntVector	Position;
					FIntVector	TangentZ;
					uint32		Index;
				};
				TArray<FQuantizedDelta> QuantizedDeltas;
				QuantizedDeltas.Reserve(NumSrcDeltas);

				bool bVertexIndicesSorted = true;

				int32 PrevVertexIndex = -1;
				for (int32 DeltaIndex = 0; DeltaIndex < NumSrcDeltas; DeltaIndex++)
				{
					const auto& MorphDelta = MorphDeltas[DeltaIndex];
					const FVector3f TangentZDelta = (VertexNeedsTangents.IsValidIndex(MorphDelta.SourceIdx) && VertexNeedsTangents[MorphDelta.SourceIdx]) ? MorphDelta.TangentZDelta : FVector3f::ZeroVector;

					// when import, we do check threshold, and also when adding weight, we do have threshold for how smaller weight can fit in
					// so no reason to check here another threshold
					MaximumValues[0] = FMath::Max(MaximumValues[0], MorphDelta.PositionDelta.X);
					MaximumValues[1] = FMath::Max(MaximumValues[1], MorphDelta.PositionDelta.Y);
					MaximumValues[2] = FMath::Max(MaximumValues[2], MorphDelta.PositionDelta.Z);
					MaximumValues[3] = FMath::Max(MaximumValues[3], FMath::Max(TangentZDelta.X, FMath::Max(TangentZDelta.Y, TangentZDelta.Z)));

					MinimumValues[0] = FMath::Min(MinimumValues[0], MorphDelta.PositionDelta.X);
					MinimumValues[1] = FMath::Min(MinimumValues[1], MorphDelta.PositionDelta.Y);
					MinimumValues[2] = FMath::Min(MinimumValues[2], MorphDelta.PositionDelta.Z);
					MinimumValues[3] = FMath::Min(MinimumValues[3], FMath::Min(TangentZDelta.X, FMath::Min(TangentZDelta.Y, TangentZDelta.Z)));

					// Check if input is sorted. It usually is, but it might not be.
					if ((int32)MorphDelta.SourceIdx < PrevVertexIndex)
						bVertexIndicesSorted = false;
					PrevVertexIndex = (int32)MorphDelta.SourceIdx;

					// Quantize delta
					FQuantizedDelta QuantizedDelta;
					const FVector3f& PositionDelta	= MorphDelta.PositionDelta;
					QuantizedDelta.Position.X		= FMath::RoundToInt(FMath::Clamp(PositionDelta.X * RcpPositionPrecision, PositionMinValue, PositionMaxValue));
					QuantizedDelta.Position.Y		= FMath::RoundToInt(FMath::Clamp(PositionDelta.Y * RcpPositionPrecision, PositionMinValue, PositionMaxValue));
					QuantizedDelta.Position.Z		= FMath::RoundToInt(FMath::Clamp(PositionDelta.Z * RcpPositionPrecision, PositionMinValue, PositionMaxValue));
					QuantizedDelta.TangentZ.X		= FMath::RoundToInt(FMath::Clamp(TangentZDelta.X * RcpTangentZPrecision, TangentZMinValue, TangentZMaxValue));
					QuantizedDelta.TangentZ.Y		= FMath::RoundToInt(FMath::Clamp(TangentZDelta.Y * RcpTangentZPrecision, TangentZMinValue, TangentZMaxValue));
					QuantizedDelta.TangentZ.Z		= FMath::RoundToInt(FMath::Clamp(TangentZDelta.Z * RcpTangentZPrecision, TangentZMinValue, TangentZMaxValue));
					QuantizedDelta.Index			= MorphDelta.SourceIdx;

					if(QuantizedDelta.Position != FIntVector::ZeroValue || QuantizedDelta.TangentZ != FIntVector::ZeroValue)
					{
						// Only add delta if it is non-zero
						QuantizedDeltas.Add(QuantizedDelta);
					}
				}

				// Sort deltas if the source wasn't already sorted
				if (!bVertexIndicesSorted)
				{
					Algo::Sort(QuantizedDeltas, [](const FQuantizedDelta& A, const FQuantizedDelta& B) { return A.Index < B.Index; });
				}

				// Encode batch deltas
				const uint32 MorphNumBatches = (QuantizedDeltas.Num() + BatchSize - 1u) / BatchSize;
				for (uint32 BatchIndex = 0; BatchIndex < MorphNumBatches; BatchIndex++)
				{
					const uint32 BatchFirstElementIndex = BatchIndex * BatchSize;
					const uint32 NumElements = FMath::Min(BatchSize, QuantizedDeltas.Num() - BatchFirstElementIndex);

					// Calculate batch min/max bounds
					uint32 IndexMin = MAX_uint32;
					uint32 IndexMax = MIN_uint32;
					FIntVector PositionMin = FIntVector(MAX_int32);
					FIntVector PositionMax = FIntVector(MIN_int32);
					FIntVector TangentZMin = FIntVector(MAX_int32);
					FIntVector TangentZMax = FIntVector(MIN_int32);

					for (uint32 LocalElementIndex = 0; LocalElementIndex < NumElements; LocalElementIndex++)
					{
						const FQuantizedDelta& Delta = QuantizedDeltas[BatchFirstElementIndex + LocalElementIndex];

						// Trick: Deltas are sorted by index, so the index increase by at least one per delta.
						//		  Naively this would mean that a batch always spans at least 64 index values and
						//		  indices would have to use at least 6 bits per index.
						//		  If instead of storing the raw index, we store the index relative to its position in the batch,
						//		  then the spanned range becomes 63 smaller.
						//		  For a consecutive range this even gets us down to 0 bits per index!
						check(Delta.Index >= LocalElementIndex);
						const uint32 AdjustedIndex = Delta.Index - LocalElementIndex;
						IndexMin = FMath::Min(IndexMin, AdjustedIndex);
						IndexMax = FMath::Max(IndexMax, AdjustedIndex);
					
						PositionMin.X = FMath::Min(PositionMin.X, Delta.Position.X);
						PositionMin.Y = FMath::Min(PositionMin.Y, Delta.Position.Y);
						PositionMin.Z = FMath::Min(PositionMin.Z, Delta.Position.Z);

						PositionMax.X = FMath::Max(PositionMax.X, Delta.Position.X);
						PositionMax.Y = FMath::Max(PositionMax.Y, Delta.Position.Y);
						PositionMax.Z = FMath::Max(PositionMax.Z, Delta.Position.Z);
						
						TangentZMin.X = FMath::Min(TangentZMin.X, Delta.TangentZ.X);
						TangentZMin.Y = FMath::Min(TangentZMin.Y, Delta.TangentZ.Y);
						TangentZMin.Z = FMath::Min(TangentZMin.Z, Delta.TangentZ.Z);

						TangentZMax.X = FMath::Max(TangentZMax.X, Delta.TangentZ.X);
						TangentZMax.Y = FMath::Max(TangentZMax.Y, Delta.TangentZ.Y);
						TangentZMax.Z = FMath::Max(TangentZMax.Z, Delta.TangentZ.Z);
					}

					const uint32 IndexDelta			= IndexMax - IndexMin;
					const FIntVector PositionDelta	= PositionMax - PositionMin;
					const FIntVector TangentZDelta	= TangentZMax - TangentZMin;
					const bool bBatchHasTangents	= TangentZMin != FIntVector::ZeroValue || TangentZMax != FIntVector::ZeroValue;
					
					FBatchHeader BatchHeader;
					BatchHeader.DataOffset			= BitstreamData.Num() * sizeof(uint32);
					BatchHeader.bTangents			= bBatchHasTangents;
					BatchHeader.NumElements			= NumElements;
					BatchHeader.IndexBits			= FMath::CeilLogTwo(IndexDelta + 1);
					BatchHeader.PositionBits.X		= FMath::CeilLogTwo(uint32(PositionDelta.X) + 1);
					BatchHeader.PositionBits.Y		= FMath::CeilLogTwo(uint32(PositionDelta.Y) + 1);
					BatchHeader.PositionBits.Z		= FMath::CeilLogTwo(uint32(PositionDelta.Z) + 1);
					BatchHeader.TangentZBits.X		= FMath::CeilLogTwo(uint32(TangentZDelta.X) + 1);
					BatchHeader.TangentZBits.Y		= FMath::CeilLogTwo(uint32(TangentZDelta.Y) + 1);
					BatchHeader.TangentZBits.Z		= FMath::CeilLogTwo(uint32(TangentZDelta.Z) + 1);
					check(BatchHeader.IndexBits <= IndexMaxBits);
					check(BatchHeader.PositionBits.X <= PositionMaxBits);
					check(BatchHeader.PositionBits.Y <= PositionMaxBits);
					check(BatchHeader.PositionBits.Z <= PositionMaxBits);
					check(BatchHeader.TangentZBits.X <= TangentZMaxBits);
					check(BatchHeader.TangentZBits.Y <= TangentZMaxBits);
					check(BatchHeader.TangentZBits.Z <= TangentZMaxBits);
					BatchHeader.IndexMin			= IndexMin;
					BatchHeader.PositionMin			= PositionMin;
					BatchHeader.TangentZMin			= TangentZMin;
					
					// Write quantized bits
					FDwordBitWriter BitWriter(BitstreamData);
					for (uint32 LocalElementIndex = 0; LocalElementIndex < NumElements; LocalElementIndex++)
					{
						const FQuantizedDelta& Delta = QuantizedDeltas[BatchFirstElementIndex + LocalElementIndex];
						const uint32 AdjustedIndex = Delta.Index - LocalElementIndex;
						BitWriter.PutBits(AdjustedIndex - IndexMin,					BatchHeader.IndexBits);
						BitWriter.PutBits(uint32(Delta.Position.X - PositionMin.X), BatchHeader.PositionBits.X);
						BitWriter.PutBits(uint32(Delta.Position.Y - PositionMin.Y), BatchHeader.PositionBits.Y);
						BitWriter.PutBits(uint32(Delta.Position.Z - PositionMin.Z), BatchHeader.PositionBits.Z);
						if (bBatchHasTangents)
						{
							BitWriter.PutBits(uint32(Delta.TangentZ.X - TangentZMin.X), BatchHeader.TangentZBits.X);
							BitWriter.PutBits(uint32(Delta.TangentZ.Y - TangentZMin.Y), BatchHeader.TangentZBits.Y);
							BitWriter.PutBits(uint32(Delta.TangentZ.Z - TangentZMin.Z), BatchHeader.TangentZBits.Z);
						}
					}
					BitWriter.Flush();

					BatchHeaders.Add(BatchHeader);
				}
				MorphTargetVertexInfoBuffers.NumTotalBatches += MorphNumBatches;
			}

			const uint32 MorphNumBatches = MorphTargetVertexInfoBuffers.NumTotalBatches - BatchStartOffset;
			MorphTargetVertexInfoBuffers.BatchStartOffsetPerMorph.Add(BatchStartOffset);
			MorphTargetVertexInfoBuffers.BatchesPerMorph.Add(MorphNumBatches);
			MorphTargetVertexInfoBuffers.MaximumValuePerMorph.Add(FVector4(MaximumValues[0], MaximumValues[1], MaximumValues[2], MaximumValues[3]));
			MorphTargetVertexInfoBuffers.MinimumValuePerMorph.Add(FVector4(MinimumValues[0], MinimumValues[1], MinimumValues[2], MinimumValues[3]));

#if !WITH_EDITOR
			if (NumSrcDeltas > 0)
			{
				// A CPU copy of the morph deltas has beenA  made so it is safe to 
				// discard the original data.  Keep CPU buffers when in the editor.
				MorphTarget->DiscardVertexData();
			}
#endif
		}


		// Write packed batch headers
		for (const FBatchHeader& BatchHeader : BatchHeaders)
		{
			const uint32 DataOffset = BatchHeader.DataOffset + BatchHeaders.Num() * NumBatchHeaderDwords * sizeof(uint32);

			MorphTargetVertexInfoBuffers.MorphData.Add(DataOffset);
			MorphTargetVertexInfoBuffers.MorphData.Add(	BatchHeader.IndexBits | 
														(BatchHeader.PositionBits.X << 5) | (BatchHeader.PositionBits.Y << 10) | (BatchHeader.PositionBits.Z << 15) |
														(BatchHeader.bTangents ? (1u << 20) : 0u) | 
														(BatchHeader.NumElements << 21));
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.IndexMin);
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.PositionMin.X);
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.PositionMin.Y);
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.PositionMin.Z);

			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.TangentZBits.X | (BatchHeader.TangentZBits.Y << 5) | (BatchHeader.TangentZBits.Z << 10));
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.TangentZMin.X);
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.TangentZMin.Y);
			MorphTargetVertexInfoBuffers.MorphData.Add(BatchHeader.TangentZMin.Z);
		}

		// Append bitstream data
		MorphTargetVertexInfoBuffers.MorphData.Append(BitstreamData);

		// UE_LOG(LogStaticMesh, Log, TEXT("Morph compression time: [%.2fs]"), FPlatformTime::ToMilliseconds(FPlatformTime::Cycles() - StartTime) / 1000.0f);

		check(MorphTargetVertexInfoBuffers.BatchesPerMorph.Num() == MorphTargetVertexInfoBuffers.BatchStartOffsetPerMorph.Num());
		check(MorphTargetVertexInfoBuffers.BatchesPerMorph.Num() == MorphTargetVertexInfoBuffers.MaximumValuePerMorph.Num());
		check(MorphTargetVertexInfoBuffers.BatchesPerMorph.Num() == MorphTargetVertexInfoBuffers.MinimumValuePerMorph.Num());
		if (MorphTargetVertexInfoBuffers.NumTotalBatches > 0)
		{
			BeginInitResource(&MorphTargetVertexInfoBuffers);
		}
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		if (SourceRayTracingGeometry.RawData.Num() > 0)
		{
			BeginInitResource(&SourceRayTracingGeometry);
		}
	}
#endif
}

void FSkeletalMeshLODRenderData::ReleaseResources()
{
	DecrementMemoryStats();

	MultiSizeIndexContainer.ReleaseResources();

	BeginReleaseResource(&StaticVertexBuffers.PositionVertexBuffer);
	BeginReleaseResource(&StaticVertexBuffers.StaticMeshVertexBuffer);
	SkinWeightVertexBuffer.BeginReleaseResources();
	BeginReleaseResource(&StaticVertexBuffers.ColorVertexBuffer);
	BeginReleaseResource(&ClothVertexBuffer);
	// DuplicatedVerticesBuffer is used only for SkinCache and Editor features which is SM5 only
    if (IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform) || IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5))
	{
		if (GPUSkinCacheNeedsDuplicatedVertices())
		{
			for (auto& RenderSection : RenderSections)
			{
#if WITH_EDITOR
				check(RenderSection.DuplicatedVerticesBuffer.DupVertData.Num());
#endif
				BeginReleaseResource(&RenderSection.DuplicatedVerticesBuffer);
			}
		}
	}
	BeginReleaseResource(&MorphTargetVertexInfoBuffers);
	
	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, SkinWeightProfilesData.GetResourcesSize());
	SkinWeightProfilesData.ReleaseResources();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		BeginReleaseResource(&SourceRayTracingGeometry);
		BeginReleaseResource(&StaticRayTracingGeometry);
	}
#endif
}

void FSkeletalMeshLODRenderData::IncrementMemoryStats(bool bNeedsVertexColors)
{
	INC_DWORD_STAT_BY(STAT_SkeletalMeshIndexMemory, MultiSizeIndexContainer.IsIndexBufferValid() ? (MultiSizeIndexContainer.GetIndexBuffer()->Num() * MultiSizeIndexContainer.GetDataTypeSize()) : 0);
	INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.PositionVertexBuffer.GetStride() * StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
	INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.StaticMeshVertexBuffer.GetResourceSize());
	INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, SkinWeightVertexBuffer.GetVertexDataSize());

	if (bNeedsVertexColors)
	{
		INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.ColorVertexBuffer.GetAllocatedSize());
	}

	if (ClothVertexBuffer.GetNumVertices() > 0)
	{
		INC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, ClothVertexBuffer.GetVertexDataSize());
	}
}

void FSkeletalMeshLODRenderData::DecrementMemoryStats()
{
	DEC_DWORD_STAT_BY(STAT_SkeletalMeshIndexMemory, MultiSizeIndexContainer.IsIndexBufferValid() ? (MultiSizeIndexContainer.GetIndexBuffer()->Num() * MultiSizeIndexContainer.GetDataTypeSize()) : 0);

	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.PositionVertexBuffer.GetStride() * StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.StaticMeshVertexBuffer.GetResourceSize());

	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, SkinWeightVertexBuffer.GetVertexDataSize());
	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, StaticVertexBuffers.ColorVertexBuffer.GetAllocatedSize());
	DEC_DWORD_STAT_BY(STAT_SkeletalMeshVertexMemory, ClothVertexBuffer.GetVertexDataSize());
}

#if WITH_EDITOR

void FSkeletalMeshLODRenderData::BuildFromLODModel(const FSkeletalMeshLODModel* ImportedModel, uint32 BuildFlags)
{
	bool bUseFullPrecisionUVs = (BuildFlags & ESkeletalMeshVertexFlags::UseFullPrecisionUVs) != 0;
	bool bUseHighPrecisionTangentBasis = (BuildFlags & ESkeletalMeshVertexFlags::UseHighPrecisionTangentBasis) != 0;
	bool bHasVertexColors = (BuildFlags & ESkeletalMeshVertexFlags::HasVertexColors) != 0;
	bool bUseBackwardsCompatibleF16TruncUVs = (BuildFlags & ESkeletalMeshVertexFlags::UseBackwardsCompatibleF16TruncUVs) != 0;

	// Copy required info from source sections
	RenderSections.Empty();
	for (int32 SectionIndex = 0; SectionIndex < ImportedModel->Sections.Num(); SectionIndex++)
	{
		const FSkelMeshSection& ModelSection = ImportedModel->Sections[SectionIndex];

		FSkelMeshRenderSection NewRenderSection;
		NewRenderSection.MaterialIndex = ModelSection.MaterialIndex;
		NewRenderSection.BaseIndex = ModelSection.BaseIndex;
		NewRenderSection.NumTriangles = ModelSection.NumTriangles;
		NewRenderSection.bRecomputeTangent = ModelSection.bRecomputeTangent;
		NewRenderSection.RecomputeTangentsVertexMaskChannel = ModelSection.RecomputeTangentsVertexMaskChannel;
		NewRenderSection.bCastShadow = ModelSection.bCastShadow;
		NewRenderSection.bVisibleInRayTracing = ModelSection.bVisibleInRayTracing;
		NewRenderSection.BaseVertexIndex = ModelSection.BaseVertexIndex;
		NewRenderSection.ClothMappingData = ModelSection.ClothMappingData;
		NewRenderSection.BoneMap = ModelSection.BoneMap;
		NewRenderSection.NumVertices = ModelSection.NumVertices;
		NewRenderSection.MaxBoneInfluences = ModelSection.MaxBoneInfluences;
		NewRenderSection.CorrespondClothAssetIndex = ModelSection.CorrespondClothAssetIndex;
		NewRenderSection.ClothingData = ModelSection.ClothingData;
		NewRenderSection.DuplicatedVerticesBuffer.Init(ModelSection.NumVertices, ModelSection.OverlappingVertices);
		NewRenderSection.bDisabled = ModelSection.bDisabled;
		RenderSections.Add(NewRenderSection);
	}

	TArray<FSoftSkinVertex> Vertices;
	ImportedModel->GetVertices(Vertices);

	// match UV and tangent precision for mesh vertex buffer to setting from parent mesh
	StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(bUseFullPrecisionUVs);
	StaticVertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(bUseHighPrecisionTangentBasis);

	// init vertex buffer with the vertex array
	StaticVertexBuffers.PositionVertexBuffer.Init(Vertices.Num());
	StaticVertexBuffers.StaticMeshVertexBuffer.Init(Vertices.Num(), ImportedModel->NumTexCoords);

	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		StaticVertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertices[i].Position;
		StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertices[i].TangentX, Vertices[i].TangentY, Vertices[i].TangentZ);
		for (uint32 j = 0; j < ImportedModel->NumTexCoords; j++)
		{
			StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, j, Vertices[i].UVs[j], bUseBackwardsCompatibleF16TruncUVs);
		}
	}

	// Init skin weight buffer
	SkinWeightVertexBuffer.SetNeedsCPUAccess(true);
	SkinWeightVertexBuffer.SetMaxBoneInfluences(ImportedModel->GetMaxBoneInfluences());
	SkinWeightVertexBuffer.SetUse16BitBoneIndex(ImportedModel->DoSectionsUse16BitBoneIndex());
	SkinWeightVertexBuffer.Init(Vertices);

	// Init the color buffer if this mesh has vertex colors.
	if (bHasVertexColors && Vertices.Num() > 0 && StaticVertexBuffers.ColorVertexBuffer.GetAllocatedSize() == 0)
	{
		StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(&Vertices[0].Color, Vertices.Num(), sizeof(FSoftSkinVertex));
	}

	if (ImportedModel->HasClothData())
	{
		TArray<FMeshToMeshVertData> MappingData;
		TArray<uint64> ClothIndexMapping;
		ImportedModel->GetClothMappingData(MappingData, ClothIndexMapping);
		ClothVertexBuffer.Init(MappingData, ClothIndexMapping);
	}

	const uint8 DataTypeSize = (ImportedModel->NumVertices < MAX_uint16) ? sizeof(uint16) : sizeof(uint32);

	MultiSizeIndexContainer.RebuildIndexBuffer(DataTypeSize, ImportedModel->IndexBuffer);
	
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

	// MorphTargetVertexInfoBuffers are created in InitResources

	SkinWeightProfilesData.Init(&SkinWeightVertexBuffer);
	// Generate runtime version of skin weight profile data, containing all required per-skin weight override data
	for (const auto& Pair : ImportedModel->SkinWeightProfiles)
	{
		FRuntimeSkinWeightProfileData& Override = SkinWeightProfilesData.AddOverrideData(Pair.Key);
		MeshUtilities.GenerateRuntimeSkinWeightData(ImportedModel, Pair.Value.SkinWeights, Override);
	}

	ActiveBoneIndices = ImportedModel->ActiveBoneIndices;
	RequiredBones = ImportedModel->RequiredBones;
}

#endif // WITH_EDITOR

void FSkeletalMeshLODRenderData::ReleaseCPUResources(bool bForStreaming)
{
	if (!GIsEditor && !IsRunningCommandlet())
	{
		if (MultiSizeIndexContainer.IsIndexBufferValid())
		{
			MultiSizeIndexContainer.GetIndexBuffer()->Empty();
		}

		SkinWeightVertexBuffer.CleanUp();
		StaticVertexBuffers.PositionVertexBuffer.CleanUp();
		StaticVertexBuffers.StaticMeshVertexBuffer.CleanUp();

		if (bForStreaming)
		{
			ClothVertexBuffer.CleanUp();
			StaticVertexBuffers.ColorVertexBuffer.CleanUp();
			SkinWeightProfilesData.ReleaseCPUResources();
		}
	}
}


void FSkeletalMeshLODRenderData::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	if (MultiSizeIndexContainer.IsIndexBufferValid())
	{
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = MultiSizeIndexContainer.GetIndexBuffer();
		if (IndexBuffer)
		{
			CumulativeResourceSize.AddUnknownMemoryBytes(IndexBuffer->GetResourceDataSize());
		}
	}

	CumulativeResourceSize.AddUnknownMemoryBytes(StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() * StaticVertexBuffers.PositionVertexBuffer.GetStride());
	CumulativeResourceSize.AddUnknownMemoryBytes(StaticVertexBuffers.StaticMeshVertexBuffer.GetResourceSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(SkinWeightVertexBuffer.GetVertexDataSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(StaticVertexBuffers.ColorVertexBuffer.GetAllocatedSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(ClothVertexBuffer.GetVertexDataSize());
	CumulativeResourceSize.AddUnknownMemoryBytes(SkinWeightProfilesData.GetResourcesSize());	
}

SIZE_T FSkeletalMeshLODRenderData::GetCPUAccessMemoryOverhead() const
{
	SIZE_T Result = 0;

	if (MultiSizeIndexContainer.IsIndexBufferValid())
	{
		const FRawStaticIndexBuffer16or32Interface* IndexBuffer = MultiSizeIndexContainer.GetIndexBuffer();
		Result += IndexBuffer && IndexBuffer->GetNeedsCPUAccess() ? IndexBuffer->GetResourceDataSize() : 0;
	}

	Result += StaticVertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess() ? StaticVertexBuffers.StaticMeshVertexBuffer.GetResourceSize() : 0;
	Result += StaticVertexBuffers.PositionVertexBuffer.GetAllowCPUAccess() ? StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() * StaticVertexBuffers.PositionVertexBuffer.GetStride() : 0;
	Result += StaticVertexBuffers.ColorVertexBuffer.GetAllowCPUAccess() ? StaticVertexBuffers.ColorVertexBuffer.GetAllocatedSize() : 0;
	Result += SkinWeightVertexBuffer.GetNeedsCPUAccess() ? SkinWeightVertexBuffer.GetVertexDataSize() : 0;
	Result += ClothVertexBuffer.GetVertexDataSize();
	Result += SkinWeightProfilesData.GetCPUAccessMemoryOverhead();
	return Result;
}

int32 FSkeletalMeshLODRenderData::GetPlatformMinLODIdx(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh)
{
#if WITH_EDITOR
	check(TargetPlatform && SkeletalMesh);
	const FName IniPlatformName = TargetPlatform->GetPlatformInfo().IniPlatformName;
	return  SkeletalMesh->GetMinLod().GetValueForPlatform(IniPlatformName);
#else
	return 0;
#endif
}

uint8 FSkeletalMeshLODRenderData::GenerateClassStripFlags(FArchive& Ar, const USkeletalMesh* OwnerMesh, int32 LODIdx)
{
#if WITH_EDITOR
	const bool bIsCook = Ar.IsCooking();
	const ITargetPlatform* CookTarget = Ar.CookingTarget();

	int32 MinMeshLod = 0;
	bool bMeshDisablesMinLodStrip = false;
	if (bIsCook)
	{
		MinMeshLod = OwnerMesh ? OwnerMesh->GetMinLod().GetValueForPlatform(CookTarget->GetPlatformInfo().IniPlatformName) : 0;
		bMeshDisablesMinLodStrip = OwnerMesh ? OwnerMesh->GetDisableBelowMinLodStripping().GetValueForPlatform(CookTarget->GetPlatformInfo().IniPlatformName) : false;
	}
	const bool bWantToStripBelowMinLod = bIsCook && GStripSkeletalMeshLodsDuringCooking != 0 && MinMeshLod > LODIdx && !bMeshDisablesMinLodStrip;

	uint8 ClassDataStripFlags = 0;
	ClassDataStripFlags |= bWantToStripBelowMinLod ? CDSF_MinLodData : 0;
	return ClassDataStripFlags;
#else
	return 0;
#endif
}

bool FSkeletalMeshLODRenderData::IsLODCookedOut(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh, bool bIsBelowMinLOD)
{
	check(SkeletalMesh);
#if WITH_EDITOR
	if (!bIsBelowMinLOD)
	{
		return false;
	}

	if (!TargetPlatform)
	{
		TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	}
	check(TargetPlatform);

	return !SkeletalMesh->GetEnableLODStreaming(TargetPlatform);
#else
	return false;
#endif
}

bool FSkeletalMeshLODRenderData::IsLODInlined(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh, int32 LODIdx, bool bIsBelowMinLOD)
{
	check(SkeletalMesh);
#if WITH_EDITOR
	if (!TargetPlatform)
	{
		TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	}
	check(TargetPlatform);

	if (!SkeletalMesh->GetEnableLODStreaming(TargetPlatform))
	{
		return true;
	}

	if (bIsBelowMinLOD)
	{
		return false;
	}

	const int32 MaxNumStreamedLODs = SkeletalMesh->GetMaxNumStreamedLODs(TargetPlatform);
	const int32 NumLODs = SkeletalMesh->GetLODNum();
	const int32 NumStreamedLODs = FMath::Min(MaxNumStreamedLODs, NumLODs - 1);
	const int32 InlinedLODStartIdx = NumStreamedLODs;
	return LODIdx >= InlinedLODStartIdx;
#else
	return false;
#endif
}

int32 FSkeletalMeshLODRenderData::GetNumOptionalLODsAllowed(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh)
{
#if WITH_EDITOR
	check(TargetPlatform && SkeletalMesh);
	return SkeletalMesh->GetMaxNumOptionalLODs(TargetPlatform);
#else
	return 0;
#endif
}

bool FSkeletalMeshLODRenderData::ShouldForceKeepCPUResources()
{
#if !WITH_EDITOR
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.FreeSkeletalMeshBuffers"));
	if (CVar)
	{
		return !CVar->GetValueOnAnyThread();
	}
#endif
	return true;
}

bool FSkeletalMeshLODRenderData::ShouldKeepCPUResources(const USkeletalMesh* SkeletalMesh, int32 LODIdx, bool bForceKeep)
{
	return bForceKeep
		|| SkeletalMesh->GetResourceForRendering()->RequiresCPUSkinning(GMaxRHIFeatureLevel)
		|| SkeletalMesh->NeedCPUData(LODIdx);
}

class FSkeletalMeshLODSizeCounter : public FArchive
{
public:
	FSkeletalMeshLODSizeCounter()
		: Size(0)
	{
		SetIsSaving(true);
		SetIsPersistent(true);
		ArIsCountingMemory = true;
	}

	virtual void Serialize(void*, int64 Length) final override
	{
		Size += Length;
	}

	virtual int64 TotalSize() final override
	{
		return Size;
	}

private:
	int64 Size;
};

void FSkeletalMeshLODRenderData::SerializeStreamedData(FArchive& Ar, USkeletalMesh* Owner, int32 LODIdx, uint8 ClassDataStripFlags, bool bNeedsCPUAccess, bool bForceKeepCPUResources)
{
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	FStripDataFlags StripFlags(Ar, ClassDataStripFlags);

	// TODO: A lot of data in a render section is needed during initialization but maybe some can still be streamed
	//Ar << RenderSections;

	MultiSizeIndexContainer.Serialize(Ar, bNeedsCPUAccess);

	if (Ar.IsLoading())
	{
		SkinWeightVertexBuffer.SetNeedsCPUAccess(bNeedsCPUAccess);
	}

	StaticVertexBuffers.PositionVertexBuffer.Serialize(Ar, bNeedsCPUAccess);
	StaticVertexBuffers.StaticMeshVertexBuffer.Serialize(Ar, bNeedsCPUAccess);
	Ar << SkinWeightVertexBuffer;

	if (Owner && Owner->GetHasVertexColors())
	{
		StaticVertexBuffers.ColorVertexBuffer.Serialize(Ar, bForceKeepCPUResources);
	}

	if (Ar.IsLoading() && Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RemovingTessellation && !StripFlags.IsClassDataStripped(CDSF_AdjacencyData_DEPRECATED))
	{
		FMultiSizeIndexContainer AdjacencyMultiSizeIndexContainer;
		AdjacencyMultiSizeIndexContainer.Serialize(Ar, bForceKeepCPUResources);
	}

	if (HasClothData())
	{
		Ar << ClothVertexBuffer;
	}

	Ar << SkinWeightProfilesData;
	SkinWeightProfilesData.Init(&SkinWeightVertexBuffer);

	if (Ar.IsLoading())
	{
#if !WITH_EDITOR
		if (GSkinWeightProfilesLoadByDefaultMode == 1)
		{
			// Only allow overriding the base buffer in non-editor builds as it could otherwise be serialized into the asset
			SkinWeightProfilesData.OverrideBaseBufferSkinWeightData(Owner, LODIdx);
		} else
	
#endif
		if (GSkinWeightProfilesLoadByDefaultMode == 3)
		{
			SkinWeightProfilesData.SetDynamicDefaultSkinWeightProfile(Owner, LODIdx, true);
		}
	}
	Ar << SourceRayTracingGeometry.RawData;
}

void FSkeletalMeshLODRenderData::SerializeAvailabilityInfo(FArchive& Ar, USkeletalMesh* Owner, int32 LODIdx, bool bAdjacencyDataStripped, bool bNeedsCPUAccess)
{
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	MultiSizeIndexContainer.SerializeMetaData(Ar, bNeedsCPUAccess);
	if (Ar.IsLoading() && Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RemovingTessellation && !bAdjacencyDataStripped)
	{
		FMultiSizeIndexContainer AdjacencyMultiSizeIndexContainer;
		AdjacencyMultiSizeIndexContainer.SerializeMetaData(Ar, bNeedsCPUAccess);
	}
	StaticVertexBuffers.StaticMeshVertexBuffer.SerializeMetaData(Ar);
	StaticVertexBuffers.PositionVertexBuffer.SerializeMetaData(Ar);
	StaticVertexBuffers.ColorVertexBuffer.SerializeMetaData(Ar);
	if (Ar.IsLoading())
	{
		SkinWeightVertexBuffer.SetNeedsCPUAccess(bNeedsCPUAccess);
	}
	SkinWeightVertexBuffer.SerializeMetaData(Ar);
	if (HasClothData())
	{
		ClothVertexBuffer.SerializeMetaData(Ar);
	}
	SkinWeightProfilesData.SerializeMetaData(Ar);
	SkinWeightProfilesData.Init(&SkinWeightVertexBuffer);
}

void FSkeletalMeshLODRenderData::Serialize(FArchive& Ar, UObject* Owner, int32 Idx)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FSkeletalMeshLODRenderData::Serialize"), STAT_SkeletalMeshLODRenderData_Serialize, STATGROUP_LoadTime);

	USkeletalMesh* OwnerMesh = CastChecked<USkeletalMesh>(Owner);
	
	// Shouldn't needed but to make some static analyzers happy
	if (!OwnerMesh)
	{
		return;
	}
	
	// Actual flags used during serialization
	const uint8 ClassDataStripFlags = GenerateClassStripFlags(Ar, OwnerMesh, Idx);
	FStripDataFlags StripFlags(Ar, ClassDataStripFlags);

#if WITH_EDITOR
	const bool bIsBelowMinLOD = StripFlags.IsClassDataStripped(CDSF_MinLodData)
		|| (Ar.IsCooking() && OwnerMesh && Idx < GetPlatformMinLODIdx(Ar.CookingTarget(), OwnerMesh));
#else
	const bool bIsBelowMinLOD = false;
#endif
	bool bIsLODCookedOut = false;
	bool bInlined = false;

	if (Ar.IsSaving() && !Ar.IsCooking() && !!(Ar.GetPortFlags() & PPF_Duplicate))
	{
		bInlined = bStreamedDataInlined;
		bIsLODCookedOut = bIsBelowMinLOD && bInlined;
		Ar << bIsLODCookedOut;
		Ar << bInlined;
	}
	else
	{
		bIsLODCookedOut = IsLODCookedOut(Ar.CookingTarget(), OwnerMesh, bIsBelowMinLOD);
		Ar << bIsLODCookedOut;

		bInlined = bIsLODCookedOut || IsLODInlined(Ar.CookingTarget(), OwnerMesh, Idx, bIsBelowMinLOD);
		Ar << bInlined;
		bStreamedDataInlined = bInlined;
	}

	// Skeletal mesh buffers are kept in CPU memory after initialization to support merging of skeletal meshes.
	const bool bForceKeepCPUResources = ShouldForceKeepCPUResources();
	bool bNeedsCPUAccess = bForceKeepCPUResources;

	if (!StripFlags.IsDataStrippedForServer())
	{
		// set cpu skinning flag on the vertex buffer so that the resource arrays know if they need to be CPU accessible
		bNeedsCPUAccess = ShouldKeepCPUResources(OwnerMesh, Idx, bForceKeepCPUResources);
	}

	if (Ar.IsFilterEditorOnly())
	{
		if (bNeedsCPUAccess)
		{
			UE_LOG(LogStaticMesh, Verbose, TEXT("[%s] Skeletal Mesh is marked for CPU read."), *OwnerMesh->GetName());
		}
	}

	Ar << RequiredBones;

	if (!StripFlags.IsDataStrippedForServer() && !bIsLODCookedOut)
	{
		Ar << RenderSections;
		Ar << ActiveBoneIndices;

#if WITH_EDITOR
		if (Ar.IsSaving())
		{
			FSkeletalMeshLODSizeCounter LODSizeCounter;
			LODSizeCounter.SetCookingTarget(Ar.CookingTarget());
			LODSizeCounter.SetByteSwapping(Ar.IsByteSwapping());
			SerializeStreamedData(LODSizeCounter, OwnerMesh, Idx, ClassDataStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
			BuffersSize = LODSizeCounter.TotalSize();
		}
#endif
		Ar << BuffersSize;

		if (bInlined)
		{
			SerializeStreamedData(Ar, OwnerMesh, Idx, ClassDataStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
			bIsLODOptional = false;
		}
		else if (Ar.IsFilterEditorOnly())
		{
			bool bDiscardBulkData = false;

#if WITH_EDITOR
			if (Ar.IsSaving())
			{
				const int32 MaxNumOptionalLODs = GetNumOptionalLODsAllowed(Ar.CookingTarget(), OwnerMesh);
				const int32 OptionalLODIdx = GetPlatformMinLODIdx(Ar.CookingTarget(), OwnerMesh) - Idx;
				bDiscardBulkData = OptionalLODIdx > MaxNumOptionalLODs;

				TArray<uint8> TmpBuff;
				if (!bDiscardBulkData)
				{
					FMemoryWriter MemWriter(TmpBuff, true);
					MemWriter.SetCookingTarget(Ar.CookingTarget());
					MemWriter.SetByteSwapping(Ar.IsByteSwapping());
					SerializeStreamedData(MemWriter, OwnerMesh, Idx, ClassDataStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
				}

				bIsLODOptional = bIsBelowMinLOD;
				const uint32 BulkDataFlags = (bDiscardBulkData ? 0 : BULKDATA_Force_NOT_InlinePayload)
					| (bIsLODOptional ? BULKDATA_OptionalPayload : 0);
				const uint32 OldBulkDataFlags = BulkData.GetBulkDataFlags();
				BulkData.ClearBulkDataFlags(0xffffffffu);
				BulkData.SetBulkDataFlags(BulkDataFlags);
				if (TmpBuff.Num() > 0)
				{
					BulkData.Lock(LOCK_READ_WRITE);
					void* BulkDataMem = BulkData.Realloc(TmpBuff.Num());
					FMemory::Memcpy(BulkDataMem, TmpBuff.GetData(), TmpBuff.Num());
					BulkData.Unlock();
				}
				BulkData.Serialize(Ar, Owner, Idx);
				BulkData.ClearBulkDataFlags(0xffffffffu);
				BulkData.SetBulkDataFlags(OldBulkDataFlags);
			}
			else
#endif
			{
#if USE_BULKDATA_STREAMING_TOKEN
				FByteBulkData TmpBulkData;
				TmpBulkData.Serialize(Ar, Owner, Idx, false);
				bIsLODOptional = TmpBulkData.IsOptional();

				StreamingBulkData = TmpBulkData.CreateStreamingToken();
#else
				StreamingBulkData.Serialize(Ar, Owner, Idx, false);
				bIsLODOptional = StreamingBulkData.IsOptional();
#endif
			
				if (StreamingBulkData.GetBulkDataSize() == 0)
				{
					bDiscardBulkData = true;
					BuffersSize = 0;
				}
			}

			if (!bDiscardBulkData)
			{
				SerializeAvailabilityInfo(Ar, OwnerMesh, Idx, StripFlags.IsClassDataStripped(CDSF_AdjacencyData_DEPRECATED), bNeedsCPUAccess);
			}
		}
	}
}

int32 FSkeletalMeshLODRenderData::NumNonClothingSections() const
{
	int32 NumSections = RenderSections.Num();
	int32 Count = 0;

	for (int32 i = 0; i < NumSections; i++)
	{
		const FSkelMeshRenderSection& Section = RenderSections[i];

		// If we have found the start of the clothing section, return that index, since it is equal to the number of non-clothing entries.
		if (!Section.HasClothingData())
		{
			Count++;
		}
	}

	return Count;
}

uint32 FSkeletalMeshLODRenderData::FindSectionIndex(const FSkelMeshRenderSection& Section) const
{
	const FSkelMeshRenderSection* Start = RenderSections.GetData();

	if (Start == nullptr)
	{
		return -1;
	}

	uint32 Ret = &Section - Start;

	if (Ret >= (uint32)RenderSections.Num())
	{
		Ret = -1;
	}

	return Ret;
}

int32 FSkeletalMeshLODRenderData::GetTotalFaces() const
{
	int32 TotalFaces = 0;
	for (int32 i = 0; i < RenderSections.Num(); i++)
	{
		TotalFaces += RenderSections[i].NumTriangles;
	}

	return TotalFaces;
}

bool FSkeletalMeshLODRenderData::HasClothData() const
{
	for (int32 SectionIdx = 0; SectionIdx<RenderSections.Num(); SectionIdx++)
	{
		if (RenderSections[SectionIdx].HasClothingData())
		{
			return true;
		}
	}
	return false;
}

void FSkeletalMeshLODRenderData::GetSectionFromVertexIndex(int32 InVertIndex, int32& OutSectionIndex, int32& OutVertIndex) const
{
	OutSectionIndex = 0;
	OutVertIndex = 0;

	int32 VertCount = 0;

	// Iterate over each chunk
	for (int32 SectionCount = 0; SectionCount < RenderSections.Num(); SectionCount++)
	{
		const FSkelMeshRenderSection& Section = RenderSections[SectionCount];
		OutSectionIndex = SectionCount;

		// Is it in Soft vertex range?
		if (InVertIndex < VertCount + Section.GetNumVertices())
		{
			OutVertIndex = InVertIndex - VertCount;
			return;
		}
		VertCount += Section.NumVertices;
	}

	// InVertIndex should always be in some chunk!
	//check(false);
}

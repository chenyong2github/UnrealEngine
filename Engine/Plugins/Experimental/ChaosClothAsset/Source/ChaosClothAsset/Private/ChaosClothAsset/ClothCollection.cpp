// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothCollection.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace UE::Chaos::ClothAsset::Private
{
	// Groups
	static const FName LodsGroup(TEXT("Lods"));
	static const FName MaterialsGroup(TEXT("Materials"));
	static const FName TetherBatchesGroup(TEXT("TetherBatches"));
	static const FName TethersGroup(TEXT("Tethers"));
	static const FName SeamsGroup(TEXT("Seams"));
	static const FName PatternsGroup(TEXT("Patterns"));
	static const FName SimFacesGroup(TEXT("SimFaces"));
	static const FName SimVerticesGroup(TEXT("SimVertices"));
	static const FName RenderFacesGroup(TEXT("RenderFaces"));
	static const FName RenderVerticesGroup(TEXT("RenderVertices"));

	// LODs Group
	static const FName MaterialStartAttribute(TEXT("MaterialStart"));
	static const FName MaterialEndAttribute(TEXT("MaterialEnd"));
	static const FName TetherBatchStartAttribute(TEXT("TetherBatchStart"));
	static const FName TetherBatchEndAttribute(TEXT("TetherBatchEnd"));
	static const FName SeamStartAttribute(TEXT("SeamStart"));
	static const FName SeamEndAttribute(TEXT("SeamEnd"));
	static const FName PatternStartAttribute(TEXT("PatternStart"));
	static const FName PatternEndAttribute(TEXT("PatternEnd"));
	static const FName PhysicsAssetPathNameAttribute(TEXT("PhysicsAssetPathName"));
	static const FName SkeletonAssetPathNameAttribute(TEXT("SkeletonAssetPathName"));
	static const TArray<FName> LodsGroupAttributes =
	{
		MaterialStartAttribute,
		MaterialEndAttribute,
		TetherBatchStartAttribute,
		TetherBatchEndAttribute,
		SeamStartAttribute,
		SeamEndAttribute,
		PatternStartAttribute,
		PatternEndAttribute,
		PhysicsAssetPathNameAttribute,
		SkeletonAssetPathNameAttribute
	};

	// Materials Group
	static const FName RenderMaterialPathNameAttribute(TEXT("RenderMaterialPathName"));
	static const TArray<FName> MaterialsGroupAttributes =
	{
		RenderMaterialPathNameAttribute
	};

	// Tether Batches Group
	static const FName TetherStartAttribute(TEXT("TetherStart"));
	static const FName TetherEndAttribute(TEXT("TetherEnd"));
	static const TArray<FName> TetherBatchesGroupAttributes =
	{
		TetherStartAttribute,
		TetherEndAttribute
	};

	// Tethers Group
	static const FName TetherKinematicIndexAttribute(TEXT("TetherKinematicIndex"));
	static const FName TetherDynamicIndexAttribute(TEXT("TetherDynamicIndex"));
	static const FName TetherReferenceLengthAttribute(TEXT("TetherReferenceLength"));
	static const TArray<FName> TethersGroupAttributes =
	{
		TetherKinematicIndexAttribute,
		TetherDynamicIndexAttribute,
		TetherReferenceLengthAttribute
	};

	// Seam Group
	static const FName SeamPatternsAttribute(TEXT("SeamPatterns"));
	static const FName SeamStitchesAttribute(TEXT("SeamStitches"));
	static const TArray<FName> SeamsGroupAttributes =
	{
		SeamPatternsAttribute,
		SeamStitchesAttribute
	};

	// Patterns Group
	static const FName SimVerticesStartAttribute(TEXT("SimVerticesStart"));
	static const FName SimVerticesEndAttribute(TEXT("SimVerticesEnd"));
	static const FName SimFacesStartAttribute(TEXT("SimFacesStart"));
	static const FName SimFacesEndAttribute(TEXT("SimFacesEnd"));
	static const FName RenderVerticesStartAttribute(TEXT("RenderVerticesStar"));
	static const FName RenderVerticesEndAttribute(TEXT("RenderVerticesEnd"));
	static const FName RenderFacesStartAttribute(TEXT("RenderFacesStart"));
	static const FName RenderFacesEndAttribute(TEXT("RenderFacesEnd"));
	static const FName StatusFlagsAttribute(TEXT("StatusFlags"));
	static const TArray<FName> PatternsGroupAttributes =
	{
		SimVerticesStartAttribute,
		SimVerticesEndAttribute,
		SimFacesStartAttribute,
		SimFacesEndAttribute,
		RenderVerticesStartAttribute,
		RenderVerticesEndAttribute,
		RenderFacesStartAttribute,
		RenderFacesEndAttribute,
		StatusFlagsAttribute
	};

	// Sim Faces Group
	static const FName SimIndicesAttribute(TEXT("SimIndices"));
	static const TArray<FName> SimFacesGroupAttributes =
	{
		SimIndicesAttribute
	};

	// Sim Vertices Group
	static const FName SimPositionAttribute(TEXT("SimPosition"));
	static const FName SimRestPositionAttribute(TEXT("SimRestPosition"));
	static const FName SimRestNormalAttribute(TEXT("SimRestNormal"));
	static const FName SimNumBoneInfluencesAttribute(TEXT("SimNumBoneInfluences"));
	static const FName SimBoneIndicesAttribute(TEXT("SimBoneIndices"));
	static const FName SimBoneWeightsAttribute(TEXT("SimBoneWeights"));
	static const TArray<FName> SimVerticesGroupAttributes =
	{
		SimPositionAttribute,
		SimRestPositionAttribute,
		SimRestNormalAttribute,
		SimNumBoneInfluencesAttribute,
		SimBoneIndicesAttribute,
		SimBoneWeightsAttribute
	};

	// Render Faces Group
	static const FName RenderIndicesAttribute(TEXT("RenderIndices"));
	static const FName RenderMaterialIndexAttribute(TEXT("RenderMaterialIndex"));
	static const TArray<FName> RenderFacesGroupAttributes =
	{
		RenderIndicesAttribute,
		RenderMaterialIndexAttribute
	};

	// Render Vertices Group
	static const FName RenderPositionAttribute(TEXT("RenderPosition"));
	static const FName RenderNormalAttribute(TEXT("RenderNormal"));
	static const FName RenderTangentUAttribute(TEXT("RenderTangentU"));
	static const FName RenderTangentVAttribute(TEXT("RenderTangentV"));
	static const FName RenderUVsAttribute(TEXT("RenderUVs"));
	static const FName RenderColorAttribute(TEXT("RenderColor"));
	static const FName RenderNumBoneInfluencesAttribute(TEXT("RenderNumBoneInfluences"));
	static const FName RenderBoneIndicesAttribute(TEXT("RenderBoneIndices"));
	static const FName RenderBoneWeightsAttribute(TEXT("RenderBoneWeights"));
	static const TArray<FName> RenderVerticesGroupAttributes =
	{
		RenderPositionAttribute,
		RenderNormalAttribute,
		RenderTangentUAttribute,
		RenderTangentVAttribute,
		RenderUVsAttribute,
		RenderColorAttribute,
		RenderNumBoneInfluencesAttribute,
		RenderBoneIndicesAttribute,
		RenderBoneWeightsAttribute
	};

	static const TMap<FName, TArray<FName>> FixedAttributeNamesMap =
	{
		{ LodsGroup, LodsGroupAttributes },
		{ MaterialsGroup, MaterialsGroupAttributes },
		{ TetherBatchesGroup, TetherBatchesGroupAttributes },
		{ TethersGroup, TethersGroupAttributes },
		{ SeamsGroup, SeamsGroupAttributes },
		{ PatternsGroup, PatternsGroupAttributes },
		{ SimFacesGroup, SimFacesGroupAttributes },
		{ SimVerticesGroup, SimVerticesGroupAttributes },
		{ RenderFacesGroup, RenderFacesGroupAttributes },
		{ RenderVerticesGroup, RenderVerticesGroupAttributes }
	};
}  // End namespace UE::Chaos::ClothAsset::Private

namespace UE::Chaos::ClothAsset
{
	// Groups
	const FName FClothCollection::LodsGroup = Private::LodsGroup;
	const FName FClothCollection::MaterialsGroup = Private::MaterialsGroup;
	const FName FClothCollection::TetherBatchesGroup = Private::TetherBatchesGroup;
	const FName FClothCollection::TethersGroup = Private::TethersGroup;
	const FName FClothCollection::SeamsGroup = Private::SeamsGroup;
	const FName FClothCollection::PatternsGroup = Private::PatternsGroup;
	const FName FClothCollection::SimFacesGroup = Private::SimFacesGroup;
	const FName FClothCollection::SimVerticesGroup = Private::SimVerticesGroup;
	const FName FClothCollection::RenderFacesGroup = Private::RenderFacesGroup;
	const FName FClothCollection::RenderVerticesGroup = Private::RenderVerticesGroup;

	FClothCollection::FClothCollection(const TSharedPtr<FManagedArrayCollection>& InManagedArrayCollection)
		: ManagedArrayCollection(InManagedArrayCollection)
	{
		using namespace UE::Chaos::ClothAsset::Private;

		check(ManagedArrayCollection.IsValid());

		// LODs Group
		MaterialStart = ManagedArrayCollection->FindAttribute<int32>(MaterialStartAttribute, LodsGroup);
		MaterialEnd = ManagedArrayCollection->FindAttribute<int32>(MaterialEndAttribute, LodsGroup);
		TetherBatchStart = ManagedArrayCollection->FindAttribute<int32>(TetherBatchStartAttribute, LodsGroup);
		TetherBatchEnd = ManagedArrayCollection->FindAttribute<int32>(TetherBatchEndAttribute, LodsGroup);
		SeamStart = ManagedArrayCollection->FindAttribute<int32>(SeamStartAttribute, LodsGroup);
		SeamEnd = ManagedArrayCollection->FindAttribute<int32>(SeamEndAttribute, LodsGroup);
		PatternStart = ManagedArrayCollection->FindAttribute<int32>(PatternStartAttribute, LodsGroup);
		PatternEnd = ManagedArrayCollection->FindAttribute<int32>(PatternEndAttribute, LodsGroup);
		PhysicsAssetPathName = ManagedArrayCollection->FindAttribute<FString>(PhysicsAssetPathNameAttribute, LodsGroup);
		SkeletonAssetPathName = ManagedArrayCollection->FindAttribute<FString>(SkeletonAssetPathNameAttribute, LodsGroup);

		// Materials Group
		RenderMaterialPathName = ManagedArrayCollection->FindAttribute<FString>(RenderMaterialPathNameAttribute, MaterialsGroup);

		// Tether Batches Group
		TetherStart = ManagedArrayCollection->FindAttribute<int32>(TetherStartAttribute, TetherBatchesGroup);
		TetherEnd = ManagedArrayCollection->FindAttribute<int32>(TetherEndAttribute, TetherBatchesGroup);

		// Tethers Group
		TetherKinematicIndex = ManagedArrayCollection->FindAttribute<int32>(TetherKinematicIndexAttribute, TethersGroup);
		TetherDynamicIndex = ManagedArrayCollection->FindAttribute<int32>(TetherDynamicIndexAttribute, TethersGroup);
		TetherReferenceLength = ManagedArrayCollection->FindAttribute<float>(TetherReferenceLengthAttribute, TethersGroup);

		// Seam Group
		SeamPatterns = ManagedArrayCollection->FindAttribute<FIntVector2>(SeamPatternsAttribute, SeamsGroup);
		SeamStitches = ManagedArrayCollection->FindAttribute<TArray<FIntVector2>>(SeamStitchesAttribute, SeamsGroup);

		// Patterns Group
		SimVerticesStart = ManagedArrayCollection->FindAttribute<int32>(SimVerticesStartAttribute, PatternsGroup);
		SimVerticesEnd = ManagedArrayCollection->FindAttribute<int32>(SimVerticesEndAttribute, PatternsGroup);
		SimFacesStart = ManagedArrayCollection->FindAttribute<int32>(SimFacesStartAttribute, PatternsGroup);
		SimFacesEnd = ManagedArrayCollection->FindAttribute<int32>(SimFacesEndAttribute, PatternsGroup);
		RenderVerticesStart = ManagedArrayCollection->FindAttribute<int32>(RenderVerticesStartAttribute, PatternsGroup);
		RenderVerticesEnd = ManagedArrayCollection->FindAttribute<int32>(RenderVerticesEndAttribute, PatternsGroup);
		RenderFacesStart = ManagedArrayCollection->FindAttribute<int32>(RenderFacesStartAttribute, PatternsGroup);
		RenderFacesEnd = ManagedArrayCollection->FindAttribute<int32>(RenderFacesEndAttribute, PatternsGroup);
		StatusFlags = ManagedArrayCollection->FindAttribute<int32>(StatusFlagsAttribute, PatternsGroup);

		// Sim Faces Group
		SimIndices = ManagedArrayCollection->FindAttribute<FIntVector3>(SimIndicesAttribute, SimFacesGroup);

		// Sim Vertices Group
		SimPosition = ManagedArrayCollection->FindAttribute<FVector2f>(SimPositionAttribute, SimVerticesGroup);
		SimRestPosition = ManagedArrayCollection->FindAttribute<FVector3f>(SimRestPositionAttribute, SimVerticesGroup);
		SimRestNormal = ManagedArrayCollection->FindAttribute<FVector3f>(SimRestNormalAttribute, SimVerticesGroup);
		SimNumBoneInfluences = ManagedArrayCollection->FindAttribute<int32>(SimNumBoneInfluencesAttribute, SimVerticesGroup);
		SimBoneIndices = ManagedArrayCollection->FindAttribute<TArray<int32>>(SimBoneIndicesAttribute, SimVerticesGroup);
		SimBoneWeights = ManagedArrayCollection->FindAttribute<TArray<float>>(SimBoneWeightsAttribute, SimVerticesGroup);

		// Render Faces Group
		RenderIndices = ManagedArrayCollection->FindAttribute<FIntVector3>(RenderIndicesAttribute, RenderFacesGroup);
		RenderMaterialIndex = ManagedArrayCollection->FindAttribute<int32>(RenderMaterialIndexAttribute, RenderFacesGroup);

		// Render Vertices Group
		RenderPosition = ManagedArrayCollection->FindAttribute<FVector3f>(RenderPositionAttribute, RenderVerticesGroup);
		RenderNormal = ManagedArrayCollection->FindAttribute<FVector3f>(RenderNormalAttribute, RenderVerticesGroup);
		RenderTangentU = ManagedArrayCollection->FindAttribute<FVector3f>(RenderTangentUAttribute, RenderVerticesGroup);
		RenderTangentV = ManagedArrayCollection->FindAttribute<FVector3f>(RenderTangentVAttribute, RenderVerticesGroup);
		RenderUVs = ManagedArrayCollection->FindAttribute<TArray<FVector2f>>(RenderUVsAttribute, RenderVerticesGroup);
		RenderColor = ManagedArrayCollection->FindAttribute<FLinearColor>(RenderColorAttribute, RenderVerticesGroup);
		RenderNumBoneInfluences = ManagedArrayCollection->FindAttribute<int32>(RenderNumBoneInfluencesAttribute, RenderVerticesGroup);
		RenderBoneIndices = ManagedArrayCollection->FindAttribute<TArray<int32>>(RenderBoneIndicesAttribute, RenderVerticesGroup);
		RenderBoneWeights = ManagedArrayCollection->FindAttribute<TArray<float>>(RenderBoneWeightsAttribute, RenderVerticesGroup);
	}

	bool FClothCollection::IsValid() const
	{
		return 
			// LODs Group
			MaterialStart &&
			MaterialEnd &&
			TetherBatchStart &&
			TetherBatchEnd &&
			SeamStart &&
			SeamEnd &&
			PatternStart &&
			PatternEnd &&
			PhysicsAssetPathName &&
			SkeletonAssetPathName &&

			// Materials Group
			RenderMaterialPathName &&

			// Tether Batches Group
			TetherStart &&
			TetherEnd &&

			// Tethers Group
			TetherKinematicIndex &&
			TetherDynamicIndex &&
			TetherReferenceLength &&

			// Seam Group
			SeamPatterns &&
			SeamStitches &&

			// Patterns Group
			SimVerticesStart &&
			SimVerticesEnd &&
			SimFacesStart &&
			SimFacesEnd &&
			RenderVerticesStart &&
			RenderVerticesEnd &&
			RenderFacesStart &&
			RenderFacesEnd &&
			StatusFlags &&

			// Sim Faces Group
			SimIndices &&

			// Sim Vertices Group
			SimPosition &&
			SimRestPosition &&
			SimRestNormal &&
			SimNumBoneInfluences &&
			SimBoneIndices &&
			SimBoneWeights &&

			// Render Faces Group
			RenderIndices &&
			RenderMaterialIndex &&

			// Render Vertices Group
			RenderPosition &&
			RenderNormal &&
			RenderTangentU &&
			RenderTangentV &&
			RenderUVs &&
			RenderColor &&
			RenderNumBoneInfluences &&
			RenderBoneIndices &&
			RenderBoneWeights;
	}

	void FClothCollection::DefineSchema()
	{
		using namespace UE::Chaos::ClothAsset::Private;

		// Dependencies
		FManagedArrayCollection::FConstructionParameters MaterialsDependency(MaterialsGroup);
		FManagedArrayCollection::FConstructionParameters TetherBatchesDependency(TetherBatchesGroup);
		FManagedArrayCollection::FConstructionParameters TethersDependency(TethersGroup);
		FManagedArrayCollection::FConstructionParameters SeamsDependency(SeamsGroup);
		FManagedArrayCollection::FConstructionParameters PatternsDependency(PatternsGroup);
		FManagedArrayCollection::FConstructionParameters RenderFacesDependency(RenderFacesGroup);
		FManagedArrayCollection::FConstructionParameters RenderVerticesDependency(RenderVerticesGroup);
		FManagedArrayCollection::FConstructionParameters SimFacesDependency(SimFacesGroup);
		FManagedArrayCollection::FConstructionParameters SimVerticesDependency(SimVerticesGroup);

		// LODs Group
		PatternStart = &ManagedArrayCollection->AddAttribute<int32>(PatternStartAttribute, LodsGroup, PatternsDependency);
		PatternEnd = &ManagedArrayCollection->AddAttribute<int32>(PatternEndAttribute, LodsGroup, PatternsDependency);
		SeamStart = &ManagedArrayCollection->AddAttribute<int32>(SeamStartAttribute, LodsGroup, SeamsDependency);
		SeamEnd = &ManagedArrayCollection->AddAttribute<int32>(SeamEndAttribute, LodsGroup, SeamsDependency);
		TetherBatchStart = &ManagedArrayCollection->AddAttribute<int32>(TetherBatchStartAttribute, LodsGroup, TetherBatchesDependency);
		TetherBatchEnd = &ManagedArrayCollection->AddAttribute<int32>(TetherBatchEndAttribute, LodsGroup, TetherBatchesDependency);
		MaterialStart = &ManagedArrayCollection->AddAttribute<int32>(MaterialStartAttribute, LodsGroup, MaterialsDependency);
		MaterialEnd = &ManagedArrayCollection->AddAttribute<int32>(MaterialEndAttribute, LodsGroup, MaterialsDependency);
		PhysicsAssetPathName = &ManagedArrayCollection->AddAttribute<FString>(PhysicsAssetPathNameAttribute, LodsGroup);
		SkeletonAssetPathName = &ManagedArrayCollection->AddAttribute<FString>(SkeletonAssetPathNameAttribute, LodsGroup);

		// Materials Group
		RenderMaterialPathName = &ManagedArrayCollection->AddAttribute<FString>(RenderMaterialPathNameAttribute, MaterialsGroup);

		// Tether Batches Group
		TetherStart = &ManagedArrayCollection->AddAttribute<int32>(TetherStartAttribute, TetherBatchesGroup, TethersDependency);
		TetherEnd = &ManagedArrayCollection->AddAttribute<int32>(TetherEndAttribute, TetherBatchesGroup, TethersDependency);

		// Tethers Group
		TetherKinematicIndex = &ManagedArrayCollection->AddAttribute<int32>(TetherKinematicIndexAttribute, TethersGroup, SimVerticesDependency);
		TetherDynamicIndex = &ManagedArrayCollection->AddAttribute<int32>(TetherDynamicIndexAttribute, TethersGroup, SimVerticesDependency);
		TetherReferenceLength = &ManagedArrayCollection->AddAttribute<float>(TetherReferenceLengthAttribute, TethersGroup);

		// Seams Group
		SeamPatterns = &ManagedArrayCollection->AddAttribute<FIntVector2>(SeamPatternsAttribute, SeamsGroup, SimVerticesDependency);
		SeamStitches = &ManagedArrayCollection->AddAttribute<TArray<FIntVector2>>(SeamStitchesAttribute, SeamsGroup, SimVerticesDependency);

		// Patterns Group
		SimVerticesStart = &ManagedArrayCollection->AddAttribute<int32>(SimVerticesStartAttribute, PatternsGroup, SimVerticesDependency);
		SimVerticesEnd = &ManagedArrayCollection->AddAttribute<int32>(SimVerticesEndAttribute, PatternsGroup, SimVerticesDependency);
		SimFacesStart = &ManagedArrayCollection->AddAttribute<int32>(SimFacesStartAttribute, PatternsGroup, SimFacesDependency);
		SimFacesEnd = &ManagedArrayCollection->AddAttribute<int32>(SimFacesEndAttribute, PatternsGroup, SimFacesDependency);
		RenderVerticesStart = &ManagedArrayCollection->AddAttribute<int32>(RenderVerticesStartAttribute, PatternsGroup, RenderVerticesDependency);
		RenderVerticesEnd = &ManagedArrayCollection->AddAttribute<int32>(RenderVerticesEndAttribute, PatternsGroup, RenderVerticesDependency);
		RenderFacesStart = &ManagedArrayCollection->AddAttribute<int32>(RenderFacesStartAttribute, PatternsGroup, RenderFacesDependency);
		RenderFacesEnd = &ManagedArrayCollection->AddAttribute<int32>(RenderFacesEndAttribute, PatternsGroup, RenderFacesDependency);
		StatusFlags = &ManagedArrayCollection->AddAttribute<int32>(StatusFlagsAttribute, PatternsGroup);

		// Sim Faces Group
		SimIndices = &ManagedArrayCollection->AddAttribute<FIntVector3>(SimIndicesAttribute, SimFacesGroup, SimVerticesDependency);

		// Sim Vertices Group
		SimPosition = &ManagedArrayCollection->AddAttribute<FVector2f>(SimPositionAttribute, SimVerticesGroup);
		SimRestPosition = &ManagedArrayCollection->AddAttribute<FVector3f>(SimRestPositionAttribute, SimVerticesGroup);
		SimRestNormal = &ManagedArrayCollection->AddAttribute<FVector3f>(SimRestNormalAttribute, SimVerticesGroup);
		SimNumBoneInfluences = &ManagedArrayCollection->AddAttribute<int32>(SimNumBoneInfluencesAttribute, SimVerticesGroup);
		SimBoneIndices = &ManagedArrayCollection->AddAttribute<TArray<int32>>(SimBoneIndicesAttribute, SimVerticesGroup);
		SimBoneWeights = &ManagedArrayCollection->AddAttribute<TArray<float>>(SimBoneWeightsAttribute, SimVerticesGroup);

		// Render Faces Group
		RenderIndices = &ManagedArrayCollection->AddAttribute<FIntVector3>(RenderIndicesAttribute, RenderFacesGroup, RenderVerticesDependency);
		RenderMaterialIndex = &ManagedArrayCollection->AddAttribute<int32>(RenderMaterialIndexAttribute, RenderFacesGroup);

		// Render Vertices Group
		RenderPosition = &ManagedArrayCollection->AddAttribute<FVector3f>(RenderPositionAttribute, RenderVerticesGroup);
		RenderNormal = &ManagedArrayCollection->AddAttribute<FVector3f>(RenderNormalAttribute, RenderVerticesGroup);
		RenderTangentU = &ManagedArrayCollection->AddAttribute<FVector3f>(RenderTangentUAttribute, RenderVerticesGroup);
		RenderTangentV = &ManagedArrayCollection->AddAttribute<FVector3f>(RenderTangentVAttribute, RenderVerticesGroup);
		RenderUVs = &ManagedArrayCollection->AddAttribute<TArray<FVector2f>>(RenderUVsAttribute, RenderVerticesGroup);
		RenderColor = &ManagedArrayCollection->AddAttribute<FLinearColor>(RenderColorAttribute, RenderVerticesGroup);
		RenderNumBoneInfluences = &ManagedArrayCollection->AddAttribute<int32>(RenderNumBoneInfluencesAttribute, RenderVerticesGroup);
		RenderBoneIndices = &ManagedArrayCollection->AddAttribute<TArray<int32>>(RenderBoneIndicesAttribute, RenderVerticesGroup);
		RenderBoneWeights = &ManagedArrayCollection->AddAttribute<TArray<float>>(RenderBoneWeightsAttribute, RenderVerticesGroup);
	}

	int32 FClothCollection::GetNumElements(const FName& GroupName) const
	{
		return ManagedArrayCollection->NumElements(GroupName);
	}

	int32 FClothCollection::GetNumElements(const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 ArrayIndex) const
	{
		if (StartArray && StartArray->GetConstArray().IsValidIndex(ArrayIndex) &&
			EndArray && EndArray->GetConstArray().IsValidIndex(ArrayIndex))
		{
			const int32 Start = (*StartArray)[ArrayIndex];
			const int32 End = (*EndArray)[ArrayIndex];
			if (Start != INDEX_NONE && End != INDEX_NONE)
			{
				return End - Start + 1;
			}
			checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		}
		return 0;
	}

	void FClothCollection::SetNumElements(int32 InNumElements, const FName& GroupName)
	{
		check(IsValid());
		check(InNumElements >= 0);
		
		const int32 NumElements = ManagedArrayCollection->NumElements(GroupName);

		if (const int32 Delta = InNumElements - NumElements)
		{
			if (Delta > 0)
			{
				ManagedArrayCollection->AddElements(Delta, GroupName);
			}
			else
			{
				ManagedArrayCollection->RemoveElements(GroupName, -Delta, InNumElements);
			}
		}
	}

	int32 FClothCollection::SetNumElements(int32 InNumElements, const FName& GroupName, TManagedArray<int32>* StartArray, TManagedArray<int32>* EndArray, int32 ArrayIndex)
	{
		check(IsValid());
		check(InNumElements >= 0);

		check(StartArray && StartArray->GetConstArray().IsValidIndex(ArrayIndex));
		check(EndArray && EndArray->GetConstArray().IsValidIndex(ArrayIndex));

		int32& Start = (*StartArray)[ArrayIndex];
		int32& End = (*EndArray)[ArrayIndex];
		check(Start != INDEX_NONE || End == INDEX_NONE);  // Best to avoid situations where only one boundary of the range is set to INDEX_NONE

		const int32 NumElements = (Start == INDEX_NONE) ? 0 : End - Start + 1;

		if (const int32 Delta = InNumElements - NumElements)
		{
			if (Delta > 0)
			{
				// Find a previous valid index range to insert after when the range is empty
				auto ComputeEnd = [&EndArray, ArrayIndex]()->int32
				{
					for (int32 Index = ArrayIndex; Index >= 0; --Index)
					{
						if ((*EndArray)[Index] != INDEX_NONE)
						{
							return (*EndArray)[Index];
						}
					}
					return INDEX_NONE;
				};

				// Grow the array
				const int32 Position = ComputeEnd() + 1;
				ManagedArrayCollection->InsertElements(Delta, Position, GroupName);

				// Update Start/End
				if (!NumElements)
				{
					Start = Position;
				}
				End = Start + InNumElements - 1;
			}
			else
			{
				// Shrink the array
				const int32 Position = Start + InNumElements;
				ManagedArrayCollection->RemoveElements(GroupName, -Delta, Position);

				// Update Start/End
				if (InNumElements)
				{
					End = Position - 1;
				}
				else
				{
					End = Start = INDEX_NONE;  // It is important to set the start & end to INDEX_NONE so that they never get automatically re-indexed by the managed array collection
				}
			}
		}
		return Start;
	}

	int32 FClothCollection::GetNumSubElements(
		const TManagedArray<int32>* StartArray,
		const TManagedArray<int32>* EndArray,
		const TManagedArray<int32>* StartSubArray,
		const TManagedArray<int32>* EndSubArray,
		int32 ArrayIndex) const
	{
		const TTuple<int32, int32> StartEnd = GetSubElementsStartEnd(StartArray, EndArray, StartSubArray, EndSubArray, ArrayIndex);
		const int32 Start = StartEnd.Get<0>();
		const int32 End = StartEnd.Get<1>();
		if (Start != INDEX_NONE && End != INDEX_NONE)
		{
			return End - Start + 1;
		}
		checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		return 0;
	}

	template<bool bStart, bool bEnd>
	TTuple<int32, int32> FClothCollection::GetSubElementsStartEnd(
		const TManagedArray<int32>* StartArray,
		const TManagedArray<int32>* EndArray,
		const TManagedArray<int32>* StartSubArray,
		const TManagedArray<int32>* EndSubArray,
		int32 ArrayIndex) const
	{
		int32 Start = INDEX_NONE;  // Find Start and End indices for the entire LOD minding empty patterns on the way
		int32 End = INDEX_NONE;

		if (StartArray && StartArray->GetConstArray().IsValidIndex(ArrayIndex) &&
			EndArray && EndArray->GetConstArray().IsValidIndex(ArrayIndex))
		{
			const int32 SubStart = (*StartArray)[ArrayIndex];
			const int32 SubEnd = (*EndArray)[ArrayIndex];

			if (SubStart != INDEX_NONE && SubEnd != INDEX_NONE)
			{
				for (int32 SubIndex = SubStart; SubIndex <= SubEnd; ++SubIndex)
				{
					if (bStart && (*StartSubArray)[SubIndex] != INDEX_NONE)
					{
						Start = (Start == INDEX_NONE) ? (*StartSubArray)[SubIndex] : FMath::Min(Start, (*StartSubArray)[SubIndex]);
					}
					if (bEnd && (*EndSubArray)[SubIndex] != INDEX_NONE)
					{
						End = (End == INDEX_NONE) ? (*EndSubArray)[SubIndex] : FMath::Max(End, (*EndSubArray)[SubIndex]);
					}
				}
			}
			else
			{
				checkf(SubStart == SubEnd, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
			}
		}
		return TTuple<int32, int32>(Start, End);
	}
	template CHAOSCLOTHASSET_API TTuple<int32, int32> FClothCollection::GetSubElementsStartEnd<true, false>(const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 ArrayIndex) const;
	template CHAOSCLOTHASSET_API TTuple<int32, int32> FClothCollection::GetSubElementsStartEnd<false, true>(const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 ArrayIndex) const;
	template CHAOSCLOTHASSET_API TTuple<int32, int32> FClothCollection::GetSubElementsStartEnd<true, true>(const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 ArrayIndex) const;

	template<typename T, typename TEnableIf<TIsUserAttributeType<T>::Value, int>::type>
	TArray<FName> FClothCollection::GetUserDefinedAttributeNames(const FName& GroupName) const
	{
		using namespace UE::Chaos::ClothAsset::Private;

		TArray<FName> UserDefinedAttributeNames;

		const TArray<FName>& FixedAttributeNames = FixedAttributeNamesMap.FindChecked(GroupName);  // Also checks that the group name is a recognized group name 
		const TArray<FName> AttributeNames = ManagedArrayCollection->AttributeNames(GroupName);

		const int32 MaxUserDefinedAttributes = AttributeNames.Num() - FixedAttributeNames.Num();
		if (MaxUserDefinedAttributes > 0)
		{
			UserDefinedAttributeNames.Reserve(MaxUserDefinedAttributes);

			for (const FName& AttributeName : AttributeNames)
			{
				if (!FixedAttributeNames.Contains(AttributeName) && ManagedArrayCollection->FindAttributeTyped<T>(AttributeName, GroupName))
				{
					UserDefinedAttributeNames.Add(AttributeName);
				}
			}
		}
		return UserDefinedAttributeNames;
	}
	template CHAOSCLOTHASSET_API TArray<FName> FClothCollection::GetUserDefinedAttributeNames<bool>(const FName& GroupName) const;
	template CHAOSCLOTHASSET_API TArray<FName> FClothCollection::GetUserDefinedAttributeNames<int32>(const FName& GroupName) const;
	template CHAOSCLOTHASSET_API TArray<FName> FClothCollection::GetUserDefinedAttributeNames<float>(const FName& GroupName) const;
	template CHAOSCLOTHASSET_API TArray<FName> FClothCollection::GetUserDefinedAttributeNames<FVector3f>(const FName& GroupName) const;

	template<typename T, typename TEnableIf<TIsUserAttributeType<T>::Value, int>::type>
	void FClothCollection::AddUserDefinedAttribute(const FName& Name, const FName& GroupName)
	{
		ManagedArrayCollection->AddAttribute<T>(Name, GroupName);
	}
	template CHAOSCLOTHASSET_API void FClothCollection::AddUserDefinedAttribute<bool>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API void FClothCollection::AddUserDefinedAttribute<int32>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API void FClothCollection::AddUserDefinedAttribute<float>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API void FClothCollection::AddUserDefinedAttribute<FVector3f>(const FName& Name, const FName& GroupName);

	void FClothCollection::RemoveUserDefinedAttribute(const FName& Name, const FName& GroupName)
	{
		ManagedArrayCollection->RemoveAttribute(Name, GroupName);
	}

	template<typename T, typename TEnableIf<TIsUserAttributeType<T>::Value, int>::type>
	bool FClothCollection::HasUserDefinedAttribute(const FName& Name, const FName& GroupName) const
	{
		return ManagedArrayCollection->FindAttributeTyped<T>(Name, GroupName) != nullptr;
	}
	template CHAOSCLOTHASSET_API bool FClothCollection::HasUserDefinedAttribute<bool>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API bool FClothCollection::HasUserDefinedAttribute<int32>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API bool FClothCollection::HasUserDefinedAttribute<float>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API bool FClothCollection::HasUserDefinedAttribute<FVector3f>(const FName& Name, const FName& GroupName) const;

	template<typename T, typename TEnableIf<TIsUserAttributeType<T>::Value, int>::type>
	const TManagedArray<T>* FClothCollection::GetUserDefinedAttribute(const FName& Name, const FName& GroupName) const
	{
		return ManagedArrayCollection->FindAttribute<T>(Name, GroupName);
	}
	template CHAOSCLOTHASSET_API const TManagedArray<bool>* FClothCollection::GetUserDefinedAttribute<bool>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API const TManagedArray<int32>* FClothCollection::GetUserDefinedAttribute<int32>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API const TManagedArray<float>* FClothCollection::GetUserDefinedAttribute<float>(const FName& Name, const FName& GroupName) const;
	template CHAOSCLOTHASSET_API const TManagedArray<FVector3f>* FClothCollection::GetUserDefinedAttribute<FVector3f>(const FName& Name, const FName& GroupName) const;

	template<typename T, typename TEnableIf<TIsUserAttributeType<T>::Value, int>::type>
	TManagedArray<T>* FClothCollection::GetUserDefinedAttribute(const FName& Name, const FName& GroupName)
	{
		return ManagedArrayCollection->FindAttribute<T>(Name, GroupName);
	}
	template CHAOSCLOTHASSET_API TManagedArray<bool>* FClothCollection::GetUserDefinedAttribute<bool>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API TManagedArray<int32>* FClothCollection::GetUserDefinedAttribute<int32>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API TManagedArray<float>* FClothCollection::GetUserDefinedAttribute<float>(const FName& Name, const FName& GroupName);
	template CHAOSCLOTHASSET_API TManagedArray<FVector3f>* FClothCollection::GetUserDefinedAttribute<FVector3f>(const FName& Name, const FName& GroupName);
} // End namespace UE::Chaos::ClothAsset

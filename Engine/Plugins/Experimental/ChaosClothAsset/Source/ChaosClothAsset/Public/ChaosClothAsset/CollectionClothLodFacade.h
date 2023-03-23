// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/CollectionClothPatternFacade.h"

namespace UE::Geometry
{
class FDynamicMesh3;
}

namespace UE::Chaos::ClothAsset
{
	class FClothCollection;

	/**
	 * Cloth Asset collection LOD facade class to access cloth LOD data.
	 * Constructed from FCollectionClothConstFacade.
	 * Const access (read only) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothLodConstFacade
	{
	public:
		FCollectionClothLodConstFacade() = delete;

		FCollectionClothLodConstFacade(const FCollectionClothLodConstFacade&) = delete;
		FCollectionClothLodConstFacade& operator=(const FCollectionClothLodConstFacade&) = delete;

		FCollectionClothLodConstFacade(FCollectionClothLodConstFacade&&) = default;
		FCollectionClothLodConstFacade& operator=(FCollectionClothLodConstFacade&&) = default;

		virtual ~FCollectionClothLodConstFacade() = default;

		/** Return the number of materials in this LOD. */
		int32 GetNumMaterials() const;
		/** Return the number of tether batches in this LOD. */
		int32 GetNumTetherBatches() const;
		/** Return the number of seams in this LOD. */
		int32 GetNumSeams() const;
		/** Return the number of patterns in this LOD. */
		int32 GetNumPatterns() const;

		/** Return a view of all the render materials used on this LOD. */
		TConstArrayView<FString> GetRenderMaterialPathName() const;

		/** Return a view of all the seam patterns used on this LOD. */
		TConstArrayView<FIntVector2> GetSeamPatterns() const;
		/** Return a view of all the seam stitches used on this LOD. */
		TConstArrayView<TArray<FIntVector2>> GetSeamStitches() const;

		/** Return a pattern facade for the specified pattern index. */
		FCollectionClothPatternConstFacade GetPattern(int32 PatternIndex) const;

		/** Return the physics asset path names used for this LOD. */
		const FString& GetPhysicsAssetPathName() const;
		/** Return the skeleton asset path names used for this LOD. */
		const FString& GetSkeletonAssetPathName() const;

		//~ Pattern Sim Vertices Group
		/** Return the total number of simulation vertices for this LOD across all patterns. */
		int32 GetNumSimVertices() const;
		TConstArrayView<FVector2f> GetSimPosition() const;
		TConstArrayView<FVector3f> GetSimRestPosition() const;
		TConstArrayView<FVector3f> GetSimRestNormal() const;
		TConstArrayView<int32> GetSimNumBoneInfluences() const;
		TConstArrayView<TArray<int32>> GetSimBoneIndices() const;
		TConstArrayView<TArray<float>> GetSimBoneWeights() const;

		//~ Pattern Sim Faces Group
		/** Return the total number of simulation faces for this LOD across all patterns. */
		int32 GetNumSimFaces() const;
		TConstArrayView<FIntVector3> GetSimIndices() const;

		//~ Pattern Render Vertices Group
		/** Return the total number of render vertices for this LOD across all patterns. */
		int32 GetNumRenderVertices() const;
		TConstArrayView<FVector3f> GetRenderPosition() const;
		TConstArrayView<FVector3f> GetRenderNormal() const;
		TConstArrayView<FVector3f> GetRenderTangentU() const;
		TConstArrayView<FVector3f> GetRenderTangentV() const;
		TConstArrayView<TArray<FVector2f>> GetRenderUVs() const;
		TConstArrayView<FLinearColor> GetRenderColor() const;
		TConstArrayView<int32> GetRenderNumBoneInfluences() const;
		TConstArrayView<TArray<int32>> GetRenderBoneIndices() const;
		TConstArrayView<TArray<float>> GetRenderBoneWeights() const;

		//~ Pattern Sim Faces Group
		/** Return the total number of render faces for this LOD across all patterns. */
		int32 GetNumRenderFaces() const;
		TConstArrayView<FIntVector3> GetRenderIndices() const;
		TConstArrayView<int32> GetRenderMaterialIndex() const;

		/**
		 * Return a view on the specified vertex weightmap.
		 * The attribute must exist otherwise the function will assert.
		 * @see FCollectionClothFacade::AddWeightMap, GetNumSimVertices.
		 */
		TConstArrayView<float> GetWeightMap(const FName& Name) const;

		/** Return the LOD index this facade has been created with. */
		int32 GetLodIndex() const { return LodIndex; }

		/** Return the welded simulation mesh for this LOD. */
		void BuildSimulationMesh(TArray<FVector3f>& Positions, TArray<FVector3f>& Normals, TArray<uint32>& Indices, TArray<int32>& WeldingMap) const;

	protected:
		friend class FCollectionClothConstFacade;
		FCollectionClothLodConstFacade(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex);

		TSharedPtr<const FClothCollection> ClothCollection;
		int32 LodIndex;
	};

	/**
	 * Cloth Asset collection LOD facade class to access cloth LOD data.
	 * Constructed from FCollectionClothFacade.
	 * Non-const access (read/write) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothLodFacade final : public FCollectionClothLodConstFacade
	{
	public:
		FCollectionClothLodFacade() = delete;

		FCollectionClothLodFacade(const FCollectionClothLodFacade&) = delete;
		FCollectionClothLodFacade& operator=(const FCollectionClothLodFacade&) = delete;

		FCollectionClothLodFacade(FCollectionClothLodFacade&&) = default;
		FCollectionClothLodFacade& operator=(FCollectionClothLodFacade&&) = default;

		virtual ~FCollectionClothLodFacade() override = default;

		/** Remove all cloth patterns from this cloth LOD. */
		void Reset();

		/** Initialize the cloth LOD using the specified 3D triangle mesh and unwrapping it into a 2D geometry for generating the patterns. */
		void Initialize(const TArray<FVector3f>& Positions, const TArray<uint32>& Indices);

		/** Initialize the cloth LOD using another cloth collection. */
		void Initialize(const FCollectionClothLodConstFacade& Other);

		/** Initialize the cloth LOD from a DynamicMesh (possibly using UV islands to define Patterns)*/
		void Initialize(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UVChannelIndex);

		/** Set the new number of materials to this cloth LOD. */
		void SetNumMaterials(int32 NumMaterials);
		/** Set the new number of tether batches to this cloth LOD. */
		void SetNumTetherBatches(int32 NumSeams);
		/** Set the new number of seams to this cloth LOD. */
		void SetNumSeams(int32 NumSeams);
		/** Set the new number of patterns to this cloth LOD. */
		void SetNumPatterns(int32 NumPatterns);

		/** Return a view to the render material path names for this cloth LOD. */
		TArrayView<FString> GetRenderMaterialPathName();
		/** Return a view to the seam patterns for this cloth LOD. */
		TArrayView<FIntVector2> GetSeamPatterns();
		/** Return a view to the seam stitches for this cloth LOD. */
		TArrayView<TArray<FIntVector2>> GetSeamStitches();

		/** Add a new pattern to this cloth LOD and return its index in the LOD pattern list. */
		int32 AddPattern();

		/** Return a pattern facade for the specified pattern index. */
		FCollectionClothPatternFacade GetPattern(int32 PatternIndex);

		/** Add a new pattern to this cloth LOD, and return the cloth pattern facade set to its index. */
		FCollectionClothPatternFacade AddGetPattern() { return GetPattern(AddPattern()); }

		/** Set the physics asset path names used for this LOD. */
		void SetPhysicsAssetPathName(const FString& PathName);
		/** Set the skeleton asset path names used for this LOD. */
		void SetSkeletonAssetPathName(const FString& PathName);

		//~ Pattern Sim Vertices Group
		TArrayView<FVector2f> GetSimPosition();
		TArrayView<FVector3f> GetSimRestPosition();
		TArrayView<FVector3f> GetSimRestNormal();
		TArrayView<int32> GetSimNumBoneInfluences();
		TArrayView<TArray<int32>> GetSimBoneIndices();
		TArrayView<TArray<float>> GetSimBoneWeights();

		//~ Pattern Sim Faces Group
		TArrayView<FIntVector3> GetSimIndices();

		//~ Pattern Render Vertices Group
		TArrayView<FVector3f> GetRenderPosition();
		TArrayView<FVector3f> GetRenderNormal();
		TArrayView<FVector3f> GetRenderTangentU();
		TArrayView<FVector3f> GetRenderTangentV();
		TArrayView<TArray<FVector2f>> GetRenderUVs();
		TArrayView<FLinearColor> GetRenderColor();
		TArrayView<int32> GetRenderNumBoneInfluences();
		TArrayView<TArray<int32>> GetRenderBoneIndices();
		TArrayView<TArray<float>> GetRenderBoneWeights();

		//~ Pattern Sim Faces Group
		TArrayView<FIntVector3> GetRenderIndices();
		TArrayView<int32> GetRenderMaterialIndex();

		/**
		 * Return a view on the specified vertex weightmap.
		 * The attribute must exist otherwise the function will assert.
		 * @see FCollectionClothFacade::AddWeightMap, GetNumSimVertices.
		 */
		TArrayView<float> GetWeightMap(const FName& Name);

	private:
		friend class FCollectionClothFacade;
		FCollectionClothLodFacade(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex);

		void SetDefaults();

		template<bool bWeldNearlyCoincidentVertices>
		void InitializeFromDynamicMeshInternal(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UVChannelIndex);

		TSharedPtr<FClothCollection> GetClothCollection() { return ConstCastSharedPtr<FClothCollection>(ClothCollection); }
	};
}  // End namespace UE::Chaos::ClothAsset

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;

namespace UE::Chaos::ClothAsset
{
	class FClothCollection;

	/**
	 * Cloth Asset collection pattern facade class to access cloth pattern data.
	 * Constructed from FCollectionClothLodConstFacade.
	 * Const access (read only) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothPatternConstFacade
	{
	public:
		FCollectionClothPatternConstFacade() = delete;

		FCollectionClothPatternConstFacade(const FCollectionClothPatternConstFacade&) = delete;
		FCollectionClothPatternConstFacade& operator=(const FCollectionClothPatternConstFacade&) = delete;

		FCollectionClothPatternConstFacade(FCollectionClothPatternConstFacade&&) = default;
		FCollectionClothPatternConstFacade& operator=(FCollectionClothPatternConstFacade&&) = default;

		virtual ~FCollectionClothPatternConstFacade() = default;

		/** Return the pattern status flag. */
		int32 GetStatusFlags() const;

		/** Return the total number of simulation vertices for this pattern. */
		int32 GetNumSimVertices() const;
		/** Return the total number of simulation faces for this pattern. */
		int32 GetNumSimFaces() const;
		/** Return the total number of render vertices for this pattern. */
		int32 GetNumRenderVertices() const;
		/** Return the total number of render faces for this pattern. */
		int32 GetNumRenderFaces() const;

		/** Return the simulation vertices offset for this pattern in the simulation vertices for the LOD. */
		int32 GetSimVerticesOffset() const;
		/** Return the simulation faces offset for this pattern in the simulation faces for the LOD. */
		int32 GetSimFacesOffset() const;
		/** Return the render vertices offset for this pattern in the render vertices for the LOD. */
		int32 GetRenderVerticesOffset() const;
		/** Return the render faces offset for this pattern in the render faces for the LOD. */
		int32 GetRenderFacesOffset() const;

		//~ Sim Vertices Group
		// Note: Use the FCollectionClothLodConstFacade accessors instead of these for the array indices to match the SimIndices values
		TConstArrayView<FVector2f> GetSimPosition() const;
		TConstArrayView<FVector3f> GetSimRestPosition() const;
		TConstArrayView<FVector3f> GetSimRestNormal() const;
		TConstArrayView<int32> GetSimNumBoneInfluences() const;
		TConstArrayView<TArray<int32>> GetSimBoneIndices() const;
		TConstArrayView<TArray<float>> GetSimBoneWeights() const;

		//~ Sim Faces Group, note: SimIndices points to the LOD arrays, not the pattern arrays
		TConstArrayView<FIntVector3> GetSimIndices() const;

		//~ Render Vertices Group
		// Note: Use the FCollectionClothLodConstFacade accessors instead of these for the array indices to match the RenderIndices values
		TConstArrayView<FVector3f> GetRenderPosition() const;
		TConstArrayView<FVector3f> GetRenderNormal() const;
		TConstArrayView<FVector3f> GetRenderTangentU() const;
		TConstArrayView<FVector3f> GetRenderTangentV() const;
		TConstArrayView<TArray<FVector2f>> GetRenderUVs() const;
		TConstArrayView<FLinearColor> GetRenderColor() const;
		TConstArrayView<int32> GetRenderNumBoneInfluences() const;
		TConstArrayView<TArray<int32>> GetRenderBoneIndices() const;
		TConstArrayView<TArray<float>> GetRenderBoneWeights() const;

		//~ Render Faces Group, note: RenderIndices points to the LOD arrays, not the pattern arrays
		TConstArrayView<FIntVector3> GetRenderIndices() const;
		TConstArrayView<int32> GetRenderMaterialIndex() const;

		/** Return the LOD index this facade has been created with. */
		int32 GetLodIndex() const { return LodIndex; }

		/** Return the Pattern index this facade has been created with. */
		int32 GetPatternIndex() const { return PatternIndex; }

		/**
		 * Return a const view on the specified vertex weightmap.
		 * The attribute must exist otherwise the function will assert.
		 * @see FCollectionClothFacade::AddWeightMap, GetNumSimVertices.
		 */
		TConstArrayView<float> GetWeightMap(const FName& Name) const;

	protected:
		friend class FCollectionClothPatternFacade;  // For other instances access
		friend class FCollectionClothLodConstFacade;
		FCollectionClothPatternConstFacade(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex, int32 InPatternIndex);

		int32 GetBaseElementIndex() const;
		int32 GetElementIndex() const { return GetBaseElementIndex() + PatternIndex; }

		TSharedPtr<const FClothCollection> ClothCollection;
		int32 LodIndex;
		int32 PatternIndex;
	};

	/**
	 * Cloth Asset collection pattern facade class to access cloth pattern data.
	 * Constructed from FCollectionClothLodFacade.
	 * Non-const access (read/write) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothPatternFacade final : public FCollectionClothPatternConstFacade
	{
	public:
		FCollectionClothPatternFacade() = delete;

		FCollectionClothPatternFacade(const FCollectionClothPatternFacade&) = delete;
		FCollectionClothPatternFacade& operator=(const FCollectionClothPatternFacade&) = delete;

		FCollectionClothPatternFacade(FCollectionClothPatternFacade&&) = default;
		FCollectionClothPatternFacade& operator=(FCollectionClothPatternFacade&&) = default;

		virtual ~FCollectionClothPatternFacade() override = default;

		/** Remove all geometry from this cloth pattern. */
		void Reset();

		/** Initialize the cloth pattern using the specified 3D and 2D positions, and topology. */
		template<typename IndexType>
		void Initialize(const TArray<FVector2f>& Positions, const TArray<FVector3f>& RestPositions, const TArray<IndexType>& Indices);

		/** Initialize the cloth pattern using another pattern. */
		void Initialize(const FCollectionClothPatternConstFacade& Other);

		/** Grow or shrink the space reserved for simulation vertices for this pattern within the cloth collection and return its start index. */
		void SetNumSimVertices(int32 NumSimVertices);

		/** Grow or shrink the space reserved for simulation faces for this pattern within the cloth collection and return its start index. */
		void SetNumSimFaces(int32 NumSimFaces);

		/** Grow or shrink the space reserved for render vertices for this pattern within the cloth collection and return its start index. */
		void SetNumRenderVertices(int32 NumRenderVertices);

		/** Grow or shrink the space reserved for render faces for this pattern within the cloth collection and return its start index. */
		void SetNumRenderFaces(int32 NumRenderFaces);

		/** Set the pattern status flag. */
		void SetStatusFlags(int32 StatusFlags);

		//~ Sim Vertices Group
		// Note: Use the FCollectionClothLodFacade accessors instead of these for the array indices to match the SimIndices values
		TArrayView<FVector2f> GetSimPosition();
		TArrayView<FVector3f> GetSimRestPosition();
		TArrayView<FVector3f> GetSimRestNormal();
		TArrayView<int32> GetSimNumBoneInfluences();
		TArrayView<TArray<int32>> GetSimBoneIndices();
		TArrayView<TArray<float>> GetSimBoneWeights();

		//~ Sim Faces Group
		// Note: SimIndices points to the LOD arrays, not the pattern arrays
		TArrayView<FIntVector3> GetSimIndices();

		//~ Render Vertices Group
		// Note: Use the FCollectionClothLodFacade accessors instead of these for the array indices to match the RenderIndices values
		TArrayView<FVector3f> GetRenderPosition();
		TArrayView<FVector3f> GetRenderNormal();
		TArrayView<FVector3f> GetRenderTangentU();
		TArrayView<FVector3f> GetRenderTangentV();
		TArrayView<TArray<FVector2f>> GetRenderUVs();
		TArrayView<FLinearColor> GetRenderColor();
		TArrayView<int32> GetRenderNumBoneInfluences();
		TArrayView<TArray<int32>> GetRenderBoneIndices();
		TArrayView<TArray<float>> GetRenderBoneWeights();

		//~ Render Faces Group
		// Note: RenderIndices points to the LOD arrays, not the pattern arrays
		TArrayView<FIntVector3> GetRenderIndices();
		TArrayView<int32> GetRenderMaterialIndex();

		/**
		 * Return a view on the specified vertex weightmap.
		 * The attribute must exist otherwise the function will assert.
		 * @see FCollectionClothFacade::AddWeightMap, GetNumSimVertices.
		 */
		TArrayView<float> GetWeightMap(const FName& Name);

	protected:
		friend class FCollectionClothLodFacade;
		FCollectionClothPatternFacade(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex, int32 InPatternIndex);

		void SetDefaults();

		TSharedPtr<FClothCollection> GetClothCollection() { return ConstCastSharedPtr<FClothCollection>(ClothCollection); }
	};
}  // End namespace UE::Chaos::ClothAsset

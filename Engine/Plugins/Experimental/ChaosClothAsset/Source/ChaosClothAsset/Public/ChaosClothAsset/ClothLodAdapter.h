// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothPatternAdapter.h"

namespace UE::Chaos::ClothAsset
{
	/**
	 * Cloth LOD adapter const object to provide a more convenient object oriented access to the cloth collection.
	 */
	class CHAOSCLOTHASSET_API FClothLodConstAdapter
	{
	public:
		FClothLodConstAdapter(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex);
		FClothLodConstAdapter(const FClothPatternConstAdapter& ClothPatternConstAdapter);

		virtual ~FClothLodConstAdapter() = default;

		FClothLodConstAdapter(const FClothLodConstAdapter& Other) : ClothCollection(Other.ClothCollection), LodIndex(Other.LodIndex) {}
		FClothLodConstAdapter(FClothLodConstAdapter&& Other) : ClothCollection(MoveTemp(Other.ClothCollection)), LodIndex(Other.LodIndex) {}
		FClothLodConstAdapter& operator=(const FClothLodConstAdapter& Other) { ClothCollection = Other.ClothCollection; LodIndex = Other.LodIndex; return *this; }
		FClothLodConstAdapter& operator=(FClothLodConstAdapter&& Other) { ClothCollection = MoveTemp(Other.ClothCollection); LodIndex = Other.LodIndex; return *this; }

		FClothPatternConstAdapter GetPattern(int32  PatternIndex) const;

		int32 GetNumStitchings() const { return GetNumElements(ClothCollection->StitchingStart, ClothCollection->StitchingEnd); }
		int32 GetNumTetherBatches() const { return GetNumElements(ClothCollection->TetherBatchStart, ClothCollection->TetherBatchEnd); }

		// Patterns Group
		int32 GetNumPatterns() const { return GetNumElements(ClothCollection->PatternStart, ClothCollection->PatternEnd); }
		TConstArrayView<int32> GetSimVerticesStart() const { return GetElements(GetClothCollection()->SimVerticesStart, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetSimVerticesEnd() const { return GetElements(GetClothCollection()->SimVerticesEnd, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetSimFacesStart() const { return GetElements(GetClothCollection()->SimFacesStart, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetSimFacesEnd() const { return GetElements(GetClothCollection()->SimFacesEnd, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetRenderVerticesStart() const { return GetElements(GetClothCollection()->RenderVerticesStart, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetRenderVerticesEnd() const { return GetElements(GetClothCollection()->RenderVerticesEnd, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetRenderFacesStart() const { return GetElements(GetClothCollection()->RenderFacesStart, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }
		TConstArrayView<int32> GetRenderFacesEnd() const { return GetElements(GetClothCollection()->RenderFacesEnd, GetClothCollection()->PatternStart, GetClothCollection()->PatternEnd); }

		// Patterns Sim Vertices Group, use these to access all pattern at once, and when using the SimIndices
		int32 GetPatternsSimVerticesStart() const { return GetPatternsElementsStartEnd<true, false>(GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd).Get<0>(); }
		int32 GetPatternsSimVerticesEnd() const { return GetPatternsElementsStartEnd<false, true>(GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd).Get<1>(); }
		TTuple<int32, int32> GetPatternsSimVerticesRange() const { return GetPatternsElementsStartEnd<true, true>(GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }
		int32 GetPatternsNumSimVertices() const { return GetPatternsNumElements(GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }

		TConstArrayView<FVector2f> GetPatternsSimPosition() const { return GetPatternsElements(GetClothCollection()->SimPosition, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }
		TConstArrayView<FVector3f> GetPatternsSimRestPosition() const { return GetPatternsElements(GetClothCollection()->SimRestPosition, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }
		TConstArrayView<FVector3f> GetPatternsSimRestNormal() const { return GetPatternsElements(GetClothCollection()->SimRestNormal, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }

		// Patterns Render Vertices Group, use these to access all pattern at once, and when using the RenderIndices
		int32 GetPatternsRenderVerticesStart() const { return GetPatternsElementsStartEnd<true, false>(GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd).Get<0>(); }
		int32 GetPatternsRenderVerticesEnd() const { return GetPatternsElementsStartEnd<false, true>(GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd).Get<1>(); }
		TTuple<int32, int32> GetPatternsRenderVerticesRange() const { return GetPatternsElementsStartEnd<true, true>(GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		int32 GetPatternsNumRenderVertices() const { return GetPatternsNumElements(GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }

		TConstArrayView<FVector3f> GetPatternsRenderPosition() const { return GetPatternsElements(GetClothCollection()->RenderPosition, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TConstArrayView<FVector3f> GetPatternsRenderNormal() const { return GetPatternsElements(GetClothCollection()->RenderNormal, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TConstArrayView<FVector3f> GetPatternsRenderTangentU() const { return GetPatternsElements(GetClothCollection()->RenderTangentU, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TConstArrayView<FVector3f> GetPatternsRenderTangentV() const { return GetPatternsElements(GetClothCollection()->RenderTangentV, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TConstArrayView<TArray<FVector2f>> GePatternstRenderUVs() const { return GetPatternsElements(GetClothCollection()->RenderUVs, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TConstArrayView<FLinearColor> GetPatternsRenderColor() const { return GetPatternsElements(GetClothCollection()->RenderColor, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }

		/** Return the element index within the cloth collection. */
		int32 GetElementIndex() const { return LodIndex; }

		/** Return the LOD index this adapter has been created with. */
		int32 GetLodIndex() const { return LodIndex; }

		/** Return the underlaying cloth collection this adapter has been created with. */
		const TSharedPtr<const FClothCollection>& GetClothCollection() const { return ClothCollection; }

	protected:
		template<bool bStart, bool bEnd>
		TTuple<int32, int32> GetPatternsElementsStartEnd(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const;

	private:
		int32 GetNumElements(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const;
		template<typename T>
		TConstArrayView<T> GetElements(const TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const;

		int32 GetPatternsNumElements(const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const;
		template<typename T>
		TConstArrayView<T> GetPatternsElements(const TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray) const;

		TSharedPtr<const FClothCollection> ClothCollection;
		int32 LodIndex;
	};

	/**
	 * Cloth LOD adapter object to provide a more convenient object oriented access to the cloth collection.
	 */
	class CHAOSCLOTHASSET_API FClothLodAdapter : public FClothLodConstAdapter
	{
	public:
		FClothLodAdapter(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex);
		virtual ~FClothLodAdapter() override = default;

		FClothLodAdapter(const FClothLodAdapter& Other) : FClothLodConstAdapter(Other) {}
		FClothLodAdapter(FClothLodAdapter&& Other) : FClothLodConstAdapter(MoveTemp(Other)) {}
		FClothLodAdapter& operator=(const FClothLodAdapter& Other) { FClothLodConstAdapter::operator=(Other); return *this; }
		FClothLodAdapter& operator=(FClothLodAdapter&& Other) { FClothLodConstAdapter::operator=(MoveTemp(Other)); return *this; }

		/** Add a new pattern to this cloth LOD. */
		int32 AddPattern();

		/** Return the specified pattern. */
		FClothPatternAdapter GetPattern(int32 PatternIndex);

		/** Add a new pattern to this cloth LOD, and return the cloth pattern adapter set to its index. */
		FClothPatternAdapter AddGetPattern() { return GetPattern(AddPattern()); }

		/** Remove all cloth patterns from this cloth LOD. */
		void Reset();

		/** Initialize the cloth LOD using the specified 3D triangle mesh and unwrapping it into a 2D geometry for generating the patterns. */
		void Initialize(const TArray<FVector3f>& Positions, const TArray<uint32>& Indices);

		/** Return the underlaying Cloth Collection this adapter has been created with. */
		TSharedPtr<FClothCollection> GetClothCollection() { return ConstCastSharedPtr<FClothCollection>(FClothLodConstAdapter::GetClothCollection()); }

		// Patterns Sim Vertices Group, use these to access all pattern at once, and when using the SimIndices
		TArrayView<FVector2f> GetPatternsSimPosition() { return GetPatternsElements(GetClothCollection()->SimPosition, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }
		TArrayView<FVector3f> GetPatternsSimRestPosition() { return GetPatternsElements(GetClothCollection()->SimRestPosition, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }
		TArrayView<FVector3f> GetPatternsSimRestNormal() { return GetPatternsElements(GetClothCollection()->SimRestNormal, GetClothCollection()->SimVerticesStart, GetClothCollection()->SimVerticesEnd); }

		// Patterns Render Vertices Group, use these to access all pattern at once, and when using the RenderIndices
		TArrayView<FVector3f> GetPatternsRenderPosition() { return GetPatternsElements(GetClothCollection()->RenderPosition, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TArrayView<FVector3f> GetPatternsRenderNormal() { return GetPatternsElements(GetClothCollection()->RenderNormal, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TArrayView<FVector3f> GetPatternsRenderTangentU() { return GetPatternsElements(GetClothCollection()->RenderTangentU, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TArrayView<FVector3f> GetPatternsRenderTangentV() { return GetPatternsElements(GetClothCollection()->RenderTangentV, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TArrayView<TArray<FVector2f>> GePatternstRenderUVs() { return GetPatternsElements(GetClothCollection()->RenderUVs, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }
		TArrayView<FLinearColor> GetPatternsRenderColor() { return GetPatternsElements(GetClothCollection()->RenderColor, GetClothCollection()->RenderVerticesStart, GetClothCollection()->RenderVerticesEnd); }

	private:
		friend class FClothAdapter;

		void SetDefaults();

		template<typename T>
		TArrayView<T> GetPatternsElements(TManagedArray<T>& ElementArray, const TManagedArray<int32>& StartArray, const TManagedArray<int32>& EndArray);
	};
}  // End namespace UE::Chaos::ClothAsset

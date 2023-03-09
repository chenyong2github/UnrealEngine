// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/ManagedArray.h"

struct FManagedArrayCollection;
template<class InElementType> class TManagedArray;

namespace UE::Chaos::ClothAsset
{
	/** User defined attribute types. */
	template<typename T> struct TIsUserAttributeType { static constexpr bool Value = false; };
	template<> struct TIsUserAttributeType<bool> { static constexpr bool Value = true; };
	template<> struct TIsUserAttributeType<int32> { static constexpr bool Value = true; };
	template<> struct TIsUserAttributeType<float> { static constexpr bool Value = true; };
	template<> struct TIsUserAttributeType<FVector3f> { static constexpr bool Value = true; };

	/**
	 * Cloth collection facade data.
	 */
	class FClothCollection final
	{
	public:
		static const FName LodsGroup;  // LOD information
		static const FName MaterialsGroup;  // Materials information
		static const FName TetherBatchesGroup;  // Tethers parallel batch processing information
		static const FName TethersGroup;  // Tethers information
		static const FName SeamsGroup;  // Contains pairs of stitched sim vertex indices and matching pattern indices
		static const FName PatternsGroup;  // Contains pattern relationships to other groups
		static const FName SimFacesGroup;  // Contains indices to sim vertex
		static const FName SimVerticesGroup;  // Contains patterns' 2D positions, 3D draped position (rest)
		static const FName RenderFacesGroup;  // Contains indices to render vertex
		static const FName RenderVerticesGroup;  // Contains pattern's 3D render model

		explicit FClothCollection(const TSharedPtr<FManagedArrayCollection>& InManagedArrayCollection);
		~FClothCollection() = default;

		FClothCollection(const FClothCollection&) = delete;
		FClothCollection& operator=(const FClothCollection&) = delete;
		FClothCollection(FClothCollection&&) = delete;
		FClothCollection& operator=(FClothCollection&&) = delete;

		/** Return whether the underlying collection is a valid cloth collection. */
		bool IsValid() const;

		/** Make the underlying collection a cloth collection. */
		void DefineSchema();

		/** Get the number of elements of a group. */
		int32 GetNumElements(const FName& GroupName) const;

		/** Get the number of elements of one of the sub groups that have start/end indices. */
		int32 GetNumElements(const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 Index) const;

		/** Set the number of elements of a group. */
		void SetNumElements(int32 InNumElements, const FName& GroupName);

		/** Set the number of elements to one of the sub groups while maintaining the correct order of the data, and return the first index of the range. */
		int32 SetNumElements(int32 InNumElements, const FName& GroupName, TManagedArray<int32>* StartArray, TManagedArray<int32>* EndArray, int32 Index);

		template<typename T>
		inline TConstArrayView<T> GetElements(const TManagedArray<T>* ElementArray, const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 ArrayIndex) const;

		template<typename T>
		inline TArrayView<T> GetElements(TManagedArray<T>* ElementArray, const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 ArrayIndex);

		/**
		 * Return the difference between the start index of an element to the start index of the first sub-element in the group (base).
		 * Usefull for getting back and forth between LOD/pattern indexation modes.
		 */
		int32 GetElementsOffset(const TManagedArray<int32>* StartArray, int32 BaseElementIndex, int32 ElementIndex) const { return (*StartArray)[ElementIndex] - (*StartArray)[BaseElementIndex]; }

		int32 GetNumSubElements(
			const TManagedArray<int32>* StartArray,
			const TManagedArray<int32>* EndArray,
			const TManagedArray<int32>* StartSubArray,
			const TManagedArray<int32>* EndSubArray,
			int32 LodIndex) const;

		UE_DEPRECATED(5.3, "Use GetNumSubElements instead.")
		int32 GetPatternsNumElements(const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 LodIndex) const
		{
			return GetNumSubElements( PatternStart, PatternEnd, StartSubArray, EndSubArray, LodIndex);
		}

		template<typename T>
		inline TConstArrayView<T> GetSubElements(
			const TManagedArray<T>* SubElementArray,
			const TManagedArray<int32>* StartArray,
			const TManagedArray<int32>* EndArray,
			const TManagedArray<int32>* StartSubArray,
			const TManagedArray<int32>* EndSubArray,
			int32 ArrayIndex) const;

		template<typename T>
		UE_DEPRECATED(5.3, "Use GetSubElements instead.")
		TConstArrayView<T> GetPatternElements(const TManagedArray<T>* SubElementArray, const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 LodIndex) const
		{
			return GetSubElements(SubElementArray, PatternStart, PatternEnd, StartSubArray, EndSubArray,LodIndex);
		}

		template<typename T>
		inline TArrayView<T> GetSubElements(
			TManagedArray<T>* SubElementArray,
			const TManagedArray<int32>* StartArray,
			const TManagedArray<int32>* EndArray,
			const TManagedArray<int32>* StartSubArray,
			const TManagedArray<int32>* EndSubArray,
			int32 ArrayIndex);

		template<typename T>
		UE_DEPRECATED(5.3, "Use GetSubElements instead.")
		TArrayView<T> GetPatternElements(TManagedArray<T>* SubElementArray, const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 LodIndex)
		{
			return GetSubElements(SubElementArray, PatternStart, PatternEnd, StartSubArray, EndSubArray, LodIndex);
		}

		template<bool bStart = true, bool bEnd = true>
		TTuple<int32, int32> GetSubElementsStartEnd(
			const TManagedArray<int32>* StartArray,
			const TManagedArray<int32>* EndArray,
			const TManagedArray<int32>* StartSubArray,
			const TManagedArray<int32>* EndSubArray,
			int32 ArrayIndex) const;

		template<bool bStart = true, bool bEnd = true>
		UE_DEPRECATED(5.3, "Use GetSubElementsStartEnd instead.")
		TTuple<int32, int32> GetPatternElementsStartEnd(const TManagedArray<int32>* StartSubArray, const TManagedArray<int32>* EndSubArray, int32 LodIndex) const
		{
			return GetSubElementsStartEnd(PatternStart, PatternEnd, StartSubArray, EndSubArray, LodIndex);
		}

		//~ Weight maps
		template<typename T, TEMPLATE_REQUIRES(TIsUserAttributeType<T>::Value)>
		TArray<FName> GetUserDefinedAttributeNames(const FName& GroupName) const;

		template<typename T, TEMPLATE_REQUIRES(TIsUserAttributeType<T>::Value)>
		void AddUserDefinedAttribute(const FName& Name, const FName& GroupName);

		void RemoveUserDefinedAttribute(const FName& Name, const FName& GroupName);

		template<typename T, TEMPLATE_REQUIRES(TIsUserAttributeType<T>::Value)>
		bool HasUserDefinedAttribute(const FName& Name, const FName& GroupName) const;

		template<typename T, TEMPLATE_REQUIRES(TIsUserAttributeType<T>::Value)>
		const TManagedArray<T>* GetUserDefinedAttribute(const FName& Name, const FName& GroupName) const;

		template<typename T, TEMPLATE_REQUIRES(TIsUserAttributeType<T>::Value)>
		TManagedArray<T>* GetUserDefinedAttribute(const FName& Name, const FName& GroupName);


		//~ LODs Group
		const TManagedArray<int32>* GetMaterialStart() const { return MaterialStart; }
		const TManagedArray<int32>* GetMaterialEnd() const { return MaterialEnd; }
		const TManagedArray<int32>* GetTetherBatchStart() const { return TetherBatchStart; }
		const TManagedArray<int32>* GetTetherBatchEnd() const { return TetherBatchEnd; }
		const TManagedArray<int32>* GetSeamStart() const { return SeamStart; }
		const TManagedArray<int32>* GetSeamEnd() const { return SeamEnd; }
		const TManagedArray<int32>* GetPatternStart() const { return PatternStart; }
		const TManagedArray<int32>* GetPatternEnd() const { return PatternEnd; }
		const TManagedArray<FString>* GetPhysicsAssetPathName() const { return PhysicsAssetPathName; }
		const TManagedArray<FString>* GetSkeletonAssetPathName() const { return SkeletonAssetPathName; }

		//~ Materials Group
		const TManagedArray<FString>* GetRenderMaterialPathName() const { return RenderMaterialPathName; }

		//~ Tether Batches Group
		const TManagedArray<int32>* GetTetherStart() const { return TetherStart; }
		const TManagedArray<int32>* GetTetherEnd() const { return TetherEnd; }

		//~ Tethers Group
		const TManagedArray<int32>* GetTetherKinematicIndex() const { return TetherKinematicIndex; }
		const TManagedArray<int32>* GetTetherDynamicIndex() const { return TetherDynamicIndex; }
		const TManagedArray<float>* GetTetherReferenceLength() const { return TetherReferenceLength; }

		//~ Seam Group
		const TManagedArray<FIntVector2>* GetSeamPatterns() const { return SeamPatterns; }
		const TManagedArray<TArray<FIntVector2>>* GetSeamStitches() const { return SeamStitches; }

		//~ Patterns Group
		const TManagedArray<int32>* GetSimVerticesStart() const { return SimVerticesStart; }
		const TManagedArray<int32>* GetSimVerticesEnd() const { return SimVerticesEnd; }
		const TManagedArray<int32>* GetSimFacesStart() const { return SimFacesStart; }
		const TManagedArray<int32>* GetSimFacesEnd() const { return SimFacesEnd; }
		const TManagedArray<int32>* GetRenderVerticesStart() const { return RenderVerticesStart; }
		const TManagedArray<int32>* GetRenderVerticesEnd() const { return RenderVerticesEnd; }
		const TManagedArray<int32>* GetRenderFacesStart() const { return RenderFacesStart; }
		const TManagedArray<int32>* GetRenderFacesEnd() const { return RenderFacesEnd; }
		const TManagedArray<int32>* GetStatusFlags() const { return StatusFlags; }

		//~ Sim Faces Group
		const TManagedArray<FIntVector3>* GetSimIndices() const { return SimIndices; }

		//~ Sim Vertices Group
		const TManagedArray<FVector2f>* GetSimPosition() const { return SimPosition; }
		const TManagedArray<FVector3f>* GetSimRestPosition() const { return SimRestPosition; }
		const TManagedArray<FVector3f>* GetSimRestNormal() const { return SimRestNormal; }
		const TManagedArray<int32>* GetSimNumBoneInfluences() const { return SimNumBoneInfluences; }
		const TManagedArray<TArray<int32>>* GetSimBoneIndices() const { return SimBoneIndices; }
		const TManagedArray<TArray<float>>* GetSimBoneWeights() const { return SimBoneWeights; }

		//~ Render Faces Group
		const TManagedArray<FIntVector3>* GetRenderIndices() const { return RenderIndices; }
		const TManagedArray<int32>* GetRenderMaterialIndex() const { return RenderMaterialIndex; }

		//~ Render Vertices Group
		const TManagedArray<FVector3f>* GetRenderPosition() const { return RenderPosition; }
		const TManagedArray<FVector3f>* GetRenderNormal() const { return RenderNormal; }
		const TManagedArray<FVector3f>* GetRenderTangentU() const { return RenderTangentU; }
		const TManagedArray<FVector3f>* GetRenderTangentV() const { return RenderTangentV; }
		const TManagedArray<TArray<FVector2f>>* GetRenderUVs() const { return RenderUVs; }
		const TManagedArray<FLinearColor>* GetRenderColor() const { return RenderColor; }
		const TManagedArray<int32>* GetRenderNumBoneInfluences() const { return RenderNumBoneInfluences; }
		const TManagedArray<TArray<int32>>* GetRenderBoneIndices() const { return RenderBoneIndices; }
		const TManagedArray<TArray<float>>* GetRenderBoneWeights() const { return RenderBoneWeights; }

		//~ LODs Group
		TManagedArray<int32>* GetMaterialStart() { return MaterialStart; }
		TManagedArray<int32>* GetMaterialEnd() { return MaterialEnd; }
		TManagedArray<int32>* GetTetherBatchStart() { return TetherBatchStart; }
		TManagedArray<int32>* GetTetherBatchEnd() { return TetherBatchEnd; }
		TManagedArray<int32>* GetSeamStart() { return SeamStart; }
		TManagedArray<int32>* GetSeamEnd() { return SeamEnd; }
		TManagedArray<int32>* GetPatternStart() { return PatternStart; }
		TManagedArray<int32>* GetPatternEnd() { return PatternEnd; }
		TManagedArray<FString>* GetPhysicsAssetPathName() { return PhysicsAssetPathName; }
		TManagedArray<FString>* GetSkeletonAssetPathName() { return SkeletonAssetPathName; }

		//~ Materials Group
		TManagedArray<FString>* GetRenderMaterialPathName() { return RenderMaterialPathName; }

		//~ Tether Batches Group
		TManagedArray<int32>* GetTetherStart() { return TetherStart; }
		TManagedArray<int32>* GetTetherEnd() { return TetherEnd; }

		//~ Tethers Group
		TManagedArray<int32>* GetTetherKinematicIndex() { return TetherKinematicIndex; }
		TManagedArray<int32>* GetTetherDynamicIndex() { return TetherDynamicIndex; }
		TManagedArray<float>* GetTetherReferenceLength() { return TetherReferenceLength; }

		//~ Seam Group
		TManagedArray<FIntVector2>* GetSeamPatterns() { return SeamPatterns; }
		TManagedArray<TArray<FIntVector2>>* GetSeamStitches() { return SeamStitches; }

		//~ Patterns Group
		TManagedArray<int32>* GetSimVerticesStart() { return SimVerticesStart; }
		TManagedArray<int32>* GetSimVerticesEnd() { return SimVerticesEnd; }
		TManagedArray<int32>* GetSimFacesStart() { return SimFacesStart; }
		TManagedArray<int32>* GetSimFacesEnd() { return SimFacesEnd; }
		TManagedArray<int32>* GetRenderVerticesStart() { return RenderVerticesStart; }
		TManagedArray<int32>* GetRenderVerticesEnd() { return RenderVerticesEnd; }
		TManagedArray<int32>* GetRenderFacesStart() { return RenderFacesStart; }
		TManagedArray<int32>* GetRenderFacesEnd() { return RenderFacesEnd; }
		TManagedArray<int32>* GetStatusFlags() { return StatusFlags; }

		//~ Sim Faces Group
		TManagedArray<FIntVector3>* GetSimIndices() { return SimIndices; }

		//~ Sim Vertices Group
		TManagedArray<FVector2f>* GetSimPosition() { return SimPosition; }
		TManagedArray<FVector3f>* GetSimRestPosition() { return SimRestPosition; }
		TManagedArray<FVector3f>* GetSimRestNormal() { return SimRestNormal; }
		TManagedArray<int32>* GetSimNumBoneInfluences() { return SimNumBoneInfluences; }
		TManagedArray<TArray<int32>>* GetSimBoneIndices() { return SimBoneIndices; }
		TManagedArray<TArray<float>>* GetSimBoneWeights() { return SimBoneWeights; }

		//~ Render Faces Group
		TManagedArray<FIntVector3>* GetRenderIndices() { return RenderIndices; }
		TManagedArray<int32>* GetRenderMaterialIndex() { return RenderMaterialIndex; }

		//~ Render Vertices Group
		TManagedArray<FVector3f>* GetRenderPosition() { return RenderPosition; }
		TManagedArray<FVector3f>* GetRenderNormal() { return RenderNormal; }
		TManagedArray<FVector3f>* GetRenderTangentU() { return RenderTangentU; }
		TManagedArray<FVector3f>* GetRenderTangentV() { return RenderTangentV; }
		TManagedArray<TArray<FVector2f>>* GetRenderUVs() { return RenderUVs; }
		TManagedArray<FLinearColor>* GetRenderColor() { return RenderColor; }
		TManagedArray<int32>* GetRenderNumBoneInfluences() { return RenderNumBoneInfluences; }
		TManagedArray<TArray<int32>>* GetRenderBoneIndices() { return RenderBoneIndices; }
		TManagedArray<TArray<float>>* GetRenderBoneWeights() { return RenderBoneWeights; }

	private:
		//~ Cloth collection
		TSharedPtr<FManagedArrayCollection> ManagedArrayCollection;

		//~ LODs Group
		TManagedArray<int32>* MaterialStart;
		TManagedArray<int32>* MaterialEnd;
		TManagedArray<int32>* TetherBatchStart;
		TManagedArray<int32>* TetherBatchEnd;
		TManagedArray<int32>* SeamStart;
		TManagedArray<int32>* SeamEnd;
		TManagedArray<int32>* PatternStart;
		TManagedArray<int32>* PatternEnd;
		TManagedArray<FString>* PhysicsAssetPathName;
		TManagedArray<FString>* SkeletonAssetPathName;

		//~ Materials Group
		TManagedArray<FString>* RenderMaterialPathName;

		//~ Tether Batches Group
		TManagedArray<int32>* TetherStart;
		TManagedArray<int32>* TetherEnd;

		//~ Tethers Group
		TManagedArray<int32>* TetherKinematicIndex;
		TManagedArray<int32>* TetherDynamicIndex;
		TManagedArray<float>* TetherReferenceLength;

		//~ Seam Group
		TManagedArray<FIntVector2>* SeamPatterns;  // Stitched pattern indices (LOD based indices) for SeamIndicesFirst
		TManagedArray<TArray<FIntVector2>>* SeamStitches;  // Stitched vertex indices pair (LOD based indices)

		//~ Patterns Group
		TManagedArray<int32>* SimVerticesStart;
		TManagedArray<int32>* SimVerticesEnd;
		TManagedArray<int32>* SimFacesStart;
		TManagedArray<int32>* SimFacesEnd;
		TManagedArray<int32>* RenderVerticesStart;
		TManagedArray<int32>* RenderVerticesEnd;
		TManagedArray<int32>* RenderFacesStart;
		TManagedArray<int32>* RenderFacesEnd;
		TManagedArray<int32>* StatusFlags;  // Whether this pattern should be rendered, simulated, ...etc

		//~ Sim Faces Group
		TManagedArray<FIntVector3>* SimIndices;  // The indices point to the elements in the Sim Vertices arrays but don't include the LOD start offset

		//~ Sim Vertices Group
		TManagedArray<FVector2f>* SimPosition;
		TManagedArray<FVector3f>* SimRestPosition;
		TManagedArray<FVector3f>* SimRestNormal;  // Used for capture, maxdistance, backstop authoring ...etc
		TManagedArray<int32>* SimNumBoneInfluences;
		TManagedArray<TArray<int32>>* SimBoneIndices;
		TManagedArray<TArray<float>>* SimBoneWeights;

		//~ Render Faces Group
		TManagedArray<FIntVector3>* RenderIndices;  // The indices point to the elements in the Render Vertices arrays but don't include the LOD start offset
		TManagedArray<int32>* RenderMaterialIndex;  // Render material per triangle

		//~ Render Vertices Group
		TManagedArray<FVector3f>* RenderPosition;
		TManagedArray<FVector3f>* RenderNormal;
		TManagedArray<FVector3f>* RenderTangentU;
		TManagedArray<FVector3f>* RenderTangentV;
		TManagedArray<TArray<FVector2f>>* RenderUVs;
		TManagedArray<FLinearColor>* RenderColor;
		TManagedArray<int32>* RenderNumBoneInfluences;
		TManagedArray<TArray<int32>>* RenderBoneIndices;
		TManagedArray<TArray<float>>* RenderBoneWeights;
	};

	template<typename T>
	inline TConstArrayView<T> FClothCollection::GetElements(const TManagedArray<T>* ElementArray, const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 ArrayIndex) const
	{
		if (StartArray && EndArray && ElementArray)
		{
			const int32 Start = (*StartArray)[ArrayIndex];
			const int32 End = (*EndArray)[ArrayIndex];
			if (Start != INDEX_NONE && End != INDEX_NONE)
			{
				return TConstArrayView<T>(ElementArray->GetData() + Start, End - Start + 1);
			}
			checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		}
		return TConstArrayView<T>();
	}

	template<typename T>
	inline TArrayView<T> FClothCollection::GetElements(TManagedArray<T>* ElementArray, const TManagedArray<int32>* StartArray, const TManagedArray<int32>* EndArray, int32 ArrayIndex)
	{
		if (StartArray && EndArray && ElementArray)
		{
			const int32 Start = (*StartArray)[ArrayIndex];
			const int32 End = (*EndArray)[ArrayIndex];
			if (Start != INDEX_NONE && End != INDEX_NONE)
			{
				return TArrayView<T>(ElementArray->GetData() + Start, End - Start + 1);
			}
			checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		}
		return TArrayView<T>();
	}

	template<typename T>
	inline TConstArrayView<T> FClothCollection::GetSubElements(
		const TManagedArray<T>* SubElementArray,
		const TManagedArray<int32>* StartArray,
		const TManagedArray<int32>* EndArray,
		const TManagedArray<int32>* StartSubArray,
		const TManagedArray<int32>* EndSubArray,
		int32 ArrayIndex) const
	{
		if (StartArray && EndArray && SubElementArray)
		{
			const TTuple<int32, int32> StartEnd = GetSubElementsStartEnd(StartArray, EndArray, StartSubArray, EndSubArray, ArrayIndex);
			const int32 Start = StartEnd.Get<0>();
			const int32 End = StartEnd.Get<1>();
			if (Start != INDEX_NONE && End != INDEX_NONE)
			{
				return TConstArrayView<T>(SubElementArray->GetData() + Start, End - Start + 1);
			}
			checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		}
		return TConstArrayView<T>();
	}

	template<typename T>
	inline TArrayView<T> FClothCollection::GetSubElements(
		TManagedArray<T>* SubElementArray,
		const TManagedArray<int32>* StartArray,
		const TManagedArray<int32>* EndArray,
		const TManagedArray<int32>* StartSubArray,
		const TManagedArray<int32>* EndSubArray,
		int32 ArrayIndex)
	{
		if (StartArray && EndArray && SubElementArray)
		{
			const TTuple<int32, int32> StartEnd = GetSubElementsStartEnd(StartArray, EndArray, StartSubArray, EndSubArray, ArrayIndex);
			const int32 Start = StartEnd.Get<0>();
			const int32 End = StartEnd.Get<1>();
			if (Start != INDEX_NONE && End != INDEX_NONE)
			{
				return TArrayView<T>(SubElementArray->GetData() + Start, End - Start + 1);
			}
			checkf(Start == End, TEXT("Only one boundary of the range is set to INDEX_NONE, when both should."));
		}
		return TArrayView<T>();
	}
}  // End namespace UE::Chaos::ClothAsset

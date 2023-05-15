// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;

namespace UE::Chaos::ClothAsset
{
	class FClothCollection;

	/**
	 * Cloth Asset collection tether batch facade class to access cloth tether batch data.
	 * Constructed from FCollectionClothLodConstFacade.
	 * Const access (read only) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothTetherBatchConstFacade
	{
	public:
		FCollectionClothTetherBatchConstFacade() = delete;

		FCollectionClothTetherBatchConstFacade(const FCollectionClothTetherBatchConstFacade&) = delete;
		FCollectionClothTetherBatchConstFacade& operator=(const FCollectionClothTetherBatchConstFacade&) = delete;

		FCollectionClothTetherBatchConstFacade(FCollectionClothTetherBatchConstFacade&&) = default;
		FCollectionClothTetherBatchConstFacade& operator=(FCollectionClothTetherBatchConstFacade&&) = default;

		virtual ~FCollectionClothTetherBatchConstFacade() = default;

		/** Return the total number of tethers for this batch. */
		int32 GetNumTethers() const;

		/** Return the tethers offset for this batch in the tethers for the LOD. */
		int32 GetTethersOffset() const;

		//~ Tethers Group. 
		// Indices correspond with the FCollectionClothLodConstFacade indices
		TConstArrayView<int32> GetTetherKinematicIndex() const;
		TConstArrayView<int32> GetTetherDynamicIndex() const;
		TConstArrayView<float> GetTetherReferenceLength() const;

		/** Return the LOD index this facade has been created with. */
		int32 GetLodIndex() const { return LodIndex; }

		/** Return the tether batch index this facade has been created with. */
		int32 GetTetherBatchIndex() const { return TetherBatchIndex; }

		/** Cloth data expects tethers to be zipped into tuples. This is a convenience function to generate those.*/
		TArray<TTuple<int32, int32, float>> GetZippedTetherData() const;

	protected:
		friend class FCollectionClothTetherBatchFacade;  // For other instances access
		friend class FCollectionClothLodConstFacade;
		FCollectionClothTetherBatchConstFacade(const TSharedPtr<const FClothCollection>& InClothCollection, int32 InLodIndex, int32 TetherBatchIndex);

		int32 GetBaseElementIndex() const;
		int32 GetElementIndex() const { return GetBaseElementIndex() + TetherBatchIndex; }

		TSharedPtr<const FClothCollection> ClothCollection;
		int32 LodIndex;
		int32 TetherBatchIndex;
	};

	/**
	 * Cloth Asset collection tether batch facade class to access cloth tether batch data.
	 * Constructed from FCollectionClothLodFacade.
	 * Non-const access (read/write) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothTetherBatchFacade final : public FCollectionClothTetherBatchConstFacade
	{
	public:
		FCollectionClothTetherBatchFacade() = delete;

		FCollectionClothTetherBatchFacade(const FCollectionClothTetherBatchFacade&) = delete;
		FCollectionClothTetherBatchFacade& operator=(const FCollectionClothTetherBatchFacade&) = delete;

		FCollectionClothTetherBatchFacade(FCollectionClothTetherBatchFacade&&) = default;
		FCollectionClothTetherBatchFacade& operator=(FCollectionClothTetherBatchFacade&&) = default;

		virtual ~FCollectionClothTetherBatchFacade() override = default;

		/** Remove all tethers from this tether batch. */
		void Reset();

		/** Initialize the tether batch based on zipped tether tuple data. */
		void Initialize(const TArray<TTuple<int32, int32, float>>& Tethers);

		/** Initialize the tether batch using another tether batch. */
		void Initialize(const FCollectionClothTetherBatchConstFacade& Other);

		/** Grow or shrink the space reserved for tethers for this tether batch within the cloth collection. */
		void SetNumTethers(int32 NumTethers);

		//~ Tethers Group. 
		// Indices correspond with the FCollectionClothLodConstFacade indices
		TArrayView<int32> GetTetherKinematicIndex();
		TArrayView<int32> GetTetherDynamicIndex();
		TArrayView<float> GetTetherReferenceLength();

	protected:
		friend class FCollectionClothLodFacade;
		FCollectionClothTetherBatchFacade(const TSharedPtr<FClothCollection>& InClothCollection, int32 InLodIndex, int32 InTetherIndex);

		void SetDefaults();

		TSharedPtr<FClothCollection> GetClothCollection() { return ConstCastSharedPtr<FClothCollection>(ClothCollection); }
	};
}  // End namespace UE::Chaos::ClothAsset

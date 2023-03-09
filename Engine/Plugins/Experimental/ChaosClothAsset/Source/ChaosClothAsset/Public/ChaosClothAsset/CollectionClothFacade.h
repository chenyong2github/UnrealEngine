// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosClothAsset/CollectionClothLodFacade.h"

class FClothCollection;

namespace UE::Chaos::ClothAsset
{
	/**
	 * Cloth Asset collection facade class focused on draping and pattern information.
	 * Const access (read only) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothConstFacade
	{
	public:
		explicit FCollectionClothConstFacade(const TSharedPtr<const FManagedArrayCollection>& ManagedArrayCollection);

		FCollectionClothConstFacade() = delete;

		FCollectionClothConstFacade(const FCollectionClothConstFacade&) = delete;
		FCollectionClothConstFacade& operator=(const FCollectionClothConstFacade&) = delete;

		FCollectionClothConstFacade(FCollectionClothConstFacade&&) = default;
		FCollectionClothConstFacade& operator=(FCollectionClothConstFacade&&) = default;

		virtual ~FCollectionClothConstFacade() = default;

		/** Return whether the facade is defined on the collection. */
		bool IsValid() const;

		/** Return the specified LOD. */
		FCollectionClothLodConstFacade GetLod(int32 LodIndex) const;

		/** Return the number of LODs contained in this Cloth. */
		int32 GetNumLods() const;

		/** Return whether this cloth collection has the specified weight map. */
		bool HasWeightMap(const FName& Name) const;

		/** Return the name of all user weight maps on this cloth collection. */
		TArray<FName> GetWeightMapNames() const;

	protected:
		TSharedPtr<const FClothCollection> ClothCollection;
	};

	/**
	 * Cloth Asset collection facade class focused on draping and pattern information.
	 * Non-const access (read/write) version.
	 */
	class CHAOSCLOTHASSET_API FCollectionClothFacade final : public FCollectionClothConstFacade
	{
	public:
		explicit FCollectionClothFacade(const TSharedPtr<FManagedArrayCollection>& ManagedArrayCollection);

		FCollectionClothFacade() = delete;

		FCollectionClothFacade(const FCollectionClothFacade&) = delete;
		FCollectionClothFacade& operator=(const FCollectionClothFacade&) = delete;

		FCollectionClothFacade(FCollectionClothFacade&&) = default;
		FCollectionClothFacade& operator=(FCollectionClothFacade&&) = default;
		virtual ~FCollectionClothFacade() override = default;

		/** Create this facade's groups and attributes. */
		void DefineSchema();

		/** Remove all LODs from this cloth. */
		void Reset();

		/** Add a new LOD to this cloth. */
		int32 AddLod();

		/** Return the specified LOD. */
		FCollectionClothLodFacade GetLod(int32 LodIndex);

		/** Add a new LOD to this cloth, and return the cloth LOD facade set to its index. */
		FCollectionClothLodFacade AddGetLod() { return GetLod(AddLod()); }

		/** Set a new number of LODs for this cloth. */
		void SetNumLod(int32 NumLods);

		/** Add a new weight map to this cloth. Access is then done per pattern. */
		void AddWeightMap(const FName& Name);

		/** Remove a weight map from this cloth. */
		void RemoveWeightMap(const FName& Name);

	private:
		TSharedPtr<FClothCollection> GetClothCollection() { return ConstCastSharedPtr<FClothCollection>(ClothCollection); }
	};
}  // End namespace UE::Chaos::ClothAsset

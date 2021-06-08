// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/SortedMap.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "Evaluation/PreAnimatedState/IMovieScenePreAnimatedStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"


namespace UE
{
namespace MovieScene
{

template<typename StorageTraits>
struct TPreAnimatedStateStorage : IPreAnimatedStorage
{
	using KeyType     = typename StorageTraits::KeyType;
	using StorageType = typename StorageTraits::StorageType;

	struct IRestoreMask
	{
		virtual ~IRestoreMask(){}

		virtual bool CanRestore(const KeyType& InKey) const = 0;
	};

	TPreAnimatedStateStorage()
	{}

	TPreAnimatedStateStorage(StorageTraits&& InTraits)
		: Traits(MoveTemp(InTraits))
	{}

	TPreAnimatedStateStorage(const TPreAnimatedStateStorage&) = delete;
	TPreAnimatedStateStorage& operator=(const TPreAnimatedStateStorage&) = delete;

public:

	void Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* InParentExtension) override
	{
		ParentExtension = InParentExtension;
		StorageID = InStorageID;
	}

	void SetRestoreMask(const IRestoreMask* InRestoreMask)
	{
		RestoreMask = InRestoreMask;
	}

	FPreAnimatedStorageID GetStorageType() const override
	{
		return StorageID;
	}

	EPreAnimatedStorageRequirement RestorePreAnimatedStateStorage(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement SourceRequirement, EPreAnimatedStorageRequirement TargetRequirement, const FRestoreStateParams& Params) override
	{
		if (RestoreMask)
		{
			if (!RestoreMask->CanRestore(PreAnimatedStorage[StorageIndex].Key))
			{
				return EPreAnimatedStorageRequirement::NoChange;
			}
		}

		if (SourceRequirement == EPreAnimatedStorageRequirement::Persistent)
		{
			// Restoring global state
			if (TargetRequirement == EPreAnimatedStorageRequirement::None)
			{
				FCachedData CachedData = MoveTemp(PreAnimatedStorage[StorageIndex]);

				KeyToStorageIndex.Remove(CachedData.Key);
				PreAnimatedStorage.RemoveAt(StorageIndex, 1);
				TransientPreAnimatedStorage.Remove(StorageIndex);

				if (CachedData.bInitialized)
				{
					Traits.RestorePreAnimatedValue(CachedData.Key, CachedData.Value, Params);
				}
				return EPreAnimatedStorageRequirement::None;
			}
			else
			{
				ensure(TargetRequirement == EPreAnimatedStorageRequirement::NoChange);

				FCachedData& CachedData = PreAnimatedStorage[StorageIndex];
				if (CachedData.bInitialized)
				{
					Traits.RestorePreAnimatedValue(CachedData.Key, CachedData.Value, Params);
				}

				return EPreAnimatedStorageRequirement::NoChange;
			}
		}

		ensure(SourceRequirement == EPreAnimatedStorageRequirement::Transient);

		// Always restore from the transient storage if available
		if (StorageType* CachedData = TransientPreAnimatedStorage.Find(StorageIndex))
		{
			Traits.RestorePreAnimatedValue(PreAnimatedStorage[StorageIndex].Key, *CachedData, Params);

			TransientPreAnimatedStorage.Remove(StorageIndex);

			return EPreAnimatedStorageRequirement::Persistent;
		}

		if (TargetRequirement == EPreAnimatedStorageRequirement::None)
		{
			FCachedData& ActualValue = PreAnimatedStorage[StorageIndex];
			if (ActualValue.bPersistent)
			{
				if (ActualValue.bInitialized)
				{
					Traits.RestorePreAnimatedValue(ActualValue.Key, ActualValue.Value, Params);
				}
				return EPreAnimatedStorageRequirement::Persistent;
			}

			FCachedData Tmp = MoveTemp(PreAnimatedStorage[StorageIndex]);

			KeyToStorageIndex.Remove(Tmp.Key);
			PreAnimatedStorage.RemoveAt(StorageIndex, 1);
			TransientPreAnimatedStorage.Remove(StorageIndex);

			if (Tmp.bInitialized)
			{
				Traits.RestorePreAnimatedValue(Tmp.Key, Tmp.Value, Params);
			}

			return EPreAnimatedStorageRequirement::None;
		}

		if (TargetRequirement == EPreAnimatedStorageRequirement::Persistent)
		{
			// Restore the value but keep the value cached
			FCachedData& PersistentData = PreAnimatedStorage[StorageIndex];

			if (PersistentData.bInitialized)
			{
				PersistentData.bPersistent = true;
				Traits.RestorePreAnimatedValue(PersistentData.Key, PersistentData.Value, Params);
			}
		}

		return EPreAnimatedStorageRequirement::Persistent;
	}

	EPreAnimatedStorageRequirement DiscardPreAnimatedStateStorage(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement SourceRequirement) override
	{
		if (RestoreMask)
		{
			if (!RestoreMask->CanRestore(PreAnimatedStorage[StorageIndex].Key))
			{
				return EPreAnimatedStorageRequirement::NoChange;
			}
		}

		if (SourceRequirement == EPreAnimatedStorageRequirement::Persistent)
		{
			KeyType Key = PreAnimatedStorage[StorageIndex].Key;

			KeyToStorageIndex.Remove(Key);
			PreAnimatedStorage.RemoveAt(StorageIndex, 1);
			TransientPreAnimatedStorage.Remove(StorageIndex);

			return EPreAnimatedStorageRequirement::None;
		}
		else
		{
			ensure(SourceRequirement == EPreAnimatedStorageRequirement::Transient);
			const int32 NumTransients = TransientPreAnimatedStorage.Remove(StorageIndex);
			if (NumTransients == 0)
			{
				PreAnimatedStorage[StorageIndex].bPersistent = true;
			}
			return EPreAnimatedStorageRequirement::Persistent;
		}
	}

	FPreAnimatedStorageIndex GetOrCreateStorageIndex(const KeyType& InKey)
	{
		FPreAnimatedStorageIndex Index = KeyToStorageIndex.FindRef(InKey);
		if (!Index)
		{
			Index = PreAnimatedStorage.Add(FCachedData{InKey});
			KeyToStorageIndex.Add(InKey, Index);
		}

		return Index;
	}

	FPreAnimatedStorageIndex FindStorageIndex(const KeyType& InKey) const
	{
		return KeyToStorageIndex.FindRef(InKey);
	}

	void AssignPreAnimatedValue(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement StorageRequirement, StorageType&& InNewValue)
	{
		check(StorageIndex);

		FCachedData& CachedData = PreAnimatedStorage[StorageIndex.Value];

		if (StorageRequirement == EPreAnimatedStorageRequirement::Persistent)
		{
			ensure(!CachedData.bInitialized);
			CachedData.Value = MoveTemp(InNewValue);
			CachedData.bPersistent = true;
			CachedData.bInitialized = true;
		}
		else if (StorageRequirement == EPreAnimatedStorageRequirement::Transient)
		{
			ensure(!CachedData.bInitialized || !TransientPreAnimatedStorage.Contains(StorageIndex));

			// Assign the transient value
			if (!CachedData.bInitialized)
			{
				CachedData.Value = MoveTemp(InNewValue);
				CachedData.bInitialized = true;
			}
			else
			{
				TransientPreAnimatedStorage.Add(StorageIndex, MoveTemp(InNewValue));
			}
		}
	}

	bool IsStorageRequirementSatisfied(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement StorageRequirement) const
	{
		check(StorageIndex);

		const FCachedData& CachedData = PreAnimatedStorage[StorageIndex.Value];

		if (StorageRequirement == EPreAnimatedStorageRequirement::Persistent)
		{
			return CachedData.bInitialized;
		}
		else if (StorageRequirement == EPreAnimatedStorageRequirement::Transient)
		{
			return (CachedData.bInitialized && CachedData.bPersistent == false)
				|| TransientPreAnimatedStorage.Contains(StorageIndex);
		}

		return true;
	}

	void ForciblyPersistStorage(FPreAnimatedStorageIndex StorageIndex)
	{
		check(StorageIndex);
		PreAnimatedStorage[StorageIndex.Value].bPersistent = true;
	}

	bool IsStorageInitialized(FPreAnimatedStorageIndex StorageIndex) const
	{
		return StorageIndex && (PreAnimatedStorage[StorageIndex.Value].bInitialized || TransientPreAnimatedStorage.Contains(StorageIndex));
	}

	bool HasEverAnimated(FPreAnimatedStorageIndex StorageIndex) const
	{
		return StorageIndex && PreAnimatedStorage[StorageIndex.Value].bInitialized;
	}

	const KeyType& GetKey(FPreAnimatedStorageIndex StorageIndex) const
	{
		return PreAnimatedStorage[StorageIndex].Key;
	}

	void ReplaceKey(FPreAnimatedStorageIndex StorageIndex, const KeyType& NewKey)
	{
		KeyType OldKey = PreAnimatedStorage[StorageIndex].Key;
		PreAnimatedStorage[StorageIndex].Key = NewKey;

		KeyToStorageIndex.Remove(OldKey);
		KeyToStorageIndex.Add(NewKey, StorageIndex);
	}

protected:

	struct FCachedData
	{
		FCachedData()
			: bInitialized(false)
			, bPersistent(false)
		{}

		FCachedData(const KeyType& InKey)
			: Key(InKey)
			, bInitialized(false)
			, bPersistent(false)
		{}

		FCachedData(const KeyType& InKey, StorageType&& InValue)
			: Key(InKey)
			, Value(MoveTemp(InValue))
			, bInitialized(true)
			, bPersistent(false)
		{}

		KeyType Key;
		StorageType Value;
		bool bInitialized : 1;
		bool bPersistent : 1;
	};

	TMap<KeyType, FPreAnimatedStorageIndex> KeyToStorageIndex;

	TSparseArray<FCachedData> PreAnimatedStorage;

	/** Storage that holds values that need to be kept transiently (for evaluation) */
	TSortedMap<FPreAnimatedStorageIndex, StorageType> TransientPreAnimatedStorage;

	FPreAnimatedStateExtension* ParentExtension = nullptr;

	const IRestoreMask* RestoreMask = nullptr;

	FPreAnimatedStorageID StorageID;

	StorageTraits Traits;
};



} // namespace MovieScene
} // namespace UE



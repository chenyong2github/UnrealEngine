// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Evaluation/MovieSceneEvaluationKey.h"
#include "UObject/ObjectKey.h"
#include "UObject/Object.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieScenePropertyBinding.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectGroupManager.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedEntityCaptureSource.h"


namespace UE
{
namespace MovieScene
{


template<typename PropertyTraits, typename MetaDataTypes, typename MetaDataIndices>
struct TPreAnimatedPropertyStorageImpl;

template<typename PropertyTraits, typename ...MetaDataTypes, int ...MetaDataIndices>
struct TPreAnimatedPropertyStorageImpl<PropertyTraits, TPropertyMetaData<MetaDataTypes...>, TIntegerSequence<int, MetaDataIndices...>>
	: IPreAnimatedStorage
	, IPreAnimatedObjectPropertyStorage
{
	using StorageType       = typename PropertyTraits::StorageType;

	TPreAnimatedPropertyStorageImpl(const FPropertyDefinition& InPropertyDefinition)
		: MetaDataComponents(InPropertyDefinition.MetaDataTypes)
	{
		check(MetaDataComponents.Num() == sizeof...(MetaDataIndices));

		if (InPropertyDefinition.CustomPropertyRegistration)
		{
			CustomAccessors = InPropertyDefinition.CustomPropertyRegistration->GetAccessors();
		}
	}

	FPreAnimatedStorageID GetStorageType() const override
	{
		return StorageID;
	}

	void Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* InParentExtension) override
	{
		this->Storage.Initialize(InStorageID, InParentExtension);

		ObjectGroupManager = InParentExtension->GetOrCreateGroupManager<FPreAnimatedObjectGroupManager>();
		ParentExtension = InParentExtension;
		StorageID = InStorageID;
	}

	void OnObjectReplaced(FPreAnimatedStorageIndex StorageIndex, const FObjectKey& OldObject, const FObjectKey& NewObject) override
	{
		FAnimatedPropertyKey ExistingKey = this->Storage.GetKey(StorageIndex);
		ExistingKey.BoundObject = NewObject;

		this->Storage.ReplaceKey(StorageIndex, ExistingKey);
	}

	EPreAnimatedStorageRequirement RestorePreAnimatedStateStorage(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement SourceRequirement, EPreAnimatedStorageRequirement TargetRequirement, const FRestoreStateParams& Params) override
	{
		return this->Storage.RestorePreAnimatedStateStorage(StorageIndex, SourceRequirement, TargetRequirement, Params);
	}

	EPreAnimatedStorageRequirement DiscardPreAnimatedStateStorage(FPreAnimatedStorageIndex StorageIndex, EPreAnimatedStorageRequirement SourceRequirement) override
	{
		return this->Storage.DiscardPreAnimatedStateStorage(StorageIndex, SourceRequirement);
	}

	IPreAnimatedObjectPropertyStorage* AsPropertyStorage() override
	{
		return this;
	}

	void BeginTrackingEntities(const FPreAnimatedTrackerParams& Params, TRead<FMovieSceneEntityID> EntityIDs, TRead<FInstanceHandle> InstanceHandles, TRead<UObject*> BoundObjects, TRead<FMovieScenePropertyBinding> PropertyBindings) override
	{
		FPreAnimatedEntityCaptureSource* EntityMetaData = ParentExtension->GetOrCreateEntityMetaData();

		const int32 Num = Params.Num;
		const bool  bWantsRestore = Params.bWantsRestoreState;

		for (int32 Index = 0; Index < Num; ++Index)
		{
			UObject* BoundObject  = BoundObjects[Index];
			FName    PropertyPath = PropertyBindings[Index].PropertyPath;

			FAnimatedPropertyKey Key{ BoundObject, PropertyPath };

			FPreAnimatedStorageGroupHandle GroupHandle  = this->ObjectGroupManager->MakeGroupForObject(BoundObject);
			FPreAnimatedStorageIndex       StorageIndex = this->Storage.GetOrCreateStorageIndex(Key);

			FPreAnimatedStateEntry Entry{ GroupHandle, FPreAnimatedStateCachedValueHandle{ StorageID, StorageIndex } };
			EntityMetaData->BeginTrackingEntity(Entry, EntityIDs[Index], InstanceHandles[Index], bWantsRestore);
		}
	}

	void CachePreAnimatedValues(const FCachePreAnimatedValueParams& Params, FEntityAllocationIteratorItem Item, TRead<UObject*> BoundObjects, TRead<FMovieScenePropertyBinding> PropertyBindings, FThreeWayAccessor Properties) override
	{
		const FEntityAllocation* Allocation = Item.GetAllocation();

		TTuple< TComponentReader<MetaDataTypes>... > MetaData(
			Allocation->ReadComponents(MetaDataComponents[MetaDataIndices].template ReinterpretCast<MetaDataTypes>())...
			);

		const uint16* Fast = Properties.Get<1>();
		const FCustomPropertyIndex* Custom = Properties.Get<0>();
		const TSharedPtr<FTrackInstancePropertyBindings>* Slow = Properties.Get<2>();

		const int32 Num = Allocation->Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			UObject* BoundObject  = BoundObjects[Index];
			FName    PropertyPath = PropertyBindings[Index].PropertyPath;

			FAnimatedPropertyKey Key{ BoundObject, PropertyPath };

			FPreAnimatedStorageGroupHandle GroupHandle  = this->ObjectGroupManager->MakeGroupForObject(Key.BoundObject);
			FPreAnimatedStorageIndex       StorageIndex = this->Storage.GetOrCreateStorageIndex(Key);

			FPreAnimatedStateEntry Entry{ GroupHandle, FPreAnimatedStateCachedValueHandle{ StorageID, StorageIndex } };

			this->ParentExtension->EnsureMetaData(Entry);

			EPreAnimatedStorageRequirement StorageRequirement = this->ParentExtension->GetStorageRequirement(Entry);
			if (!Storage.IsStorageRequirementSatisfied(StorageIndex, StorageRequirement))
			{
				FPreAnimatedProperty NewValue;
				NewValue.MetaData = MakeTuple(MetaData.template Get<MetaDataIndices>()[Index]...);

				if (Fast)
				{
					NewValue.Binding.template Set<uint16>(Fast[Index]);
					PropertyTraits::GetObjectPropertyValue(BoundObjects[Index], MetaData.template Get<MetaDataIndices>()[Index]..., Fast[Index], NewValue.Data);
				}
				else if (Custom)
				{
					const FCustomPropertyAccessor& Accessor = this->CustomAccessors[Custom[Index].Value];

					NewValue.Binding.template Set<const FCustomPropertyAccessor*>(&Accessor);
					PropertyTraits::GetObjectPropertyValue(BoundObjects[Index], MetaData.template Get<MetaDataIndices>()[Index]..., Accessor, NewValue.Data);
				}
				else if (Slow)
				{
					const TSharedPtr<FTrackInstancePropertyBindings>& Bindings = Slow[Index];

					NewValue.Binding.template Set<TSharedPtr<FTrackInstancePropertyBindings>>(Bindings);
					PropertyTraits::GetObjectPropertyValue(BoundObjects[Index], MetaData.template Get<MetaDataIndices>()[Index]..., Bindings.Get(), NewValue.Data);
				}

				Storage.AssignPreAnimatedValue(StorageIndex, StorageRequirement, MoveTemp(NewValue));
			}

			if (Params.bForcePersist)
			{
				this->Storage.ForciblyPersistStorage(StorageIndex);
			}
		}
	}

protected:

	struct FAnimatedPropertyKey
	{
		FObjectKey BoundObject;
		FName PropertyPath;

		friend uint32 GetTypeHash(const FAnimatedPropertyKey& In)
		{
			return HashCombine(GetTypeHash(In.BoundObject), GetTypeHash(In.PropertyPath));
		}
		friend bool operator==(const FAnimatedPropertyKey& A, const FAnimatedPropertyKey& B)
		{
			return A.BoundObject == B.BoundObject && A.PropertyPath == B.PropertyPath;
		}
	};

	struct FPreAnimatedProperty
	{
		StorageType Data;
		TVariant<const FCustomPropertyAccessor*, uint16, TSharedPtr<FTrackInstancePropertyBindings>> Binding;
		TTuple<MetaDataTypes...> MetaData;
	};

	struct FPropertyStorageTraits
	{
		using KeyType = FAnimatedPropertyKey;
		using StorageType = FPreAnimatedProperty;

		void RestorePreAnimatedValue(const FAnimatedPropertyKey& InKey, FPreAnimatedProperty& CachedValue, const FRestoreStateParams& Params)
		{
			UObject* Object = InKey.BoundObject.ResolveObjectPtr();
			if (!Object)
			{
				return;
			}

			if (const uint16* FastOffset = CachedValue.Binding.template TryGet<uint16>())
			{
				PropertyTraits::SetObjectPropertyValue(Object, CachedValue.MetaData.template Get<MetaDataIndices>()..., *FastOffset, CachedValue.Data);
			}
			else  if (const TSharedPtr<FTrackInstancePropertyBindings>* Bindings = CachedValue.Binding.template TryGet<TSharedPtr<FTrackInstancePropertyBindings>>())
			{
				PropertyTraits::SetObjectPropertyValue(Object, CachedValue.MetaData.template Get<MetaDataIndices>()..., Bindings->Get(), CachedValue.Data);
			}
			else if (const FCustomPropertyAccessor* CustomAccessor = CachedValue.Binding.template Get<const FCustomPropertyAccessor*>())
			{
				PropertyTraits::SetObjectPropertyValue(Object, CachedValue.MetaData.template Get<MetaDataIndices>()..., *CustomAccessor, CachedValue.Data);
			}
		}
	};

	TArrayView<const FComponentTypeID> MetaDataComponents;
	FCustomAccessorView CustomAccessors;
	TPreAnimatedStateStorage<FPropertyStorageTraits> Storage;
	FPreAnimatedStorageID StorageID;
	FPreAnimatedStateExtension* ParentExtension;

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager;
};


template<typename PropertyTraits>
using TPreAnimatedPropertyStorage = TPreAnimatedPropertyStorageImpl<PropertyTraits, typename PropertyTraits::MetaDataType, TMakeIntegerSequence<int, PropertyTraits::MetaDataType::Num>>;






} // namespace MovieScene
} // namespace UE







// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"

#include "Templates/SharedPointer.h"
#include "Templates/TypeHash.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"

#include "IMovieScenePlayer.h"
#include "MovieSceneExecutionToken.h"
#include "MovieSceneCommonHelpers.h"

namespace UE
{
namespace MovieScene
{

struct MOVIESCENE_API FGlobalPreAnimatedStateIDs
{
	FMovieSceneAnimTypeID GetCustom(uint16 CustomAccessorIndex);
	FMovieSceneAnimTypeID GetFast(uint16 FastPropertyOffset);
	FMovieSceneAnimTypeID GetSlow(FName PropertyPath);

private:

	static TMap<uint16, FMovieSceneAnimTypeID> CustomGlobalPreAnimatedTypeID;
	static TMap<uint16, FMovieSceneAnimTypeID> FastGlobalPreAnimatedTypeID;
	static TMap<FName,  FMovieSceneAnimTypeID> SlowGlobalPreAnimatedTypeID;
};

template<typename PropertyTraits, typename MetaDataType, typename MetaDataIndices>
struct TPreAnimatedPropertyHelperBase;

template<typename PropertyTraits, typename ...MetaDataTypes, int... MetaDataIndices>
struct TPreAnimatedPropertyHelperBase<PropertyTraits, TPropertyMetaData<MetaDataTypes...>, TIntegerSequence<int, MetaDataIndices...>>
{
	using StorageType = typename PropertyTraits::StorageType;

	TPreAnimatedPropertyHelperBase(const FPropertyDefinition* InPropertyDefinition, UMovieSceneEntitySystemLinker* InLinker)
		: Linker(InLinker)
		, BuiltInComponents(FBuiltInComponentTypes::Get())
		, PropertyDefinition(InPropertyDefinition)
	{
		if (PropertyDefinition->CustomPropertyRegistration)
		{
			CustomAccessors = PropertyDefinition->CustomPropertyRegistration->GetAccessors();
		}
	}

	void SavePreAnimatedState()
	{
		if (CustomAccessors.Num() != 0)
		{
			SavePreAnimatedStateForComponent(BuiltInComponents->CustomPropertyIndex);
		}

		SavePreAnimatedStateForComponent(BuiltInComponents->FastPropertyOffset);
		SavePreAnimatedStateForComponent(BuiltInComponents->SlowProperty);
	}

private:

	UMovieSceneEntitySystemLinker* Linker;
	FBuiltInComponentTypes* BuiltInComponents;
	const FPropertyDefinition* PropertyDefinition;
	FCustomAccessorView CustomAccessors;
	FGlobalPreAnimatedStateIDs AnimTypeIDs;

private:

	template<typename T>
	void SavePreAnimatedStateForComponent(TComponentTypeID<T> ComponentType)
	{
		if (!Linker->EntityManager.ContainsComponent(ComponentType))
		{
			return;
		}

		FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

		auto CacheState =  [this, InstanceRegistry](UObject* Object, FInstanceHandle InstanceHandle, const MetaDataTypes&... InMetaData, T InComponent)
		{
			IMovieScenePlayer* Player = InstanceRegistry->GetInstance(InstanceHandle).GetPlayer();
			if (Player->PreAnimatedState.IsGlobalCaptureEnabled())
			{
				this->SavePreAnimatedState(Player, Object, InMetaData..., InComponent);
			}
		};

		// Run a task for any entity that has an instance handle directly. This is the case for the majority of unblended entities.
		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.Read(BuiltInComponents->InstanceHandle)
		.ReadAllOf(PropertyDefinition->MetaDataTypes[MetaDataIndices].ReinterpretCast<MetaDataTypes>()...)
		.Read(ComponentType)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink, PropertyDefinition->PropertyType })
		.Iterate_PerEntity(&Linker->EntityManager, CacheState);

		// Special cases for blended entities running multiple inputs into outputs
		if (Linker->EntityManager.ContainsComponent(BuiltInComponents->BlendChannelOutput))
		{
			struct FOutputProperty
			{
				T PropertyComponent;
				TTuple<MetaDataTypes...> MetaData;
			};
			TMap<uint16, FOutputProperty> RelinkedOutputs;

			// Gather outputs that have been changed
			FEntityTaskBuilder()
			.Read(BuiltInComponents->BlendChannelOutput)
			.ReadAllOf(PropertyDefinition->MetaDataTypes[MetaDataIndices].ReinterpretCast<MetaDataTypes>()...)
			.Read(ComponentType)
			.FilterAll({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->BoundObject, PropertyDefinition->PropertyType })
			.Iterate_PerEntity(&Linker->EntityManager, [&RelinkedOutputs](uint16 BlendChannel, const MetaDataTypes&... InMetaData, T InComponent) { RelinkedOutputs.Add(BlendChannel, FOutputProperty{ InComponent, MakeTuple( InMetaData... )}); });

			if (RelinkedOutputs.Num() > 0)
			{
				// Iterate all inputs that match the gathered outputs and add those as pre-animated state - these may not have the input
				FEntityTaskBuilder()
				.Read(BuiltInComponents->BlendChannelInput)
				.Read(BuiltInComponents->BoundObject)
				.Read(BuiltInComponents->InstanceHandle)
				.FilterAll({ PropertyDefinition->PropertyType })
				.Iterate_PerEntity(&Linker->EntityManager,
					[&RelinkedOutputs, CacheState](uint16 BlendChannel, UObject* Object, FInstanceHandle InstanceHandle)
					{
						if (const FOutputProperty* OutputComponent = RelinkedOutputs.Find(BlendChannel))
						{
							CacheState(Object, InstanceHandle, OutputComponent->MetaData.template Get<MetaDataIndices>()..., OutputComponent->PropertyComponent);
						}
					}
				);
			}
		}
	}

	struct FBaseToken : IMovieScenePreAnimatedToken
	{
		TTuple<MetaDataTypes...> MetaData;

		FBaseToken(const MetaDataTypes&... InMetaData)
			: MetaData(InMetaData...)
		{}
	};
	struct FFastToken : FBaseToken
	{
		StorageType CachedValue;
		uint16 PropertyOffset;

		explicit FFastToken(const UObject* Object, const MetaDataTypes&... InMetaData, uint16 InPropertyOffset)
			: FBaseToken(InMetaData...)
			, CachedValue{}
			, PropertyOffset(InPropertyOffset)
		{
			PropertyTraits::GetObjectPropertyValue(Object, InMetaData..., InPropertyOffset, CachedValue);
		}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			PropertyTraits::SetObjectPropertyValue(&Object, this->MetaData.template Get<MetaDataIndices>()..., PropertyOffset, CachedValue);
		}
	};
	struct FCustomToken : FBaseToken
	{
		StorageType CachedValue;
		const FCustomPropertyAccessor* Accessor;

		explicit FCustomToken(const UObject* Object, const MetaDataTypes&... InMetaData, const FCustomPropertyAccessor* InAccessor)
			: FBaseToken(InMetaData...)
			, CachedValue{}
			, Accessor(InAccessor)
		{
			PropertyTraits::GetObjectPropertyValue(Object, InMetaData..., *InAccessor, CachedValue);
		}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			PropertyTraits::SetObjectPropertyValue(&Object, this->MetaData.template Get<MetaDataIndices>()..., *Accessor, CachedValue);
		}
	};
	struct FSlowToken : FBaseToken
	{
		StorageType CachedValue;
		TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings;

		explicit FSlowToken(const UObject* Object, const MetaDataTypes&... InMetaData, const TSharedPtr<FTrackInstancePropertyBindings>& InPropertyBindings)
			: FBaseToken(InMetaData...)
			, CachedValue{}
			, PropertyBindings(InPropertyBindings)
		{
			PropertyTraits::GetObjectPropertyValue(Object, InMetaData..., InPropertyBindings.Get(), CachedValue);
		}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			PropertyTraits::SetObjectPropertyValue(&Object, this->MetaData.template Get<MetaDataIndices>()..., PropertyBindings.Get(), CachedValue);
		}
	};

	struct FProducer : IMovieScenePreAnimatedTokenProducer
	{
		TFunctionRef<IMovieScenePreAnimatedTokenPtr()> Function;

		explicit FProducer(TFunctionRef<IMovieScenePreAnimatedTokenPtr()> InFunction)
			: Function(InFunction)
		{}

		virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
		{
			return Function();
		}
	};

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, MetaDataTypes... InMetaData, uint16 PropertyOffset)
	{
		if (PropertyOffset != 0)
		{
			auto MakeToken = [PropertyOffset, InObject, InMetaData...]
			{
				return FFastToken(InObject, InMetaData..., PropertyOffset);
			};

			FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetFast(PropertyOffset);
			Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
		}
	}

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, MetaDataTypes... InMetaData, FCustomPropertyIndex InCustomIndex)
	{
		auto MakeToken = [this, InCustomIndex, InObject, InMetaData...]
		{
			return FCustomToken(InObject, InMetaData..., &this->CustomAccessors[InCustomIndex.Value]);
		};

		FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetCustom(InCustomIndex.Value);
		Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
	}

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, MetaDataTypes... InMetaData, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings)
	{
		auto MakeToken = [&PropertyBindings, InObject, InMetaData...]
		{
			return FSlowToken(InObject, InMetaData..., PropertyBindings);
		};

		FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetSlow(*PropertyBindings->GetPropertyPath());
		Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
	}
};

template<typename PropertyTraits>
using TPreAnimatedPropertyHelper = TPreAnimatedPropertyHelperBase<PropertyTraits, typename PropertyTraits::MetaDataType, TMakeIntegerSequence<int, PropertyTraits::MetaDataType::Num>>;


} // namespace MovieScene
} // namespace UE



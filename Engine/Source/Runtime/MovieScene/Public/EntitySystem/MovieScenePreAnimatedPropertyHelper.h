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

template<typename PropertyType>
struct TPreAnimatedPropertyHelper
{
	TPreAnimatedPropertyHelper(const FPropertyDefinition& PropertyDefinition, UMovieSceneEntitySystemLinker* InLinker)
		: Linker(InLinker)
	{
		PropertyTag = PropertyDefinition.PropertyType;
		if (PropertyDefinition.CustomPropertyRegistration)
		{
			CustomAccessors = PropertyDefinition.CustomPropertyRegistration->GetAccessors();
		}
		BuiltInComponents = FBuiltInComponentTypes::Get();
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
	FCustomAccessorView CustomAccessors;
	FGlobalPreAnimatedStateIDs AnimTypeIDs;
	FBuiltInComponentTypes* BuiltInComponents;
	FComponentTypeID PropertyTag;

private:

	template<typename T>
	void SavePreAnimatedStateForComponent(TComponentTypeID<T> ComponentType)
	{
		if (!Linker->EntityManager.ContainsComponent(ComponentType))
		{
			return;
		}

		FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

		auto CacheState =  [this, InstanceRegistry](UObject* Object, FInstanceHandle InstanceHandle, T InComponent)
		{
			IMovieScenePlayer* Player = InstanceRegistry->GetInstance(InstanceHandle).GetPlayer();
			if (Player->PreAnimatedState.IsGlobalCaptureEnabled())
			{
				this->SavePreAnimatedState(Player, Object, InComponent);
			}
		};

		// Run a task for any entity that has an instance handle directly. This is the case for the majority of unblended entities.
		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.Read(BuiltInComponents->InstanceHandle)
		.Read(ComponentType)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink, PropertyTag })
		.Iterate_PerEntity(&Linker->EntityManager, CacheState);

		// Special cases for blended entities running multiple inputs into outputs
		if (Linker->EntityManager.ContainsComponent(BuiltInComponents->BlendChannelOutput))
		{
			TMap<uint16, T> RelinkedOutputs;

			// Gather outputs that have been changed
			FEntityTaskBuilder()
			.Read(BuiltInComponents->BlendChannelOutput)
			.Read(ComponentType)
			.FilterAll({ BuiltInComponents->Tags.NeedsLink, BuiltInComponents->BoundObject, PropertyTag })
			.Iterate_PerEntity(&Linker->EntityManager, [&RelinkedOutputs](uint16 BlendChannel, T InComponent) { RelinkedOutputs.Add(BlendChannel, InComponent); });

			if (RelinkedOutputs.Num() > 0)
			{
				// Iterate all inputs that match the gathered outputs and add those as pre-animated state - these may not have the input
				FEntityTaskBuilder()
				.Read(BuiltInComponents->BlendChannelInput)
				.Read(BuiltInComponents->BoundObject)
				.Read(BuiltInComponents->InstanceHandle)
				.FilterAll({ PropertyTag })
				.Iterate_PerEntity(&Linker->EntityManager,
					[&RelinkedOutputs, CacheState](uint16 BlendChannel, UObject* Object, FInstanceHandle InstanceHandle)
					{
						if (const T* OutputComponent = RelinkedOutputs.Find(BlendChannel))
						{
							CacheState(Object, InstanceHandle, *OutputComponent);
						}
					}
				);
			}
		}
	}

	struct FFastToken : IMovieScenePreAnimatedToken
	{
		PropertyType CachedValue;
		uint16 PropertyOffset;

		explicit FFastToken(const PropertyType& InCachedValue, uint16 InPropertyOffset)
			: CachedValue(InCachedValue), PropertyOffset(InPropertyOffset)
		{}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			*reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(&Object) + PropertyOffset ) = CachedValue;
		}
	};
	struct FCustomToken : IMovieScenePreAnimatedToken
	{
		using SetterFunc = typename TCustomPropertyAccessorFunctions<PropertyType>::SetterFunc;

		PropertyType CachedValue;
		SetterFunc Setter;

		explicit FCustomToken(const PropertyType& InCachedValue, SetterFunc InSetter)
			: CachedValue(InCachedValue), Setter(InSetter)
		{}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			(*Setter)(&Object, CachedValue);
		}
	};
	struct FSlowToken : IMovieScenePreAnimatedToken
	{
		PropertyType CachedValue;
		TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings;

		explicit FSlowToken(const PropertyType& InCachedValue, TSharedPtr<FTrackInstancePropertyBindings> InPropertyBindings)
			: CachedValue(InCachedValue), PropertyBindings(InPropertyBindings)
		{}

		virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
		{
			PropertyBindings->CallFunction<PropertyType>(Object, CachedValue);
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

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, uint16 PropertyOffset)
	{
		if (PropertyOffset != 0)
		{
			auto MakeToken = [PropertyOffset, InObject]
			{
				PropertyType CurrentValue = *reinterpret_cast<PropertyType*>( reinterpret_cast<uint8*>(InObject) + PropertyOffset );
				return FFastToken(CurrentValue, PropertyOffset);
			};

			FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetFast(PropertyOffset);
			Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
		}
	}

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, FCustomPropertyIndex InCustomIndex)
	{
		auto MakeToken = [this, InCustomIndex, InObject]
		{
			const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(this->CustomAccessors[InCustomIndex.Value]);

			PropertyType CurrentValue = CustomAccessor.Functions.Getter(InObject);
			return FCustomToken(CurrentValue, CustomAccessor.Functions.Setter);
		};

		FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetCustom(InCustomIndex.Value);
		Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
	}

	void SavePreAnimatedState(IMovieScenePlayer* Player, UObject* InObject, TSharedPtr<FTrackInstancePropertyBindings> PropertyBindings)
	{
		auto MakeToken = [&PropertyBindings, InObject]
		{
			PropertyType CurrentValue = PropertyBindings->GetCurrentValue<PropertyType>(*InObject);
			return FSlowToken(CurrentValue, PropertyBindings);
		};

		FMovieSceneAnimTypeID TypeID = AnimTypeIDs.GetSlow(*PropertyBindings->GetPropertyPath());
		Player->SaveGlobalPreAnimatedState(*InObject, TypeID, FProducer(MakeToken));
	}
};


} // namespace MovieScene
} // namespace UE



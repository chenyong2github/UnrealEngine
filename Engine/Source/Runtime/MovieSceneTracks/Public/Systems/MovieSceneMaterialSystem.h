// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneEntityMutations.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "EntitySystem/MovieSceneComponentTypeInfo.h"
#include "MovieSceneTracksComponentTypes.h"

#include "MovieSceneMaterialSystem.generated.h"

USTRUCT()
struct FMovieScenePreAnimatedMaterialParameters
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviousMaterial = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviousParameterContainer = nullptr;
};

namespace UE::MovieScene
{

template<typename AccessorType, typename... RequiredComponents>
struct TPreAnimatedMaterialTraits : FBoundObjectPreAnimatedStateTraits
{
	using KeyType     = typename AccessorType::KeyType;
	using StorageType = UMaterialInterface*;

	static_assert(THasAddReferencedObjectForComponent<StorageType>::Value, "StorageType is not correctly exposed to the reference graph!");

	static UMaterialInterface* CachePreAnimatedValue(typename TCallTraits<RequiredComponents>::ParamType... InRequiredComponents)
	{
		return AccessorType{ InRequiredComponents... }.GetMaterial();
	}

	static void RestorePreAnimatedValue(const KeyType& InKeyType, UMaterialInterface* OldMaterial, const FRestoreStateParams& Params)
	{
		AccessorType{ InKeyType }.SetMaterial(OldMaterial);
	}
};

template<typename AccessorType, typename... RequiredComponents>
struct TPreAnimatedMaterialParameterTraits : FBoundObjectPreAnimatedStateTraits
{
	using KeyType     = typename AccessorType::KeyType;
	using StorageType = FMovieScenePreAnimatedMaterialParameters;

	static_assert(THasAddReferencedObjectForComponent<StorageType>::Value, "StorageType is not correctly exposed to the reference graph!");

	static FMovieScenePreAnimatedMaterialParameters CachePreAnimatedValue(typename TCallTraits<RequiredComponents>::ParamType... InRequiredComponents)
	{
		AccessorType Accessor{ InRequiredComponents... };

		FMovieScenePreAnimatedMaterialParameters Parameters;
		Parameters.PreviousMaterial = Accessor.GetMaterial();

		// If the current material we're overriding is already a material instance dynamic, copy it since we will be modifying the data. 
		// The copied material will be used to restore the values in RestoreState.
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Parameters.PreviousMaterial);
		if (MID)
		{
			Parameters.PreviousParameterContainer = DuplicateObject<UMaterialInterface>(MID, MID->GetOuter());
		}

		return Parameters;
	}

	static void RestorePreAnimatedValue(const KeyType& InKey, const FMovieScenePreAnimatedMaterialParameters& PreAnimatedValue, const FRestoreStateParams& Params)
	{
		AccessorType Accessor{ InKey };
		if (PreAnimatedValue.PreviousParameterContainer != nullptr)
		{
			// If we cached parameter values in CachePreAnimatedValue that means the previous material was already a MID
			// and we probably did not replace it with a new one when resolving bound materials. Therefore we
			// just copy the parameters back over without changing the material
			UMaterialInstanceDynamic* CurrentMID = Cast<UMaterialInstanceDynamic>(Accessor.GetMaterial());
			if (CurrentMID)
			{
				CurrentMID->CopyMaterialUniformParameters(PreAnimatedValue.PreviousParameterContainer);
				return;
			}
		}

		Accessor.SetMaterial(PreAnimatedValue.PreviousMaterial);
	}
};

template<typename AccessorType, typename... RequiredComponents>
class TMovieSceneMaterialSystem
{
public:

	using MaterialSwitcherStorageType = TPreAnimatedStateStorage<TPreAnimatedMaterialTraits<AccessorType, RequiredComponents...>>;
	using MaterialParameterStorageType = TPreAnimatedStateStorage<TPreAnimatedMaterialParameterTraits<AccessorType, RequiredComponents...>>;

	TSharedPtr<MaterialSwitcherStorageType> MaterialSwitcherStorage;
	TSharedPtr<MaterialParameterStorageType> MaterialParameterStorage;

	void OnLink(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents);
	void OnRun(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

	void SavePreAnimatedState(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents, const IMovieScenePreAnimatedStateSystemInterface::FPreAnimationParameters& InParameters);

protected:

	UE::MovieScene::FEntityComponentFilter MaterialSwitcherFilter;
	UE::MovieScene::FEntityComponentFilter MaterialParameterFilter;
};

template<typename AccessorType, typename... RequiredComponents>
struct TApplyMaterialSwitchers
{
	static void ForEachEntity(typename TCallTraits<RequiredComponents>::ParamType... Inputs, UObject* ObjectResult)
	{
		// ObjectResult must be a material
		UMaterialInterface* NewMaterial = Cast<UMaterialInterface>(ObjectResult);

		AccessorType Accessor(Inputs...);

		UMaterialInterface*        ExistingMaterial = Accessor.GetMaterial();
		UMaterialInstanceDynamic*  ExistingMID      = Cast<UMaterialInstanceDynamic>(ExistingMaterial);

		if (ExistingMID && ExistingMID->Parent && ExistingMID->Parent == NewMaterial)
		{
			// Do not re-assign materials when a dynamic instance is already assigned with the same parent (since that's basically the same material, just with animated parameters)
			// This is required for supporting material switchers alongside parameter tracks
			return;
		}

		Accessor.SetMaterial(NewMaterial);
	}
};

template<typename AccessorType, typename... RequiredComponents>
struct TInitializeBoundMaterials
{
	static void ForEachEntity(typename TCallTraits<RequiredComponents>::ParamType... Inputs, UObject*& OutDynamicMaterial)
	{
		AccessorType Accessor(Inputs...);

		UMaterialInterface* ExistingMaterial = Accessor.GetMaterial();

		if (!ExistingMaterial)
		{
			// The object no longer has a valid material assigned.
			// Rather than null the bound material and cause all downstream systems to have to check for null
			// We just leave it assigned to the previous MID, even if that won't have an effect any more
			return;
		}
		else if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(ExistingMaterial))
		{
			OutDynamicMaterial = MID;
			return;
		}

		if (OutDynamicMaterial && OutDynamicMaterial->IsA<UMaterialInstanceDynamic>())
		{
			return;
		}
		
		OutDynamicMaterial = Accessor.CreateDynamicMaterial(ExistingMaterial);
	}
};

template<typename...>
struct TAddBoundMaterialMutationImpl;

template<typename AccessorType, typename... RequiredComponents, int... Indices>
struct TAddBoundMaterialMutationImpl<AccessorType, TIntegerSequence<int, Indices...>, RequiredComponents...> : IMovieSceneEntityMutation
{
	TAddBoundMaterialMutationImpl(TComponentTypeID<RequiredComponents>... InRequiredComponents)
		: ComponentTypes(InRequiredComponents...)
	{
		BuiltInComponents = FBuiltInComponentTypes::Get();
		TracksComponents  = FMovieSceneTracksComponentTypes::Get();
	}
	virtual void CreateMutation(FEntityManager* EntityManager, FComponentMask* InOutEntityComponentTypes) const override
	{
		InOutEntityComponentTypes->Set(TracksComponents->BoundMaterial);
	}
	virtual void InitializeAllocation(FEntityAllocation* Allocation, const FComponentMask& AllocationType) const
	{
		TComponentWriter<UObject*> BoundMaterials = Allocation->WriteComponents(TracksComponents->BoundMaterial, FEntityAllocationWriteContext::NewAllocation());
		InitializeAllocation(Allocation, BoundMaterials, Allocation->ReadComponents(ComponentTypes.template Get<Indices>())...);
	}

	void InitializeAllocation(FEntityAllocation* Allocation, UObject** OutBoundMaterials, const RequiredComponents*... InRequiredComponents) const
	{
		const int32 Num = Allocation->Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			OutBoundMaterials[Index] = nullptr;
			TInitializeBoundMaterials<AccessorType, RequiredComponents...>::ForEachEntity(InRequiredComponents[Index]..., OutBoundMaterials[Index]);

			// @todo: We could remove the entity if it is null to avoid having to check for null downstream
			//        but that is a problem for some widgets that play animations in PreConstruct _before_
			//        the material is able to resolve correctly.
			// if (!ensureAlwaysMsgf(OutBoundMaterials[Index] != nullptr,
			//	TEXT("Unable to resolve material for %s. Material parameter tracks on this object will not function until it is resolved."),
			//	*AccessorType(InRequiredComponents[Index]...).ToString()))
			//{
			//	
			//	InvalidEntities.Add(Allocation->GetRawEntityIDs()[Index]);
			//}
		}
	}

	void RemoveInvalidBoundMaterials(UMovieSceneEntitySystemLinker* InLinker)
	{
#if 0
		// Currently disabled due to the comment in InitializeAllocation. Another approach would be to
		// Tag these differently, but that would require us mutating the entity manager during evaluation
		// if the material _does_ resolve in the future, and mutation during eval is not supported
		for (FMovieSceneEntityID InvalidEntity : InvalidEntities)
		{
			InLinker->EntityManager.RemoveComponent(InvalidEntity, TracksComponents->BoundMaterial);
		}
#endif
	}
private:

	FBuiltInComponentTypes* BuiltInComponents;
	FMovieSceneTracksComponentTypes* TracksComponents;

	/** Track entities that we added a BoundMaterial component to but were unable to resolve so we can remove the component. This should happen rarely */
	mutable TArray<FMovieSceneEntityID> InvalidEntities;

	TTuple<TComponentTypeID<RequiredComponents>...> ComponentTypes;
};


template<typename AccessorType, typename... RequiredComponents>
void TMovieSceneMaterialSystem<AccessorType, RequiredComponents...>::OnLink(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	MaterialSwitcherFilter.Reset();
	MaterialSwitcherFilter.All({ InRequiredComponents..., BuiltInComponents->ObjectResult });

	// Currently the only supported entities that we initialize are ones that contain Scalar, Vector or Color parameters
	// Imported entities are implicitly excluded by way of filtering by BoundObject, which do not exist on imported entities
	MaterialParameterFilter.Reset();
	MaterialParameterFilter.All({ InRequiredComponents... });
	MaterialParameterFilter.Any({ TracksComponents->ScalarParameterName, TracksComponents->ColorParameterName, TracksComponents->VectorParameterName });
}

template<typename AccessorType, typename... RequiredComponents>
void TMovieSceneMaterialSystem<AccessorType, RequiredComponents...>::OnRun(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	ESystemPhase CurrentPhase = Linker->GetActiveRunner()->GetCurrentPhase();
	if (CurrentPhase == ESystemPhase::Instantiation)
	{
		// Only mutate things that are tagged as requiring linking
		FEntityComponentFilter Filter(MaterialParameterFilter);
		Filter.All({ BuiltInComponents->Tags.NeedsLink});

		// Initialize bound dynamic materials (for material parameters)
		using MutationType = TAddBoundMaterialMutationImpl<AccessorType, TMakeIntegerSequence<int, sizeof...(RequiredComponents)>, RequiredComponents...>;
		MutationType BindMaterialsMutation(InRequiredComponents...);

		Linker->EntityManager.MutateAll(Filter, BindMaterialsMutation);
		BindMaterialsMutation.RemoveInvalidBoundMaterials(Linker);
	}
	else if (CurrentPhase == ESystemPhase::Evaluation)
	{
		using ApplyMaterialSwitchers = TApplyMaterialSwitchers<AccessorType, RequiredComponents...>;
		using InitializeBoundMaterials = TInitializeBoundMaterials<AccessorType, RequiredComponents...>;

		// Apply material switchers
		FEntityTaskBuilder()
		.ReadAllOf(InRequiredComponents...)
		.Read(BuiltInComponents->ObjectResult)
		.SetDesiredThread(Linker->EntityManager.GetDispatchThread())
		.template Dispatch_PerEntity<ApplyMaterialSwitchers>(&Linker->EntityManager, InPrerequisites, &Subsequents);

		// Initialize bound dynamic materials
		FEntityTaskBuilder()
		.ReadAllOf(InRequiredComponents...)
		.Write(TracksComponents->BoundMaterial)
		.SetDesiredThread(Linker->EntityManager.GetDispatchThread())
		.template Dispatch_PerEntity<InitializeBoundMaterials>(&Linker->EntityManager, InPrerequisites, &Subsequents);
	}
}

template<typename AccessorType, typename... RequiredComponents>
void TMovieSceneMaterialSystem<AccessorType, RequiredComponents...>::SavePreAnimatedState(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<RequiredComponents>... InRequiredComponents, const IMovieScenePreAnimatedStateSystemInterface::FPreAnimationParameters& InParameters)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes*          BuiltInComponents = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents  = FMovieSceneTracksComponentTypes::Get();

	// If we have material results to apply save those as well
	if (Linker->EntityManager.Contains(MaterialSwitcherFilter))
	{
		TSavePreAnimatedStateParams<RequiredComponents...> Params;
		Params.AdditionalFilter = MaterialSwitcherFilter;

		MaterialSwitcherStorage->BeginTrackingAndCachePreAnimatedValuesTask(Linker, Params, InRequiredComponents...);
	}

	// If we have bound materials to resolve save the current material
	if (Linker->EntityManager.Contains(MaterialParameterFilter))
	{
		TSavePreAnimatedStateParams<RequiredComponents...> Params;
		Params.AdditionalFilter = MaterialParameterFilter;

		MaterialParameterStorage->BeginTrackingAndCachePreAnimatedValuesTask(Linker, Params, InRequiredComponents...);
	}
}

} // namespace UE::MovieScene
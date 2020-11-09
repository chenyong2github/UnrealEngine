// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePropertyInstantiator.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"


#include "Algo/IndexOf.h"

UMovieScenePropertyInstantiatorSystem::UMovieScenePropertyInstantiatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	BuiltInComponents = FBuiltInComponentTypes::Get();

	RecomposerImpl.OnGetPropertyInfo = FOnGetPropertyRecomposerPropertyInfo::CreateUObject(
				this, &UMovieScenePropertyInstantiatorSystem::FindPropertyFromSource);

	SystemExclusionContext = EEntitySystemContext::Interrogation;
	RelevantComponent = BuiltInComponents->PropertyBinding;
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), BuiltInComponents->BoundObject);
		DefineComponentProducer(GetClass(), BuiltInComponents->BlendChannelInput);
		DefineComponentProducer(GetClass(), BuiltInComponents->SymbolicTags.CreatesEntities);
	}

	CleanFastPathMask.SetAll({ BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty, BuiltInComponents->CustomPropertyIndex });
}

UE::MovieScene::FPropertyStats UMovieScenePropertyInstantiatorSystem::GetStatsForProperty(UE::MovieScene::FCompositePropertyTypeID PropertyID) const
{
	const int32 Index = PropertyID.AsIndex();
	if (PropertyStats.IsValidIndex(Index))
	{
		return PropertyStats[Index];
	}

	return UE::MovieScene::FPropertyStats();
}

void UMovieScenePropertyInstantiatorSystem::OnLink()
{
	Linker->Events.CleanTaggedGarbage.AddUObject(this, &UMovieScenePropertyInstantiatorSystem::CleanTaggedGarbage);

	CleanFastPathMask.CombineWithBitwiseOR(Linker->EntityManager.GetComponents()->GetMigrationMask(), EBitwiseOperatorFlags::MaxSize);
}

void UMovieScenePropertyInstantiatorSystem::OnUnlink()
{
	Linker->Events.CleanTaggedGarbage.RemoveAll(this);
}

void UMovieScenePropertyInstantiatorSystem::CleanTaggedGarbage(UMovieSceneEntitySystemLinker*)
{
	using namespace UE::MovieScene;

	TBitArray<> InvalidatedProperties;
	DiscoverInvalidatedProperties(InvalidatedProperties);

	if (InvalidatedProperties.Num() != 0)
	{
		ProcessInvalidatedProperties(InvalidatedProperties);
	}
}

void UMovieScenePropertyInstantiatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	TBitArray<> InvalidatedProperties;
	DiscoverInvalidatedProperties(InvalidatedProperties);

	if (InvalidatedProperties.Num() != 0)
	{
		ProcessInvalidatedProperties(InvalidatedProperties);
	}

	// Kick off initial value gather task immediately
	if (InitialValueStateTasks.Find(true) != INDEX_NONE)
	{
		AssignInitialValues(InPrerequisites, Subsequents);
	}

	if (CachePreAnimatedStateTasks.Find(true) != INDEX_NONE)
	{
		UMovieSceneCachePreAnimatedStateSystem* PreAnimatedState = Linker->LinkSystem<UMovieSceneCachePreAnimatedStateSystem>();
		Linker->SystemGraph.AddReference(this, PreAnimatedState);
	}

	ObjectPropertyToResolvedIndex.Compact();
	EntityToProperty.Compact();
}

void UMovieScenePropertyInstantiatorSystem::DiscoverInvalidatedProperties(TBitArray<>& OutInvalidatedProperties)
{
	using namespace UE::MovieScene;

	TBitArray<> InvalidatedProperties;

	TArrayView<const FPropertyDefinition> Properties = this->BuiltInComponents->PropertyRegistry.GetProperties();

	PropertyStats.SetNum(Properties.Num());

	auto VisitNewProperties = [this, Properties, &OutInvalidatedProperties](const FEntityAllocation* Allocation, FReadEntityIDs EntityIDsAccessor, TRead<UObject*> ObjectComponents, TRead<FMovieScenePropertyBinding> PropertyBindingComponents)
	{
		const int32 PropertyDefinitionIndex = Algo::IndexOfByPredicate(Properties, [=](const FPropertyDefinition& InDefinition){ return Allocation->HasComponent(InDefinition.PropertyType); });
		if (PropertyDefinitionIndex == INDEX_NONE)
		{
			return;
		}

		const FPropertyDefinition& PropertyDefinition = Properties[PropertyDefinitionIndex];

		FCustomAccessorView CustomAccessors = PropertyDefinition.CustomPropertyRegistration ? PropertyDefinition.CustomPropertyRegistration->GetAccessors() : FCustomAccessorView();

		UObject* const * ObjectPtrs = ObjectComponents.Resolve(Allocation);
		const FMovieSceneEntityID* EntityIDs = EntityIDsAccessor.Resolve(Allocation);
		const FMovieScenePropertyBinding* PropertyPtrs = PropertyBindingComponents.Resolve(Allocation);

		for (int32 Index = 0; Index < Allocation->Num(); ++Index)
		{
			const int32 PropertyIndex = this->ResolveProperty(CustomAccessors, ObjectPtrs[Index], PropertyPtrs[Index], PropertyDefinitionIndex);
			
			// If the property did not resolve, we still add it to the LUT
			// So that the ensure inside VisitExpiredEntities only fires
			// for genuine link/unlink disparities
			this->EntityToProperty.Add(EntityIDs[Index], PropertyIndex);

			if (PropertyIndex != INDEX_NONE)
			{
				this->Contributors.Add(PropertyIndex, EntityIDs[Index]);
				this->NewContributors.Add(PropertyIndex, EntityIDs[Index]);

				OutInvalidatedProperties.PadToNum(PropertyIndex + 1, false);
				OutInvalidatedProperties[PropertyIndex] = true;

				if (PropertyDefinition.PreAnimatedValue)
				{
					SaveGlobalStateTasks.PadToNum(PropertyDefinitionIndex + 1, false);
					SaveGlobalStateTasks[PropertyDefinitionIndex] = true;
				}
			}
		}
	};

	FEntityTaskBuilder()
	.ReadEntityIDs()
	.Read(BuiltInComponents->BoundObject)
	.Read(BuiltInComponents->PropertyBinding)
	.FilterAll({ BuiltInComponents->Tags.NeedsLink })
	.Iterate_PerAllocation(&Linker->EntityManager, VisitNewProperties);


	auto VisitExpiredEntities = [this, &OutInvalidatedProperties](FMovieSceneEntityID EntityID)
	{
		const int32* PropertyIndexPtr = this->EntityToProperty.Find(EntityID);
		if (ensureMsgf(PropertyIndexPtr, TEXT("Could not find entity to clean up from linker entity ID - this indicates VisitNewProperties never got called for this entity, or a garbage collection has somehow destroyed the entity without flushing the ecs.")))
		{
			const int32 PropertyIndex = *PropertyIndexPtr;
			if (PropertyIndex != INDEX_NONE)
			{
				OutInvalidatedProperties.PadToNum(PropertyIndex + 1, false);
				OutInvalidatedProperties[PropertyIndex] = true;

				this->Contributors.Remove(PropertyIndex, EntityID);
			}

			// Always remove the entity ID from the LUT
			this->EntityToProperty.Remove(EntityID);
		}
	};

	FEntityTaskBuilder()
	.ReadEntityIDs()
	.FilterAll({ BuiltInComponents->BoundObject, BuiltInComponents->PropertyBinding, BuiltInComponents->Tags.NeedsUnlink })
	.Iterate_PerEntity(&Linker->EntityManager, VisitExpiredEntities);
}

void UMovieScenePropertyInstantiatorSystem::ProcessInvalidatedProperties(const TBitArray<>& InvalidatedProperties)
{
	using namespace UE::MovieScene;

	TBitArray<> StaleProperties;

	TArrayView<const FPropertyDefinition> Properties = BuiltInComponents->PropertyRegistry.GetProperties();

	FPropertyParameters Params;

	// This is all random access at this point :(
	for (TConstSetBitIterator<> It(InvalidatedProperties); It; ++It)
	{
		const int32 PropertyIndex = It.GetIndex();
		if (!ResolvedProperties.IsValidIndex(PropertyIndex))
		{
			continue;
		}

		// Update our view of how this property is animated
		Params.PropertyInfo = &ResolvedProperties[PropertyIndex];
		Params.PropertyDefinition = &Properties[Params.PropertyInfo->PropertyDefinitionIndex];
		Params.PropertyInfoIndex = PropertyIndex;

		UpdatePropertyInfo(Params);

		// Does it have anything at all contributing to it anymore?
		if (!Contributors.Contains(PropertyIndex))
		{
			StaleProperties.PadToNum(PropertyIndex + 1, false);
			StaleProperties[PropertyIndex] = true;
		}
		// Does it support fast path?
		else if (PropertySupportsFastPath(Params))
		{
			InitializeFastPath(Params);
		}
		// Else use the (slightly more) expensive blend path
		else
		{
			InitializeBlendPath(Params);
		}


		const int32 PropertyDefinitionIndex = Params.PropertyInfo->PropertyDefinitionIndex;

		const bool bHasPreAnimatedValue = Params.PropertyDefinition->PreAnimatedValue && Linker->EntityManager.HasComponent(Params.PropertyInfo->PropertyEntityID, Params.PropertyDefinition->PreAnimatedValue);
		if (Params.PropertyDefinition->PreAnimatedValue && Params.PropertyInfo->bWantsRestoreState && !bHasPreAnimatedValue)
		{
			Linker->EntityManager.AddComponents(Params.PropertyInfo->PropertyEntityID, { BuiltInComponents->Tags.RestoreState, BuiltInComponents->Tags.CachePreAnimatedValue, Params.PropertyDefinition->PreAnimatedValue });

			CachePreAnimatedStateTasks.PadToNum(PropertyDefinitionIndex+1, false);
			CachePreAnimatedStateTasks[PropertyDefinitionIndex] = true;
		}
		else if (!Params.PropertyInfo->bWantsRestoreState && bHasPreAnimatedValue)
		{
			Linker->EntityManager.RemoveComponents(Params.PropertyInfo->PropertyEntityID, { Params.PropertyDefinition->PreAnimatedValue, BuiltInComponents->Tags.RestoreState });
		}
	}

	// Restore and destroy stale properties
	if (StaleProperties.Find(true) != INDEX_NONE)
	{
		for (TConstSetBitIterator<> It(StaleProperties); It; ++It)
		{
			const int32 PropertyIndex = It.GetIndex();
			FObjectPropertyInfo* PropertyInfo = &ResolvedProperties[PropertyIndex];

			RestorePreAnimatedStateTasks.PadToNum(PropertyInfo->PropertyDefinitionIndex+1, false);
			RestorePreAnimatedStateTasks[PropertyInfo->PropertyDefinitionIndex] = true;

			if (PropertyInfo->BlendChannel != INVALID_BLEND_CHANNEL)
			{
				if (UMovieSceneBlenderSystem* Blender = PropertyInfo->Blender.Get())
				{
					Blender->ReleaseBlendChannel(PropertyInfo->BlendChannel);
				}
				Linker->EntityManager.AddComponents(PropertyInfo->PropertyEntityID, BuiltInComponents->FinishedMask);

				if (PropertyInfo->EmptyChannels.Find(true) != INDEX_NONE)
				{
					--PropertyStats[PropertyInfo->PropertyDefinitionIndex].NumPartialProperties;
				}
			}

			--PropertyStats[PropertyInfo->PropertyDefinitionIndex].NumProperties;
			ResolvedProperties.RemoveAt(PropertyIndex);

			// PropertyInfo is now garbage
		}

		// @todo: If perf is a real issue with this look, we could call ObjectPropertyToResolvedIndex.Remove(MakeTuple(PropertyInfo->BoundObject, PropertyInfo->PropertyPath));
		// In the loop above, but it is possible that BoundObject no longer relates to a valid object at that point
		for (auto It = ObjectPropertyToResolvedIndex.CreateIterator(); It; ++It)
		{
			if (!ResolvedProperties.IsAllocated(It.Value()))
			{
				It.RemoveCurrent();
			}
		}
	}

	NewContributors.Empty();
}

void UMovieScenePropertyInstantiatorSystem::UpdatePropertyInfo(const FPropertyParameters& Params)
{
	using namespace UE::MovieScene;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(*Params.PropertyDefinition);

	FChannelMask EmptyChannels(true, Params.PropertyDefinition->CompositeSize);

	bool bWantsRestoreState = false;
	int32 NumContributors = 0;

	for (auto ContributorIt = Contributors.CreateConstKeyIterator(Params.PropertyInfoIndex); ContributorIt; ++ContributorIt)
	{
		++NumContributors;

		if (!bWantsRestoreState && Linker->EntityManager.HasComponent(ContributorIt.Value(), BuiltInComponents->Tags.RestoreState))
		{
			bWantsRestoreState = true;
		}

		for (int32 CompositeIndex = 0; CompositeIndex < Params.PropertyDefinition->CompositeSize; ++CompositeIndex)
		{
			if (EmptyChannels[CompositeIndex] == false)
			{
				continue;
			}

			FComponentTypeID ThisChannel = Composites[CompositeIndex].ComponentTypeID;
			if (ThisChannel && Linker->EntityManager.HasComponent(ContributorIt.Value(), ThisChannel))
			{
				EmptyChannels[CompositeIndex] = false;
			}
		}
	}

	// Reset the restore state status of the property if we still have contributors
	// We do not do this if there are no contributors to ensure that stale properties are restored correctly
	if (NumContributors > 0)
	{
		const bool bWasPartial = Params.PropertyInfo->EmptyChannels.Find(true) != INDEX_NONE;
		const bool bIsPartial  = EmptyChannels.Find(true) != INDEX_NONE;

		if (bWasPartial != bIsPartial)
		{
			const int32 StatIndex = Params.PropertyInfo->PropertyDefinitionIndex;
			PropertyStats[StatIndex].NumPartialProperties += bIsPartial ? 1 : -1;
		}

		Params.PropertyInfo->EmptyChannels = EmptyChannels;
		Params.PropertyInfo->bWantsRestoreState = bWantsRestoreState;
	}
}

bool UMovieScenePropertyInstantiatorSystem::PropertySupportsFastPath(const FPropertyParameters& Params) const
{
	using namespace UE::MovieScene;

	// Properties that are already blended, or are currently animated must use the blend path
	if (ResolvedProperties[Params.PropertyInfoIndex].BlendChannel != INVALID_BLEND_CHANNEL || Params.PropertyInfo->PropertyEntityID.IsValid())
	{
		return false;
	}

	int32 NumContributors = 0;
	for (auto It = Contributors.CreateConstKeyIterator(Params.PropertyInfoIndex); It; ++It)
	{
		++NumContributors;
		if (NumContributors > 1)
		{
			return false;
		}

		FComponentMask Type = Linker->EntityManager.GetEntityType(It.Value());
		if (Type.Contains(BuiltInComponents->Tags.RelativeBlend) || 
				Type.Contains(BuiltInComponents->Tags.AdditiveBlend) || 
				Type.Contains(BuiltInComponents->Tags.AdditiveFromBaseBlend) || 
				Type.Contains(BuiltInComponents->WeightAndEasingResult))
		{
			return false;
		}
	}

	return true;
}

void UMovieScenePropertyInstantiatorSystem::InitializeFastPath(const FPropertyParameters& Params)
{
	using namespace UE::MovieScene;

	FMovieSceneEntityID SoleContributor = Contributors.FindChecked(Params.PropertyInfoIndex);

	// Have we ever seen this property before?
	if (SoleContributor == Params.PropertyInfo->PropertyEntityID)
	{
		return;
	}

	Params.PropertyInfo->PropertyEntityID = SoleContributor;

	check(Params.PropertyInfo->BlendChannel == INVALID_BLEND_CHANNEL);
	switch (Params.PropertyInfo->Property->GetIndex())
	{
	case 0:
		Linker->EntityManager.AddComponent(SoleContributor, BuiltInComponents->FastPropertyOffset,  Params.PropertyInfo->Property->template Get<uint16>());
		break;
	case 1:
		Linker->EntityManager.AddComponent(SoleContributor, BuiltInComponents->CustomPropertyIndex, Params.PropertyInfo->Property->template Get<FCustomPropertyIndex>());
		break;
	case 2:
		Linker->EntityManager.AddComponent(SoleContributor, BuiltInComponents->SlowProperty,        Params.PropertyInfo->Property->template Get<FSlowPropertyPtr>());
		break;
	}
}

void UMovieScenePropertyInstantiatorSystem::InitializeBlendPath(const FPropertyParameters& Params)
{
	using namespace UE::MovieScene;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(*Params.PropertyDefinition);

	UClass* BlenderClass = UMovieScenePiecewiseFloatBlenderSystem::StaticClass();

	// Ensure contributors all have the necessary blend inputs and tags
	for (auto ContributorIt = Contributors.CreateConstKeyIterator(Params.PropertyInfoIndex); ContributorIt; ++ContributorIt)
	{
		FMovieSceneEntityID Contributor = ContributorIt.Value();
		
		TComponentPtr<const TSubclassOf<UMovieSceneBlenderSystem>> BlenderTypeComponent = Linker->EntityManager.ReadComponent(Contributor, BuiltInComponents->BlenderType);
		if (BlenderTypeComponent)
		{
			BlenderClass = BlenderTypeComponent->Get();
			break;
		}
	}

	check(BlenderClass);

	UMovieSceneBlenderSystem* ExistingBlender = Params.PropertyInfo->Blender.Get();
	if (ExistingBlender && BlenderClass != ExistingBlender->GetClass())
	{
		ExistingBlender->ReleaseBlendChannel(Params.PropertyInfo->BlendChannel);
		Params.PropertyInfo->BlendChannel = INVALID_BLEND_CHANNEL;
	}

	Params.PropertyInfo->Blender = CastChecked<UMovieSceneBlenderSystem>(Linker->LinkSystem(BlenderClass));

	const bool bWasAlreadyBlended = Params.PropertyInfo->BlendChannel != INVALID_BLEND_CHANNEL;
	if (!bWasAlreadyBlended)
	{
		Params.PropertyInfo->BlendChannel = Params.PropertyInfo->Blender->AllocateBlendChannel();
	}

	FComponentMask NewMask;
	FComponentMask OldMask;

	if (!bWasAlreadyBlended)
	{
		NewMask.Set(Params.PropertyDefinition->InitialValueType);
		InitialValueStateTasks.PadToNum(Params.PropertyInfo->PropertyDefinitionIndex+1, false);
		InitialValueStateTasks[Params.PropertyInfo->PropertyDefinitionIndex] = true;

		for (int32 Index = 0; Index < Composites.Num(); ++Index)
		{
			if (Params.PropertyInfo->EmptyChannels[Index] == false)
			{
				NewMask.Set(Composites[Index].ComponentTypeID);
			}
		}
		NewMask.Set(Params.PropertyDefinition->PropertyType);

		FMovieSceneEntityID NewEntityID;
		switch (Params.PropertyInfo->Property->GetIndex())
		{
		// Never seen this property before
		case 0:
			NewEntityID = FEntityBuilder()
			.Add(BuiltInComponents->FastPropertyOffset,      Params.PropertyInfo->Property->template Get<uint16>())
			.Add(BuiltInComponents->BoundObject,             Params.PropertyInfo->BoundObject)
			.Add(BuiltInComponents->BlendChannelOutput,      Params.PropertyInfo->BlendChannel)
			.AddTagConditional(BuiltInComponents->Tags.MigratedFromFastPath, Params.PropertyInfo->PropertyEntityID.IsValid())
			.AddTagConditional(BuiltInComponents->Tags.RestoreState, Params.PropertyInfo->bWantsRestoreState)
			.AddTag(BuiltInComponents->Tags.NeedsLink)
			.AddMutualComponents()
			.CreateEntity(&Linker->EntityManager, NewMask);
			break;

		case 1:
			NewEntityID = FEntityBuilder()
			.Add(BuiltInComponents->CustomPropertyIndex, Params.PropertyInfo->Property->template Get<FCustomPropertyIndex>())
			.Add(BuiltInComponents->BoundObject,         Params.PropertyInfo->BoundObject)
			.Add(BuiltInComponents->BlendChannelOutput,  Params.PropertyInfo->BlendChannel)
			.AddTagConditional(BuiltInComponents->Tags.MigratedFromFastPath, Params.PropertyInfo->PropertyEntityID.IsValid())
			.AddTagConditional(BuiltInComponents->Tags.RestoreState, Params.PropertyInfo->bWantsRestoreState)
			.AddTag(BuiltInComponents->Tags.NeedsLink)
			.AddMutualComponents()
			.CreateEntity(&Linker->EntityManager, NewMask);
			break;

		case 2:
			NewEntityID = FEntityBuilder()
			.Add(BuiltInComponents->SlowProperty,            Params.PropertyInfo->Property->template Get<FSlowPropertyPtr>())
			.Add(BuiltInComponents->BoundObject,             Params.PropertyInfo->BoundObject)
			.Add(BuiltInComponents->BlendChannelOutput,      Params.PropertyInfo->BlendChannel)
			.AddTagConditional(BuiltInComponents->Tags.MigratedFromFastPath, Params.PropertyInfo->PropertyEntityID.IsValid())
			.AddTagConditional(BuiltInComponents->Tags.RestoreState, Params.PropertyInfo->bWantsRestoreState)
			.AddTag(BuiltInComponents->Tags.NeedsLink)
			.AddMutualComponents()
			.CreateEntity(&Linker->EntityManager, NewMask);
			break;
		}

		if (Params.PropertyInfo->PropertyEntityID)
		{
			// Move any migratable components over from the existing fast-path entity
			Linker->EntityManager.CopyComponents(Params.PropertyInfo->PropertyEntityID, NewEntityID, Linker->EntityManager.GetComponents()->GetMigrationMask());

			// Add blend inputs on the first contributor, which was using the fast-path
			Linker->EntityManager.AddComponent(Params.PropertyInfo->PropertyEntityID, BuiltInComponents->BlendChannelInput, Params.PropertyInfo->BlendChannel);
			Linker->EntityManager.RemoveComponents(Params.PropertyInfo->PropertyEntityID, CleanFastPathMask);
		}

		Params.PropertyInfo->PropertyEntityID = NewEntityID;
	}
	else
	{
		FComponentMask NewEntityType = Linker->EntityManager.GetEntityType(Params.PropertyInfo->PropertyEntityID);

		// Ensure the property has only the exact combination of components that constitute its animation
		for (int32 Index = 0; Index < Composites.Num(); ++Index)
		{
			FComponentTypeID Composite = Composites[Index].ComponentTypeID;
			NewEntityType[Composite] = (Params.PropertyInfo->EmptyChannels[Index] != true);
		}
		NewEntityType.Set(Params.PropertyDefinition->PropertyType);

		Linker->EntityManager.ChangeEntityType(Params.PropertyInfo->PropertyEntityID, NewEntityType);
	}

	// Ensure contributors all have the necessary blend inputs and tags
	for (auto ContributorIt = NewContributors.CreateConstKeyIterator(Params.PropertyInfoIndex); ContributorIt; ++ContributorIt)
	{
		FMovieSceneEntityID Contributor = ContributorIt.Value();
		Linker->EntityManager.AddComponent(Contributor, BuiltInComponents->BlendChannelInput, Params.PropertyInfo->BlendChannel);
		Linker->EntityManager.RemoveComponents(Contributor, CleanFastPathMask);
	}
}

int32 UMovieScenePropertyInstantiatorSystem::FindCustomAccessorIndex(UE::MovieScene::FCustomAccessorView Accessors, UClass* ClassType, FName PropertyPath)
{
	using namespace UE::MovieScene;

	UClass* StopIterationAt = UObject::StaticClass();

	while (ClassType != StopIterationAt)
	{
		for (int32 Index = 0; Index < Accessors.Num(); ++Index)
		{
			const FCustomPropertyAccessor& Accessor = Accessors[Index];
			if (Accessor.Class == ClassType && Accessor.PropertyPath == PropertyPath)
			{
				return Index;
			}
		}
		ClassType = ClassType->GetSuperClass();
	}

	return INDEX_NONE;
}

TOptional<uint16> UMovieScenePropertyInstantiatorSystem::ComputeFastPropertyPtrOffset(UClass* ObjectClass, const FMovieScenePropertyBinding& PropertyBinding)
{
	using namespace UE::MovieScene;

	FProperty* Property = ObjectClass->FindPropertyByName(PropertyBinding.PropertyName);
	// @todo: Constructing FNames from strings is _very_ costly and we really shouldn't be doing this at runtime.
	UFunction* Setter   = ObjectClass->FindFunctionByName(*(FString("Set") + PropertyBinding.PropertyName.ToString()));
	if (Property && !Setter)
	{
		UObject* DefaultObject   = ObjectClass->GetDefaultObject();
		uint8*   PropertyAddress = Property->ContainerPtrToValuePtr<uint8>(DefaultObject);
		int32    PropertyOffset  = PropertyAddress - reinterpret_cast<uint8*>(DefaultObject);

		if (ensureMsgf(PropertyOffset >= 0 && PropertyOffset < int32(uint16(0xFFFF)), TEXT("Property offset of more than 65535 bytes - this is most likely an error and is not supported by fast property accessors.")))
		{
			return uint16(PropertyOffset);
		}
	}

	return TOptional<uint16>();
}

int32 UMovieScenePropertyInstantiatorSystem::ResolveProperty(UE::MovieScene::FCustomAccessorView CustomAccessors, UObject* Object, const FMovieScenePropertyBinding& PropertyBinding, int32 PropertyDefinitionIndex)
{
	using namespace UE::MovieScene;

	TTuple<UObject*, FName> Key = MakeTuple(Object, PropertyBinding.PropertyPath);
	if (const int32* ExistingPropertyIndex = ObjectPropertyToResolvedIndex.Find(Key))
	{
		return *ExistingPropertyIndex;
	}


	FObjectPropertyInfo NewInfo;

	NewInfo.BoundObject  = Object;
	NewInfo.PropertyPath = PropertyBinding.PropertyPath;
	NewInfo.PropertyDefinitionIndex = PropertyDefinitionIndex;

	UClass* Class = Object->GetClass();

	if (CustomAccessors.Num() != 0)
	{
		const int32 CustomPropertyIndex = FindCustomAccessorIndex(CustomAccessors, Class, PropertyBinding.PropertyPath);
		if (CustomPropertyIndex != INDEX_NONE)
		{
			check(CustomPropertyIndex < MAX_uint16);

			// This property has a custom property accessor that can apply properties through a static function ptr.
			// Just add the function ptrs to the property entity so they can be called directly
			NewInfo.Property.Emplace(TInPlaceType<FCustomPropertyIndex>(), FCustomPropertyIndex{ static_cast<uint16>(CustomPropertyIndex) });
		}
	}

	if (PropertyBinding.CanUseClassLookup())
	{
		TOptional<uint16> FastPtrOffset = ComputeFastPropertyPtrOffset(Class, PropertyBinding);
		if (FastPtrOffset.IsSet())
		{
			// This property/object combination has no custom setter function and a constant property offset from the base ptr for all instances of the object.
			NewInfo.Property.Emplace(TInPlaceType<uint16>(), FastPtrOffset.GetValue());
		}
	}

	// None of the above optimized paths can apply to this property (probably because it has a setter function or because it is within a compound property), so we must use the slow property bindings
	if (!NewInfo.Property.IsSet())
	{
		TSharedPtr<FTrackInstancePropertyBindings> SlowBindings = MakeShared<FTrackInstancePropertyBindings>(PropertyBinding.PropertyName, PropertyBinding.PropertyPath.ToString());
		if (SlowBindings->GetProperty(*Object) == nullptr)
		{
			UE_LOG(LogMovieScene, Warning, TEXT("Unable to resolve property '%s' from '%s' instance '%s'"), *PropertyBinding.PropertyPath.ToString(), *Object->GetClass()->GetName(), *Object->GetName());
			return INDEX_NONE;
		}

		NewInfo.Property.Emplace(TInPlaceType<FSlowPropertyPtr>(), SlowBindings);
	}

	const int32 NewPropertyIndex = ResolvedProperties.Add(NewInfo);

	ObjectPropertyToResolvedIndex.Add(Key, NewPropertyIndex);

	++PropertyStats[PropertyDefinitionIndex].NumProperties;

	return NewPropertyIndex;
}

UE::MovieScene::FPropertyRecomposerPropertyInfo UMovieScenePropertyInstantiatorSystem::FindPropertyFromSource(FMovieSceneEntityID EntityID, UObject* Object) const
{
	using namespace UE::MovieScene;

	TComponentPtr<const FMovieScenePropertyBinding> PropertyBinding = Linker->EntityManager.ReadComponent(EntityID, BuiltInComponents->PropertyBinding);
	if (!PropertyBinding)
	{
		return FPropertyRecomposerPropertyInfo::Invalid();
	}

	TTuple<UObject*, FName> Key = MakeTuple(Object, PropertyBinding->PropertyPath);
	if (const int32* PropertyIndex = ObjectPropertyToResolvedIndex.Find(Key))
	{
		const FObjectPropertyInfo& PropertyInfo = ResolvedProperties[*PropertyIndex];
		return FPropertyRecomposerPropertyInfo { PropertyInfo.BlendChannel, PropertyInfo.Blender.Get(), PropertyInfo.PropertyEntityID };
	}

	return FPropertyRecomposerPropertyInfo::Invalid();
}

void UMovieScenePropertyInstantiatorSystem::AssignInitialValues(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (TConstSetBitIterator<> TypesToCache(InitialValueStateTasks); TypesToCache; ++TypesToCache)
	{
		FCompositePropertyTypeID PropertyID = FCompositePropertyTypeID::FromIndex(TypesToCache.GetIndex());

		const FPropertyDefinition& Definition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyID);
		Definition.Handler->DispatchCacheInitialValueTasks(Definition, InPrerequisites, Subsequents, Linker);
	}

	InitialValueStateTasks.Empty();
}

void UMovieScenePropertyInstantiatorSystem::SavePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (TConstSetBitIterator<> TypesToCache(CachePreAnimatedStateTasks); TypesToCache; ++TypesToCache)
	{
		FCompositePropertyTypeID PropertyID = FCompositePropertyTypeID::FromIndex(TypesToCache.GetIndex());

		const FPropertyDefinition& Definition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyID);
		Definition.Handler->DispatchCachePreAnimatedTasks(Definition, InPrerequisites, Subsequents, Linker);
	}

	CachePreAnimatedStateTasks.Empty();
}

void UMovieScenePropertyInstantiatorSystem::SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (TConstSetBitIterator<> TypesToCache(SaveGlobalStateTasks); TypesToCache; ++TypesToCache)
	{
		FCompositePropertyTypeID PropertyID = FCompositePropertyTypeID::FromIndex(TypesToCache.GetIndex());

		const FPropertyDefinition& Definition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyID);
		Definition.Handler->SaveGlobalPreAnimatedState(Definition, Linker);
	}

	SaveGlobalStateTasks.Empty();
}

void UMovieScenePropertyInstantiatorSystem::RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	for (TConstSetBitIterator<> TypesToRestore(RestorePreAnimatedStateTasks); TypesToRestore; ++TypesToRestore)
	{
		FCompositePropertyTypeID PropertyID = FCompositePropertyTypeID::FromIndex(TypesToRestore.GetIndex());

		const FPropertyDefinition& Definition = BuiltInComponents->PropertyRegistry.GetDefinition(PropertyID);
		if (Definition.PreAnimatedValue)
		{
			Definition.Handler->DispatchRestorePreAnimatedStateTasks(Definition, InPrerequisites, Subsequents, Linker);
		}
	}

	RestorePreAnimatedStateTasks.Empty();
}

void UMovieScenePropertyInstantiatorSystem::DiscardPreAnimatedStateForObject(UObject& Object)
{
	using namespace UE::MovieScene;

	for (FObjectPropertyInfo& PropertyInfo : ResolvedProperties)
	{
		if (PropertyInfo.BoundObject == &Object && PropertyInfo.bWantsRestoreState)
		{
			Linker->EntityManager.RemoveComponent(PropertyInfo.PropertyEntityID, BuiltInComponents->Tags.RestoreState);
			PropertyInfo.bWantsRestoreState = false;
		}
	}
}


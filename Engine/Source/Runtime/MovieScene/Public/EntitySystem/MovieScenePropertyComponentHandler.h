// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/MovieScenePartialProperties.inl"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieScenePreAnimatedPropertyHelper.h"
#include "EntitySystem/MovieSceneInitialValueCache.h"
#include "EntitySystem/MovieScenePropertySystemTypes.inl"
#include "EntitySystem/MovieSceneOperationalTypeConversions.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationExtension.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"



namespace UE
{
namespace MovieScene
{

DECLARE_CYCLE_STAT(TEXT("Apply properties"), MovieSceneEval_ApplyProperties,  STATGROUP_MovieSceneECS);



template<typename PropertyType, typename OperationalType, typename ...CompositeTypes>
struct TPropertyComponentHandlerImpl;

template<typename PropertyType, typename OperationalType, typename ...CompositeTypes>
struct TPropertyComponentHandler
	: TPropertyComponentHandlerImpl<TMakeIntegerSequence<int32, sizeof...(CompositeTypes)>, PropertyType, OperationalType, CompositeTypes...>
{
};

template<typename OperationalType, typename MemberType>
struct TPatchComposite
{
	int32 MemberOffset;

	void operator()(OperationalType& Out, MemberType InComponent) const
	{
		*reinterpret_cast<MemberType*>(reinterpret_cast<uint8*>(&Out) + MemberOffset) = InComponent;
	}
};

template<typename PropertyType, typename OperationalType, typename ...CompositeTypes, int ...Indices>
struct TPropertyComponentHandlerImpl<TIntegerSequence<int, Indices...>, PropertyType, OperationalType, CompositeTypes...> : IPropertyComponentHandler
{
	using CustomAccessorType = TCustomPropertyAccessorFunctions<PropertyType>;
	using CompleteSetterTask = TSetCompositePropertyValues<PropertyType, CompositeTypes...>;

	using ProjectionType = TPartialProjections<OperationalType, TPartialProjection<CompositeTypes, TPatchComposite<OperationalType, CompositeTypes>>...   >;
	using PartialSetterTask = TSetPartialPropertyValues< PropertyType, ProjectionType >;

	template<typename T, int Index>
	static void InitProjection(ProjectionType& OutProjection, const FPropertyCompositeDefinition& Composite)
	{
		OutProjection.Composites.template Get<Index>().ComponentTypeID = Composite.ComponentTypeID.ReinterpretCast<T>();
		OutProjection.Composites.template Get<Index>().Projection.MemberOffset = Composite.CompositeOffset;
	}

	static void ConvertOperationalPropertyToFinal(const OperationalType& In, PropertyType& Out)
	{
		using namespace UE::MovieScene;
		ConvertOperationalProperty(In, Out);
	}

	static void ConvertFinalPropertyToOperational(const PropertyType& In, OperationalType& Out)
	{
		using namespace UE::MovieScene;
		ConvertOperationalProperty(Out, In);
	}

	static PropertyType ConvertCompositesToFinal(CompositeTypes... InComposites)
	{
		OperationalType Temp = { InComposites... };
		PropertyType Final;
		ConvertOperationalPropertyToFinal(Temp, Final);
		return Final;
	}

	virtual void DispatchSetterTasks(const FPropertyDefinition& Definition, TArrayView<const FPropertyCompositeDefinition> Composites, const FPropertyStats& Stats, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker)
	{
		ProjectionType Projection;

		int Temp[] = { (this->InitProjection<CompositeTypes, Indices>(Projection, Composites[Indices]), 0)... };
		(void)Temp;

		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
		.ReadAllOf(Composites[Indices].ComponentTypeID.ReinterpretCast<CompositeTypes>()...)
		.FilterAll({ Definition.PropertyType })
		.SetStat(GET_STATID(MovieSceneEval_ApplyProperties))
		.SetDesiredThread(Linker->EntityManager.GetGatherThread())
		.template Dispatch_PerAllocation<CompleteSetterTask>(&Linker->EntityManager, InPrerequisites, &Subsequents, Definition.CustomPropertyRegistration, &ConvertCompositesToFinal);

		if (Stats.NumPartialProperties > 0)
		{
			FComponentMask CompletePropertyMask;
			for (const FPropertyCompositeDefinition& Composite : Composites)
			{
				CompletePropertyMask.Set(Composite.ComponentTypeID);
			}

			FEntityTaskBuilder()
			.Read(BuiltInComponents->BoundObject)
			.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
			.FilterAny({ CompletePropertyMask })
			.FilterAll({ Definition.PropertyType })
			.FilterOut(CompletePropertyMask)
			.SetStat(GET_STATID(MovieSceneEval_ApplyProperties))
			.SetDesiredThread(Linker->EntityManager.GetGatherThread())
			.template Dispatch_PerAllocation<PartialSetterTask>(&Linker->EntityManager, InPrerequisites, &Subsequents, Definition.CustomPropertyRegistration, Projection);
		}
	}

	virtual void DispatchCachePreAnimatedTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) override
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		TGetPropertyValues<PropertyType> GetProperties(Definition.CustomPropertyRegistration);

		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
		.Write(Definition.PreAnimatedValue.ReinterpretCast<PropertyType>())
		.FilterAll({ BuiltInComponents->Tags.CachePreAnimatedValue, Definition.PropertyType })
		.SetDesiredThread(Linker->EntityManager.GetGatherThread())
		.RunInline_PerAllocation(&Linker->EntityManager, GetProperties);
	}

	virtual void DispatchRestorePreAnimatedStateTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) override
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		TSetPropertyValues<PropertyType> SetProperties(Definition.CustomPropertyRegistration);

		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
		.Read(Definition.PreAnimatedValue.ReinterpretCast<PropertyType>())
		.FilterAll({ Definition.PropertyType, BuiltInComponents->Tags.Finished })
		.SetDesiredThread(Linker->EntityManager.GetGatherThread())
		.RunInline_PerAllocation(&Linker->EntityManager, SetProperties);
	}


	struct FInitialValueProcessor : IInitialValueProcessor
	{
		TSortedMap<FInterrogationChannel, OperationalType> ValuesByChannel;

		FBuiltInComponentTypes* BuiltInComponents;
		IInterrogationExtension* Interrogation;
		const FPropertyDefinition* PropertyDefinition;
		FCustomAccessorView CustomAccessors;

		FEntityAllocationWriteContext WriteContext;
		TPropertyValueStorage<PropertyType>* CacheStorage;

		FInitialValueProcessor()
			: WriteContext(FEntityAllocationWriteContext::NewAllocation())
		{
			BuiltInComponents = FBuiltInComponentTypes::Get();

			Interrogation = nullptr;
			CacheStorage = nullptr;
		}

		virtual void Initialize(UMovieSceneEntitySystemLinker* Linker, const FPropertyDefinition* Definition, FInitialValueCache* InitialValueCache) override
		{
			PropertyDefinition = Definition;
			Interrogation = Linker->FindExtension<IInterrogationExtension>();
			WriteContext  = FEntityAllocationWriteContext(Linker->EntityManager);

			CustomAccessors = PropertyDefinition->CustomPropertyRegistration->GetAccessors();

			if (InitialValueCache)
			{
				CacheStorage = InitialValueCache->GetStorage<PropertyType>(Definition->InitialValueType);
			}
		}

		virtual void Process(const FEntityAllocation* Allocation, const FComponentMask& AllocationType) override
		{
			if (Interrogation && AllocationType.Contains(BuiltInComponents->Interrogation.OutputKey))
			{
				VisitInterrogationAllocation(Allocation);
			}
			else if (CacheStorage)
			{
				VisitAllocationCached(Allocation);
			}
			else
			{
				VisitAllocation(Allocation);
			}
		}

		virtual void Finalize() override
		{
			ValuesByChannel.Empty();
			Interrogation = nullptr;
			CacheStorage = nullptr;
			CustomAccessors = FCustomAccessorView();
		}

		void VisitAllocation(const FEntityAllocation* Allocation)
		{
			const int32 Num = Allocation->Num();

			TComponentWriter<OperationalType> InitialValues = Allocation->WriteComponents(PropertyDefinition->InitialValueType.ReinterpretCast<OperationalType>(), WriteContext);
			TComponentReader<UObject*>        BoundObjects  = Allocation->ReadComponents(BuiltInComponents->BoundObject);

			if (TOptionalComponentReader<FCustomPropertyIndex> CustomIndices = Allocation->TryReadComponents(BuiltInComponents->CustomPropertyIndex))
			{
				const FCustomPropertyIndex* RawIndices = CustomIndices.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					const TCustomPropertyAccessor<PropertyType>& CustomAccessor = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[RawIndices[Index].Value]);

					PropertyType CurrentValue = CustomAccessor.Functions.Getter(BoundObjects[Index]);
					ConvertOperationalProperty(CurrentValue, InitialValues[Index]);
				}
			}

			else if (TOptionalComponentReader<uint16> FastOffsets = Allocation->TryReadComponents(BuiltInComponents->FastPropertyOffset))
			{
				const uint16* RawOffsets = FastOffsets.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					PropertyType CurrentValue = *reinterpret_cast<const PropertyType*>(reinterpret_cast<const uint8*>(static_cast<const void*>(BoundObjects[Index])) + RawOffsets[Index]);
					ConvertOperationalProperty(CurrentValue, InitialValues[Index]);
				}
			}

			else if (TOptionalComponentReader<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperties = Allocation->TryReadComponents(BuiltInComponents->SlowProperty))
			{
				const TSharedPtr<FTrackInstancePropertyBindings>* RawProperties = SlowProperties.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					PropertyType CurrentValue = RawProperties[Index]->GetCurrentValue<PropertyType>(*BoundObjects[Index]);
					ConvertOperationalProperty(CurrentValue, InitialValues[Index]);
				}
			}
		}

		void VisitAllocationCached(const FEntityAllocation* Allocation)
		{
			const int32 Num = Allocation->Num();

			TComponentWriter<FInitialValueIndex> InitialValueIndices = Allocation->WriteComponents(BuiltInComponents->InitialValueIndex, WriteContext);
			TComponentWriter<OperationalType>    InitialValues       = Allocation->WriteComponents(PropertyDefinition->InitialValueType.ReinterpretCast<OperationalType>(), WriteContext);
			TComponentReader<UObject*>           BoundObjects        = Allocation->ReadComponents(BuiltInComponents->BoundObject);

			if (TOptionalComponentReader<FCustomPropertyIndex> CustomIndices = Allocation->TryReadComponents(BuiltInComponents->CustomPropertyIndex))
			{
				const FCustomPropertyIndex* RawIndices = CustomIndices.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					TPair<FInitialValueIndex, PropertyType> CachedValue = CacheStorage->CacheInitialValue(BoundObjects[Index], CustomAccessors, RawIndices[Index]);

					ConvertOperationalProperty(CachedValue.Value, InitialValues[Index]);
					InitialValueIndices[Index] = CachedValue.Key;
				}
			}

			else if (TOptionalComponentReader<uint16> FastOffsets = Allocation->TryReadComponents(BuiltInComponents->FastPropertyOffset))
			{
				const uint16* RawOffsets = FastOffsets.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					TPair<FInitialValueIndex, PropertyType> CachedValue = CacheStorage->CacheInitialValue(BoundObjects[Index], RawOffsets[Index]);

					ConvertOperationalProperty(CachedValue.Value, InitialValues[Index]);
					InitialValueIndices[Index] = CachedValue.Key;
				}
			}

			else if (TOptionalComponentReader<TSharedPtr<FTrackInstancePropertyBindings>> SlowProperties = Allocation->TryReadComponents(BuiltInComponents->SlowProperty))
			{
				const TSharedPtr<FTrackInstancePropertyBindings>* RawProperties = SlowProperties.AsPtr();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					TPair<FInitialValueIndex, PropertyType> CachedValue = CacheStorage->CacheInitialValue(BoundObjects[Index], RawProperties[Index].Get());

					ConvertOperationalProperty(CachedValue.Value, InitialValues[Index]);
					InitialValueIndices[Index] = CachedValue.Key;
				}
			}
		}

		void VisitInterrogationAllocation(const FEntityAllocation* Allocation)
		{
			const int32 Num = Allocation->Num();

			TComponentWriter<OperationalType>   InitialValues = Allocation->WriteComponents(PropertyDefinition->InitialValueType.ReinterpretCast<OperationalType>(), WriteContext);
			TComponentReader<FInterrogationKey> OutputKeys    = Allocation->ReadComponents(BuiltInComponents->Interrogation.OutputKey);

			const FSparseInterrogationChannelInfo& SparseChannelInfo = Interrogation->GetSparseChannelInfo();

			for (int32 Index = 0; Index < Num; ++Index)
			{
				FInterrogationChannel Channel = OutputKeys[Index].Channel;

				// Did we already cache this value?
				if (const OperationalType* CachedValue = ValuesByChannel.Find(Channel))
				{
					InitialValues[Index] = *CachedValue;
					continue;
				}

				const FInterrogationChannelInfo* ChannelInfo = SparseChannelInfo.Find(Channel);
				UObject* Object = ChannelInfo ? ChannelInfo->WeakObject.Get() : nullptr;
				if (!ChannelInfo || !Object || ChannelInfo->PropertyBinding.PropertyName.IsNone())
				{
					continue;
				}

				TOptional< FResolvedFastProperty > Property = FPropertyRegistry::ResolveFastProperty(Object, ChannelInfo->PropertyBinding, CustomAccessors);

				// Retrieve a cached value if possible
				if (CacheStorage)
				{
					const PropertyType* CachedValue = nullptr;
					if (!Property.IsSet())
					{
						CachedValue = CacheStorage->FindCachedValue(Object, ChannelInfo->PropertyBinding.PropertyPath);
					}
					else if (const FCustomPropertyIndex* CustomIndex = Property->TryGet<FCustomPropertyIndex>())
					{
						CachedValue = CacheStorage->FindCachedValue(Object, *CustomIndex);
					}
					else
					{
						CachedValue = CacheStorage->FindCachedValue(Object, Property->Get<uint16>());
					}
					if (CachedValue)
					{
						OperationalType ConvertedProperty;
						ConvertOperationalProperty(*CachedValue, ConvertedProperty);

						InitialValues[Index] = ConvertedProperty;
						ValuesByChannel.Add(Channel, ConvertedProperty);
						continue;
					}
				}

				// No cached value available, must retrieve it now
				TOptional<PropertyType> CurrentValue;

				if (!Property.IsSet())
				{
					CurrentValue = FTrackInstancePropertyBindings::StaticValue<PropertyType>(Object, ChannelInfo->PropertyBinding.PropertyPath.ToString());
				}
				else if (const FCustomPropertyIndex* Custom = Property->TryGet<FCustomPropertyIndex>())
				{
					CurrentValue = static_cast<const TCustomPropertyAccessor<PropertyType>&>(CustomAccessors[Custom->Value]).Functions.Getter(Object);
				}
				else
				{
					const uint16 FastPtrOffset = Property->Get<uint16>();
					CurrentValue = *reinterpret_cast<const PropertyType*>(reinterpret_cast<const uint8*>(static_cast<const void*>(Object)) + FastPtrOffset);
				}

				OperationalType NewValue;
				ConvertOperationalProperty(CurrentValue.GetValue(), NewValue);

				InitialValues[Index] = NewValue;
				ValuesByChannel.Add(Channel, NewValue);
			};
		}
	};

	virtual IInitialValueProcessor* GetInitialValueProcessor() override
	{
		static FInitialValueProcessor Processor;
		return &Processor;
	}

	virtual void SaveGlobalPreAnimatedState(const FPropertyDefinition& Definition, UMovieSceneEntitySystemLinker* Linker) override
	{
		TPreAnimatedPropertyHelper<PropertyType> Helper(Definition, Linker);
		Helper.SavePreAnimatedState();
	}

	virtual void RecomposeBlendFinal(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const FFloatDecompositionParams& InParams, UMovieSceneBlenderSystem* Blender, FConstPropertyComponentView InCurrentValue, FPropertyComponentArrayView OutResult) override
	{
		check(OutResult.Num() == InParams.Query.Entities.Num());
		check(OutResult.Sizeof() == sizeof(PropertyType));

		OperationalType CurrentOperationalValue;
		ConvertOperationalProperty(InCurrentValue.ReinterpretCast<PropertyType>(), CurrentOperationalValue);

		TRecompositionResult<OperationalType> OperationalResults(CurrentOperationalValue, OutResult.Num());
		RecomposeBlendImpl(PropertyDefinition, Composites, InParams, Blender, CurrentOperationalValue, OperationalResults.Values);

		for (int32 Index = 0; Index < OperationalResults.Values.Num(); ++Index)
		{
			ConvertOperationalProperty(OperationalResults.Values[Index], OutResult[Index].ReinterpretCast<PropertyType>());
		}
	}

	virtual void RecomposeBlendOperational(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const FFloatDecompositionParams& InParams, UMovieSceneBlenderSystem* Blender, FConstPropertyComponentView InCurrentValue, FPropertyComponentArrayView OutResult) override
	{
		RecomposeBlendImpl(PropertyDefinition, Composites, InParams, Blender, InCurrentValue.ReinterpretCast<OperationalType>(), OutResult.ReinterpretCast<OperationalType>());
	}

	void RecomposeBlendImpl(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const FFloatDecompositionParams& InParams, UMovieSceneBlenderSystem* Blender, const OperationalType& InCurrentValue, TArrayView<OperationalType> OutResults)
	{
		check(OutResults.Num() == InParams.Query.Entities.Num());

		IMovieSceneFloatDecomposer* FloatDecomposer = Cast<IMovieSceneFloatDecomposer>(Blender);
		if (!FloatDecomposer)
		{
			return;
		}

		constexpr int32 NumComposites = sizeof...(CompositeTypes);
		check(Composites.Num() == NumComposites);

		FAlignedDecomposedFloat AlignedOutputs[NumComposites];

		FFloatDecompositionParams LocalParams = InParams;

		FGraphEventArray Tasks;
		for (int32 Index = 0; Index < NumComposites; ++Index)
		{
			if ((PropertyDefinition.FloatCompositeMask & (1 << Index)) == 0)
			{
				continue;
			}

			LocalParams.ResultComponentType = Composites[Index].ComponentTypeID.ReinterpretCast<float>();
			FGraphEventRef Task = FloatDecomposer->DispatchDecomposeTask(LocalParams, &AlignedOutputs[Index]);
			if (Task)
			{
				Tasks.Add(Task);
			}
		}

		if (Tasks.Num() != 0)
		{
			FTaskGraphInterface::Get().WaitUntilTasksComplete(Tasks, ENamedThreads::GameThread);
		}

		// Get the initial value in case we have a value without a full-weighted absolute channel.
		TOptionalComponentReader<OperationalType> InitialValueComponent;
		if (InParams.PropertyEntityID)
		{
			const FEntityManager& EntityManager = Blender->GetLinker()->EntityManager;
			TComponentTypeID<OperationalType> InitialValueType = PropertyDefinition.InitialValueType.ReinterpretCast<OperationalType>();
			InitialValueComponent = EntityManager.ReadComponent(InParams.PropertyEntityID, InitialValueType);
		}

		for (int32 Index = 0; Index < LocalParams.Query.Entities.Num(); ++Index)
		{
			FMovieSceneEntityID EntityID = LocalParams.Query.Entities[Index];

			uint8* Result = reinterpret_cast<uint8*>(&OutResults[Index]);

			for (int32 CompositeIndex = 0; CompositeIndex < NumComposites; ++CompositeIndex)
			{
				if ((PropertyDefinition.FloatCompositeMask & (1 << CompositeIndex)) == 0)
				{
					continue;
				}

				const float* InitialValueComposite = nullptr;
				FAlignedDecomposedFloat& AlignedOutput = AlignedOutputs[CompositeIndex];
				if (InitialValueComponent)
				{
					const OperationalType& InitialValue = (*InitialValueComponent);
					InitialValueComposite = reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(&InitialValue) + Composites[CompositeIndex].CompositeOffset);
				}

				const float NewComposite = *reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(&InCurrentValue) + Composites[CompositeIndex].CompositeOffset);

				float* RecomposedComposite = reinterpret_cast<float*>(Result + Composites[CompositeIndex].CompositeOffset);
				*RecomposedComposite = AlignedOutput.Value.Recompose(EntityID, NewComposite, InitialValueComposite);
			}
		}
	}

	virtual void RecomposeBlendChannel(const FPropertyDefinition& PropertyDefinition, const FPropertyCompositeDefinition& Composite, const FFloatDecompositionParams& InParams, UMovieSceneBlenderSystem* Blender, float InCurrentValue, TArrayView<float> OutResults) override
	{
		check(OutResults.Num() == InParams.Query.Entities.Num());

		IMovieSceneFloatDecomposer* FloatDecomposer = Cast<IMovieSceneFloatDecomposer>(Blender);
		if (!FloatDecomposer)
		{
			return;
		}

		FAlignedDecomposedFloat AlignedOutput;

		FFloatDecompositionParams LocalParams = InParams;

		LocalParams.ResultComponentType = Composite.ComponentTypeID.ReinterpretCast<float>();
		FGraphEventRef Task = FloatDecomposer->DispatchDecomposeTask(LocalParams, &AlignedOutput);
		if (Task)
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task, ENamedThreads::GameThread);
		}

		// Get the initial value in case we have a value without a full-weighted absolute channel.
		TOptionalComponentReader<OperationalType> InitialValueComponent;
		if (InParams.PropertyEntityID)
		{
			const FEntityManager& EntityManager = Blender->GetLinker()->EntityManager;
			TComponentTypeID<OperationalType> InitialValueType = PropertyDefinition.InitialValueType.ReinterpretCast<OperationalType>();
			InitialValueComponent = EntityManager.ReadComponent(InParams.PropertyEntityID, InitialValueType);
		}

		for (int32 Index = 0; Index < LocalParams.Query.Entities.Num(); ++Index)
		{
			FMovieSceneEntityID EntityID = LocalParams.Query.Entities[Index];

			uint8* Result = reinterpret_cast<uint8*>(&OutResults[Index]);

			const float* InitialValueComposite = nullptr;
			if (InitialValueComponent)
			{
				const OperationalType& InitialValue = (*InitialValueComponent);
				InitialValueComposite = reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(&InitialValue) + Composite.CompositeOffset);
			}

			const float NewComposite = *reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(&InCurrentValue) + Composite.CompositeOffset);

			float* RecomposedComposite = reinterpret_cast<float*>(Result + Composite.CompositeOffset);
			*RecomposedComposite = AlignedOutput.Value.Recompose(EntityID, NewComposite, InitialValueComposite);
		}
	}

	virtual void RebuildOperational(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const TArrayView<FMovieSceneEntityID>& EntityIDs, UMovieSceneEntitySystemLinker* Linker, FPropertyComponentArrayView OutResult) override
	{
		RebuildOperationalImpl(PropertyDefinition, Composites, EntityIDs, Linker, OutResult.ReinterpretCast<OperationalType>());
	}

	virtual void RebuildFinal(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const TArrayView<FMovieSceneEntityID>& EntityIDs, UMovieSceneEntitySystemLinker* Linker, FPropertyComponentArrayView OutResult) override
	{
		TArray<OperationalType> OperationalValues;
		OperationalValues.SetNum(OutResult.Num());
		RebuildOperationalImpl(PropertyDefinition, Composites, EntityIDs, Linker, OperationalValues);

		TArrayView<PropertyType> OutResultView = OutResult.ReinterpretCast<PropertyType>();
		for (int32 Index = 0; Index < OperationalValues.Num(); ++Index)
		{
			PropertyType FinalValue;
			ConvertOperationalPropertyToFinal(OperationalValues[Index], FinalValue);
			OutResultView[Index] = FinalValue;
		}
	}

	template<typename T>
	T ReadComponentValueOrDefault(const FEntityManager& EntityManager, FMovieSceneEntityID EntityID, TComponentTypeID<T> ComponentTypeID)
	{
		TComponentPtr<const T> ComponentPtr = EntityManager.ReadComponent(EntityID, ComponentTypeID);
		if (ComponentPtr)
		{
			return *ComponentPtr;
		}
		return T();
	}

	void RebuildOperationalImpl(const FPropertyDefinition& PropertyDefinition, TArrayView<const FPropertyCompositeDefinition> Composites, const TArrayView<FMovieSceneEntityID>& EntityIDs, UMovieSceneEntitySystemLinker* Linker, TArrayView<OperationalType> OutResults) 
	{
		constexpr int32 NumComposites = sizeof...(CompositeTypes);
		check(Composites.Num() == NumComposites);

		check(OutResults.Num() == EntityIDs.Num());

		FEntityManager& EntityManager = Linker->EntityManager;
		
		for (int32 Index = 0; Index < EntityIDs.Num(); ++Index)
		{
			FMovieSceneEntityID EntityID(EntityIDs[Index]);

			OperationalType OperationalValue = {
				ReadComponentValueOrDefault(
						EntityManager, EntityID, Composites[Indices].ComponentTypeID.ReinterpretCast<CompositeTypes>())...
			};
			OutResults[Index] = OperationalValue;
		}
	}
};




template<typename PropertyType, typename OperationalType>
struct TPropertyDefinitionBuilder
{
	TPropertyDefinitionBuilder<PropertyType, OperationalType>& AddSoleChannel(TComponentTypeID<OperationalType> InComponent)
	{
		checkf(Definition == &Registry->GetProperties().Last(), TEXT("Cannot re-define a property type after another has been added."));
		checkf(Definition->CompositeSize == 0, TEXT("Property already has a composite."));

		FPropertyCompositeDefinition NewChannel = { InComponent, 0 };
		Registry->CompositeDefinitions.Add(NewChannel);

		Definition->CompositeSize = 1;
		if (TIsSame<OperationalType, float>::Value)
		{
			Definition->FloatCompositeMask = 1;
		}

		return *this;
	}

	template<int InlineSize>
	TPropertyDefinitionBuilder<PropertyType, OperationalType>& SetCustomAccessors(TCustomPropertyRegistration<PropertyType, InlineSize>* InCustomAccessors)
	{
		Definition->CustomPropertyRegistration = InCustomAccessors;
		return *this;
	}

	void Commit()
	{
		Definition->Handler = TPropertyComponentHandler<PropertyType, OperationalType, OperationalType>();
	}

	template<typename HandlerType>
	void Commit(HandlerType&& InHandler)
	{
		Definition->Handler = Forward<HandlerType>(InHandler);
	}

protected:

	friend FPropertyRegistry;

	TPropertyDefinitionBuilder(FPropertyDefinition* InDefinition, FPropertyRegistry* InRegistry)
		: Definition(InDefinition), Registry(InRegistry)
	{}

	FPropertyDefinition* Definition;
	FPropertyRegistry* Registry;
};


template<typename PropertyType, typename OperationalType, typename... Composites>
struct TCompositePropertyDefinitionBuilder
{
	static_assert(sizeof...(Composites) <= 32, "More than 32 composites is not supported");

	TCompositePropertyDefinitionBuilder(FPropertyDefinition* InDefinition, FPropertyRegistry* InRegistry)
		: Definition(InDefinition), Registry(InRegistry)
	{}

	template<typename T, T OperationalType::*DataPtr>
	TCompositePropertyDefinitionBuilder<PropertyType, OperationalType, Composites..., T> AddComposite(TComponentTypeID<T> InComponent)
	{
		checkf(Definition == &Registry->GetProperties().Last(), TEXT("Cannot re-define a property type after another has been added."));

		const PTRINT CompositeOffset = (PTRINT)&(((OperationalType*)0)->*DataPtr);

		FPropertyCompositeDefinition NewChannel = { InComponent, static_cast<uint16>(CompositeOffset) };
		Registry->CompositeDefinitions.Add(NewChannel);

		return TCompositePropertyDefinitionBuilder<PropertyType, OperationalType, Composites..., T>(Definition, Registry);
	}

	template<float OperationalType::*DataPtr>
	TCompositePropertyDefinitionBuilder<PropertyType, OperationalType, Composites..., float> AddComposite(TComponentTypeID<float> InComponent)
	{
		checkf(Definition == &Registry->GetProperties().Last(), TEXT("Cannot re-define a property type after another has been added."));

		const PTRINT CompositeOffset = (PTRINT)&(((OperationalType*)0)->*DataPtr);

		FPropertyCompositeDefinition NewChannel = { InComponent, static_cast<uint16>(CompositeOffset) };
		Registry->CompositeDefinitions.Add(NewChannel);

		Definition->FloatCompositeMask |= 1 << Definition->CompositeSize;

		++Definition->CompositeSize;
		return TCompositePropertyDefinitionBuilder<PropertyType, OperationalType, Composites..., float>(Definition, Registry);
	}

	template<int InlineSize>
	TCompositePropertyDefinitionBuilder<PropertyType, OperationalType, Composites...>& SetCustomAccessors(TCustomPropertyRegistration<PropertyType, InlineSize>* InCustomAccessors)
	{
		Definition->CustomPropertyRegistration = InCustomAccessors;
		return *this;
	}

	void Commit()
	{
		Definition->Handler = TPropertyComponentHandler<PropertyType, OperationalType, Composites...>();
	}

	template<typename HandlerType>
	void Commit(HandlerType&& InHandler)
	{
		Definition->Handler = Forward<HandlerType>(InHandler);
	}

private:

	FPropertyDefinition* Definition;
	FPropertyRegistry* Registry;
};


struct FPropertyRecomposerPropertyInfo
{
	static constexpr uint16 INVALID_BLEND_CHANNEL = uint16(-1);

	uint16 BlendChannel = INVALID_BLEND_CHANNEL;
	UMovieSceneBlenderSystem* BlenderSystem = nullptr;
	FMovieSceneEntityID PropertyEntityID;

	static FPropertyRecomposerPropertyInfo Invalid()
	{ 
		return FPropertyRecomposerPropertyInfo { INVALID_BLEND_CHANNEL, nullptr, FMovieSceneEntityID::Invalid() };
	}
};

DECLARE_DELEGATE_RetVal_TwoParams(FPropertyRecomposerPropertyInfo, FOnGetPropertyRecomposerPropertyInfo, FMovieSceneEntityID, UObject*);

struct FPropertyRecomposerImpl
{
	template<typename PropertyType, typename OperationalType>
	TRecompositionResult<PropertyType> RecomposeBlendFinal(const TPropertyComponents<PropertyType, OperationalType>& InComponents, const FDecompositionQuery& InQuery, const PropertyType& InCurrentValue);

	template<typename PropertyType, typename OperationalType>
	TRecompositionResult<OperationalType> RecomposeBlendOperational(const TPropertyComponents<PropertyType, OperationalType>& InComponents, const FDecompositionQuery& InQuery, const OperationalType& InCurrentValue);

	FOnGetPropertyRecomposerPropertyInfo OnGetPropertyInfo;
};

template<typename PropertyType, typename OperationalType>
TRecompositionResult<PropertyType> FPropertyRecomposerImpl::RecomposeBlendFinal(const TPropertyComponents<PropertyType, OperationalType>& Components, const FDecompositionQuery& InQuery, const PropertyType& InCurrentValue)
{
	using namespace UE::MovieScene;

	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	const FPropertyDefinition& PropertyDefinition = BuiltInComponents->PropertyRegistry.GetDefinition(Components.CompositeID);

	TRecompositionResult<PropertyType> Result(InCurrentValue, InQuery.Entities.Num());

	if (InQuery.Entities.Num() == 0)
	{
		return Result;
	}

	const FPropertyRecomposerPropertyInfo Property = OnGetPropertyInfo.Execute(InQuery.Entities[0], InQuery.Object);

	if (Property.BlendChannel == FPropertyRecomposerPropertyInfo::INVALID_BLEND_CHANNEL)
	{
		return Result;
	}

	UMovieSceneBlenderSystem* Blender = Property.BlenderSystem;
	if (!Blender)
	{
		return Result;
	}

	FFloatDecompositionParams Params;
	Params.Query = InQuery;
	Params.PropertyEntityID = Property.PropertyEntityID;
	Params.DecomposeBlendChannel = Property.BlendChannel;
	Params.PropertyTag = PropertyDefinition.PropertyType;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(PropertyDefinition);

	PropertyDefinition.Handler->RecomposeBlendFinal(PropertyDefinition, Composites, Params, Blender, InCurrentValue, Result.Values);

	return Result;
}

template<typename PropertyType, typename OperationalType>
TRecompositionResult<OperationalType> FPropertyRecomposerImpl::RecomposeBlendOperational(const TPropertyComponents<PropertyType, OperationalType>& Components, const FDecompositionQuery& InQuery, const OperationalType& InCurrentValue)
{
	using namespace UE::MovieScene;

	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();
	const FPropertyDefinition& PropertyDefinition = BuiltInComponents->PropertyRegistry.GetDefinition(Components.CompositeID);

	TRecompositionResult<OperationalType> Result(InCurrentValue, InQuery.Entities.Num());

	if (InQuery.Entities.Num() == 0)
	{
		return Result;
	}

	const FPropertyRecomposerPropertyInfo Property = OnGetPropertyInfo.Execute(InQuery.Entities[0], InQuery.Object);

	if (Property.BlendChannel == FPropertyRecomposerPropertyInfo::INVALID_BLEND_CHANNEL)
	{
		return Result;
	}

	UMovieSceneBlenderSystem* Blender = Property.BlenderSystem;
	if (!Blender)
	{
		return Result;
	}

	FFloatDecompositionParams Params;
	Params.Query = InQuery;
	Params.PropertyEntityID = Property.PropertyEntityID;
	Params.DecomposeBlendChannel = Property.BlendChannel;
	Params.PropertyTag = PropertyDefinition.PropertyType;

	TArrayView<const FPropertyCompositeDefinition> Composites = BuiltInComponents->PropertyRegistry.GetComposites(PropertyDefinition);

	PropertyDefinition.Handler->RecomposeBlendOperational(PropertyDefinition, Composites, Params, Blender, InCurrentValue, Result.Values);

	return Result;
}


} // namespace MovieScene
} // namespace UE



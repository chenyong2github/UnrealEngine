// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertyRegistry.h"
#include "EntitySystem/MovieScenePartialProperties.inl"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieScenePreAnimatedPropertyHelper.h"

#include "EntitySystem/MovieSceneOperationalTypeConversions.h"



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

	virtual void DispatchCacheInitialValueTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) override
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		TGetPropertyValues<PropertyType, OperationalType> GetProperties(Definition.CustomPropertyRegistration);

		FEntityTaskBuilder()
		.Read(BuiltInComponents->BoundObject)
		.ReadOneOf(BuiltInComponents->CustomPropertyIndex, BuiltInComponents->FastPropertyOffset, BuiltInComponents->SlowProperty)
		.Write(Definition.InitialValueType.ReinterpretCast<OperationalType>())
		.FilterAll({ BuiltInComponents->Tags.NeedsLink, Definition.PropertyType })
		.SetDesiredThread(Linker->EntityManager.GetGatherThread())
		.RunInline_PerAllocation(&Linker->EntityManager, GetProperties);
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
		TComponentPtr<const OperationalType> InitialValueComponent;
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
		TComponentPtr<const OperationalType> InitialValueComponent;
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



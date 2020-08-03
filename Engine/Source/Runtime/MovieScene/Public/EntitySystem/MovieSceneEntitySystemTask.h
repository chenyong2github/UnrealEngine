// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntityRange.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneSystemTaskDependencies.h"

#include "Templates/AndOrNot.h"

#include <initializer_list>

namespace UE
{
namespace MovieScene
{

DECLARE_CYCLE_STAT(TEXT("Aquire Component Access Locks"), MovieSceneEval_AquireComponentAccessLocks, STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Release Component Access Locks"), MovieSceneEval_ReleaseComponentAccessLocks, STATGROUP_MovieSceneECS);


template<typename...> struct TFilteredEntityTask;
template<typename...> struct TEntityTaskComponents;
template<typename...> struct TEntityTaskComponentsImpl;

template<typename, typename...> struct TEntityTask;
template<typename, typename...> struct TEntityTaskBase;
template<typename, typename...> struct TEntityAllocationTask;
template<typename, typename...> struct TEntityAllocationTaskBase;


template<typename TaskImpl, int32 NumComponents, bool AutoExpandAccessors>
struct TEntityTaskCaller;

/** Default traits specialized for each user TaskImplInstance */
template<typename T>
struct TDefaultEntityTaskTraits
{
	enum
	{
		/**
		 * When true, the various component accessors are passed to the task callback as separate parameters. When false, they are passed through as a combined template
		 * 
		 * For example:
		 *
		 * struct FForEach_Expanded
		 * { 
		 *     void ForEachEntity(float, uint16, UObject*);
		 *     void ForEachAllocation(const FEntityAllocation*, TRead<float>, TRead<uint16>, TRead<UObject*>);
		 * };
		 * struct FForEach_NoExpansion
		 * {
		 *     void ForEachEntity(const TEntityPtr<const float, const uint16, const UObject*>&);
		 *     void ForEachAllocation(const FEntityAllocation*, const TEntityTaskComponents<TRead<float>, TRead<uint16>, TRead<UObject*>>&);
		 * };
		 * template<> struct TEntityTaskTraits<FForEach_NoExpansion> : TDefaultEntityTaskTraits<FForEach_NoExpansion> { enum { AutoExpandAccessors = false }; };
		 * 
		 * FEntityTaskBuilder().Read<float>().Read<uint16>().Read<UObject*>().Dispatch_PerEntity<FForEach_Expanded>(...);
		 * FEntityTaskBuilder().Read<float>().Read<uint16>().Read<UObject*>().Dispatch_PerEntity<FForEach_NoExpansion>(...);
		 * FEntityTaskBuilder().Read<float>().Read<uint16>().Read<UObject*>().Dispatch_PerAllocation<FForEach_Expanded>(...);
		 * FEntityTaskBuilder().Read<float>().Read<uint16>().Read<UObject*>().Dispatch_PerAllocation<FForEach_NoExpansion>(...);
		 */
		AutoExpandAccessors = true,
	};
};

/** Optionally specialized traits for user TaskImplInstances */
template<typename T>
struct TEntityTaskTraits : TDefaultEntityTaskTraits<T>
{
};

/** Utility that promotes callbacks that return void to always return 'true' when iterating entities*/
struct FEntityIterationResult
{
	template<typename T>
	friend FORCEINLINE FEntityIterationResult operator,(T, FEntityIterationResult)
	{
		return FEntityIterationResult { true };
	}

	friend FORCEINLINE FEntityIterationResult operator,(bool In, FEntityIterationResult)
	{
		return FEntityIterationResult{ In };
	}

	FORCEINLINE explicit operator bool() const
	{
		return Value;
	}

	bool Value = true;
};

/**
 * Defines the accessors for each desired component of an entity task
 */
template<typename... T>
struct TEntityTaskComponents : TEntityTaskComponentsImpl<TMakeIntegerSequence<int, sizeof...(T)>, T...>
{
	using Super = TEntityTaskComponentsImpl<TMakeIntegerSequence<int, sizeof...(T)>, T...>;


	/**
	 * Constrain this task to only run for entities that have all the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterAll(const FComponentMask& InComponentMask)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterAll(InComponentMask);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that have all the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterAll(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterAll(InComponentTypes);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that have none the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterNone(const FComponentMask& InComponentMask)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterNone(InComponentMask);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that have none the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterNone(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterNone(InComponentTypes);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that have at least one of the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterAny(const FComponentMask& InComponentMask)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterAny(InComponentMask);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that have at least one of the specified components or tags
	 */
	TFilteredEntityTask< T... > FilterAny(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterAny(InComponentTypes);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that do not have the specific combination of components or tags
	 */
	TFilteredEntityTask< T... > FilterOut(const FComponentMask& InComponentMask)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterOut(InComponentMask);
		return Filtered;
	}

	/**
	 * Constrain this task to only run for entities that do not have the specific combination of components or tags
	 */
	TFilteredEntityTask< T... > FilterOut(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		TFilteredEntityTask< T... > Filtered(*this);
		Filtered.FilterOut(InComponentTypes);
		return Filtered;
	}

	/**
	 * Combine this task's filter with the specified filter
	 */
	TFilteredEntityTask< T... > CombineFilter(const FEntityComponentFilter& InFilter)
	{
		return TFilteredEntityTask< T... >(*this, InFilter);
	}

	/**
	 * Assign the current thread for task dispatch to ensure that it is issued on the correct thread.
	 * This should only be required for tasks dispatched outside of the main linker execution, or tasks dispatched for the global entity manager
	 */
	TEntityTaskComponents< T... >& SetCurrentThread(ENamedThreads::Type InCurrentThread)
	{
		CurrentThread = InCurrentThread;
		return *this;
	}

	/**
	 * Assign a desired thread for this task to run on
	 */
	TEntityTaskComponents< T... >& SetDesiredThread(ENamedThreads::Type InDesiredThread)
	{
		DesiredThread = InDesiredThread;
		return *this;
	}

	/**
	 * Assign a stat ID for this task
	 */
	TEntityTaskComponents< T... >& SetStat(TStatId InStatId)
	{
		StatId = InStatId;
		return *this;
	}

	/**
	 * Dispatch a task for every allocation that matches the filters and component types. Must be explicitly instantiated with the task type to dispatch. Construction arguments are deduced.
	 * Tasks must implement a ForEachAllocation function that matches this task's component accessor types.
	 * 
	 * For example:
	 * struct FForEachAllocation
	 * {
	 *     void ForEachAllocation(FEntityAllocation* InAllocation, const TFilteredEntityTask< FReadEntityIDs, TRead<FMovieSceneFloatChannel> >& InputTask);
	 * };
	 *
	 * TComponentTypeID<FMovieSceneFloatChannel> FloatChannelComponent = ...;
	 *
	 * FGraphEventRef Task = FEntityTaskBuilder()
	 * .ReadEntityIDs()
	 * .Read(FloatChannelComponent)
	 * .SetStat(GET_STATID(MyStatName))
	 * .SetDesiredThread(ENamedThreads::AnyThread)
	 * .Dispatch_PerAllocation<FForEachAllocation>(EntityManager, Prerequisites);
	 *
	 * @param EntityManager    The entity manager to run the task on. All component types *must* relate to this entity manager.
	 * @param Prerequisites    Prerequisite tasks that must run before this one
	 * @param Subsequents      (Optional) Subsequent task tracking that this task should be added to for each writable component type
	 * @param InArgs           Optional arguments that are forwarded to the constructor of TaskImpl
	 * @return A pointer to the graph event for the task, or nullptr if this task is not valid (ie contains invalid component types that would be necessary for the task to run), or threading is disabled
	 */
	template<typename TaskImpl, typename... TaskConstructionArgs>
	FGraphEventRef Dispatch_PerAllocation(FEntityManager* EntityManager, const FSystemTaskPrerequisites& Prerequisites, FSystemSubsequentTasks* Subsequents, TaskConstructionArgs&&... InArgs) const
	{
		checkfSlow(IsInGameThread(), TEXT("Tasks can only be dispatched from the game thread."));

		if (!this->IsValid())
		{
			return nullptr;
		}

		if (EntityManager->GetThreadingModel() == EEntityThreadingModel::NoThreading)
		{
			TaskImpl Task{ Forward<TaskConstructionArgs>(InArgs)... };
			TEntityAllocationTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
			return nullptr;
		}
		else
		{

			FGraphEventArray GatheredPrereqs;
			this->PopulatePrerequisites(Prerequisites, &GatheredPrereqs);

			ENamedThreads::Type ThisThread = CurrentThread == ENamedThreads::AnyThread ? EntityManager->GetDispatchThread() : CurrentThread;
			checkSlow(ThisThread != ENamedThreads::AnyThread);

			FGraphEventRef NewTask = TGraphTask< TEntityAllocationTask<TaskImpl, T...> >::CreateTask(GatheredPrereqs.Num() != 0 ? &GatheredPrereqs : nullptr, ThisThread)
				.ConstructAndDispatchWhenReady( EntityManager, *this, DesiredThread, StatId, bBreakOnRun, Forward<TaskConstructionArgs>(InArgs)... );

			if (Subsequents)
			{
				this->PopulateSubsequents(NewTask, *Subsequents);
			}

			return NewTask;
		}
	}

	template<typename TaskImpl>
	void RunInline_PerAllocation(FEntityManager* EntityManager, TaskImpl& Task) const
	{
		if (this->IsValid())
		{
			TEntityAllocationTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
		}
	}

	/**
	 * Dispatch a task for every entity that matches the filters and component types. Must be explicitly instantiated with the task type to dispatch. Construction arguments are deduced.
	 * Tasks must implement a ForEachEntity function that matches this task's component accessor types.
	 * 
	 * For example:
	 * struct FForEachEntity
	 * {
	 *     void ForEachEntity(FMovieSceneEntityID InEntityID, const FMovieSceneFloatChannel& Channel);
	 * };
	 *
	 * TComponentTypeID<FMovieSceneFloatChannel> FloatChannelComponent = ...;
	 *
	 * FGraphEventRef Task = FEntityTaskBuilder()
	 * .ReadEntityIDs()
	 * .Read(FloatChannelComponent)
	 * .SetStat(GET_STATID(MyStatName))
	 * .SetDesiredThread(ENamedThreads::AnyThread)
	 * .Dispatch_PerEntity<FForEachEntity>(EntityManager, nullptr);
	 *
	 * @param EntityManager    The entity manager to run the task on. All component types *must* relate to this entity manager.
	 * @param Prerequisites    Prerequisite tasks that must run before this one, or nullptr if there are no prerequisites
	 * @param Subsequents      (Optional) Subsequent task tracking that this task should be added to for each writable component type
	 * @param InArgs           Optional arguments that are forwarded to the constructor of TaskImpl
	 * @return A pointer to the graph event for the task, or nullptr if this task is not valid (ie contains invalid component types that would be necessary for the task to run), or threading is disabled
	 */
	template<typename TaskImpl, typename... TaskConstructionArgs>
	FGraphEventRef Dispatch_PerEntity(FEntityManager* EntityManager, const FSystemTaskPrerequisites& Prerequisites, FSystemSubsequentTasks* Subsequents, TaskConstructionArgs&&... InArgs) const
	{
		checkfSlow(IsInGameThread(), TEXT("Tasks can only be dispatched from the game thread."));

		if (!this->IsValid())
		{
			return nullptr;
		}

		if (EntityManager->GetThreadingModel() == EEntityThreadingModel::NoThreading)
		{
			TaskImpl Task{ Forward<TaskConstructionArgs>(InArgs)... };
			TEntityTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
			return nullptr;
		}
		else
		{

			FGraphEventArray GatheredPrereqs;
			this->PopulatePrerequisites(Prerequisites, &GatheredPrereqs);

			ENamedThreads::Type ThisThread = CurrentThread == ENamedThreads::AnyThread ? EntityManager->GetDispatchThread() : CurrentThread;
			checkSlow(ThisThread != ENamedThreads::AnyThread);

			FGraphEventRef NewTask = TGraphTask< TEntityTask<TaskImpl, T...> >::CreateTask(GatheredPrereqs.Num() != 0 ? &GatheredPrereqs : nullptr, ThisThread)
				.ConstructAndDispatchWhenReady( EntityManager, *this, DesiredThread, StatId, bBreakOnRun, Forward<TaskConstructionArgs>(InArgs)... );

			if (Subsequents)
			{
				this->PopulateSubsequents(NewTask, *Subsequents);
			}

			return NewTask;
		}
	}

	template<typename TaskImpl>
	void RunInline_PerEntity(FEntityManager* EntityManager, TaskImpl& Task) const
	{
		if (this->IsValid())
		{
			TEntityTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
		}
	}

public:

	/** Useful for debugging to break the debugger when this task is run */
	bool bBreakOnRun;
	/** The current thread that is being used to dispatch from. Only necessary when EntityManager->GetDispatchThread is not available (ie when tasks are being run outside of system linkers) */
	ENamedThreads::Type CurrentThread;
	/** The thread that this task wants to run on */
	ENamedThreads::Type DesiredThread;
	/** A stat ID for the task */
	TStatId StatId;

	TEntityTaskComponents()
		: bBreakOnRun(false)
		, CurrentThread(ENamedThreads::AnyThread)
		, DesiredThread(ENamedThreads::AnyHiPriThreadHiPriTask)
	{
		static_assert(sizeof...(T) == 0, "Default construction is only supported for TEntityTaskComponents<>");
	}

	template<typename... ConstructionTypes>
	explicit TEntityTaskComponents(ConstructionTypes&&... InTypes)
		: Super(Forward<ConstructionTypes>(InTypes)...)
		, bBreakOnRun(false)
		, CurrentThread(ENamedThreads::AnyThread)
		, DesiredThread(ENamedThreads::AnyHiPriThreadHiPriTask)
	{}
};


template<int... Indices, typename... T>
struct TEntityTaskComponentsImpl<TIntegerSequence<int, Indices...>, T...>
{
	using EntityRangeType = TEntityRange<typename T::AccessType...>;

	/**
	 * Read the entity ID along with this task. Passed to the task as an FMovieSceneEntityID
	 */
	TEntityTaskComponents< T..., FReadEntityIDs > ReadEntityIDs() const
	{
		return TEntityTaskComponents< T..., FReadEntityIDs >( Accessors.template Get<Indices>()..., FReadEntityIDs{} );
	}

	/**
	 * Read the value of a component. Passed to the task as either a const T& or T depending on the type specified in the task function.
	 * @note Supplying an invalid ComponentType will be handled gracefully, but will result in no task being dispatched.
	 *
	 * @param ComponentType   A valid component type to read.
	 */
	template<typename U>
	TEntityTaskComponents<T..., TRead<U>> Read(TComponentTypeID<U> ComponentType) const
	{
		return TEntityTaskComponents<T..., TRead<U> >(Accessors.template Get<Indices>()..., TRead<U>(ComponentType));
	}

	/**
	 * Read the value of only one of the specified components. Only entities with exactly one of these components will be read. Per-entity iteration not supported with this accessor.
	 * @note Supplying an invalid ComponentType will be handled gracefully, but will result in no task being dispatched.
	 *
	 * @param ComponentTypes  The component types to visit
	 */
	template<typename... U>
	TEntityTaskComponents<T..., TReadOneOf<U...>> ReadOneOf(TComponentTypeID<U>... ComponentTypes) const
	{
		return TEntityTaskComponents<T..., TReadOneOf<U...> >( Accessors.template Get<Indices>()..., TReadOneOf<U...>(ComponentTypes...) );
	}

	/**
	 * Read the value of one or more of the specified components. Entities with at least one of these components will be read. Per-entity iteration not supported with this accessor.
	 *
	 * @param ComponentTypes  The component types to visit
	 */
	template<typename... U>
	TEntityTaskComponents<T..., TReadOneOrMoreOf<U...>> ReadOneOrMoreOf(TComponentTypeID<U>... ComponentTypes) const
	{
		return TEntityTaskComponents<T..., TReadOneOrMoreOf<U...> >(Accessors.template Get<Indices>()..., TReadOneOrMoreOf<U...>(ComponentTypes...));
	}

	/**
	 * Read all of the specified components and pass them through to the task as individual parameters
	 *
	 * @param ComponentTypes  The component types to visit
	 */
	template<typename... U>
	TEntityTaskComponents<T..., TRead<U>...> ReadAllOf(TComponentTypeID<U>... ComponentTypes) const
	{
		return TEntityTaskComponents<T..., TRead<U>... >( Accessors.template Get<Indices>()..., TRead<U>(ComponentTypes)... );
	}

	/**
	 * Projected Read of a component value. Type passed to the task will be the result of the invocation of the projection.
	 * @note Supplying an invalid ComponentType will be handled gracefully, but will result in no task being dispatched.
	 * @note For example:
	 * 
	 * struct FForEachEntity
	 * {
	 *     void ForEachEntity(float InX);
	 * };
	 *
	 * TComponentTypeID<FVector> VectorComponent = ...;
	 *
	 * FGraphEventRef Task = FEntityTaskBuilder()
	 * .ReadEntityIDs()
	 * .ReadProjected(VectorComponent, &FVector::X)
	 * .Dispatch_PerEntity<FForEachEntity>(...);
	 */
	template<typename U, typename ProjectionType>
	TEntityTaskComponents<T..., TReadProjected< U, typename TRemoveReference<ProjectionType>::Type >> ReadProjected(TComponentTypeID<U> ComponentType, ProjectionType&& Projection) const
	{
		using FReturnType = TReadProjected< U, typename TRemoveReference<ProjectionType>::Type >;
		return TEntityTaskComponents<T..., FReturnType >(Accessors.template Get<Indices>()..., FReturnType(ComponentType, Forward<ProjectionType>(Projection)));
	}

	/**
	 * Read the type-erased value of a component. Passed to the task as a const void*
	 * @note Supplying an invalid ComponentType will be handled gracefully, but will result in no task being dispatched.
	 *
	 * @param ComponentType   A valid component type to read.
	 */
	TEntityTaskComponents<T..., FReadErased> ReadErased(FComponentTypeID ComponentType) const
	{
		return TEntityTaskComponents<T..., FReadErased >(Accessors.template Get<Indices>()..., FReadErased(ComponentType));
	}

	/**
	 * Optionally read the value of a component. ComponentType may be invalid, and the component may or may not exist for some/all of the entities in the resulting task
	 * @note Always passed to the task as a const T* pointer which must be checked for null
	 */
	template<typename U>
	TEntityTaskComponents<T..., TReadOptional<U>> ReadOptional(TComponentTypeID<U> ComponentType) const
	{
		return TEntityTaskComponents<T..., TReadOptional<U> >(Accessors.template Get<Indices>()..., TReadOptional<U>(ComponentType));
	}

	/**
	 * Write the value of a component in a thread safe manner. Passed to the task as a T& so the value can be modified or overwritten.
	 * @note Supplying an invalid ComponentType will be handled gracefully, but will result in no task being dispatched.
	 *
	 * @param ComponentType   A valid component type to read.
	 */
	template<typename U>
	TEntityTaskComponents<T..., TWrite<U>> Write(TComponentTypeID<U> ComponentType) const
	{
		return TEntityTaskComponents<T..., TWrite<U> >(Accessors.template Get<Indices>()..., TWrite<U>(ComponentType));
	}


	/**
	 * Optionally write the value of a component in a thread safe manner if it exists. Passed to the task as a T* which must be checked for nullptr.
	 * @note Always passed to the task as a T* pointer which must be checked for null
	 */
	template<typename U>
	TEntityTaskComponents<T..., TWriteOptional<U>> WriteOptional(TComponentTypeID<U> ComponentType) const
	{
		return TEntityTaskComponents<T..., TWriteOptional<U> >(Accessors.template Get<Indices>()..., TWriteOptional<U>(ComponentType));
	}

	bool HasBeenWrittenToSince(uint32 InSystemVersion)
	{
		bool bAnyWrittenTo = true;

		int Temp[] = { ( bAnyWrittenTo |= HasBeenWrittenToSince(&Accessors.template Get<Indices>(), InSystemVersion), 0)... };
		(void)Temp;

		return bAnyWrittenTo;
	}

	/**
	 * Check whether this task data is well-formed in the sense that it can perform meaningful work.
	 */
	bool IsValid() const
	{
		bool bAllValid = true;

		int Temp[] = { ( bAllValid &= IsAccessorValid(&Accessors.template Get<Indices>()), 0)..., 0 };
		(void)Temp;

		return bAllValid;
	}

	/** Utility function called when the task is dispatched to populate the filter based on our component typs */
	void PopulateFilter(FEntityComponentFilter* OutFilter) const
	{
		int Temp[] = { (AddAccessorToFilter(&Accessors.template Get<Indices>(), OutFilter), 0)..., 0 };
		(void)Temp;
	}

	/** Utility function called when the task is dispatched to populate the filter based on our component typs */
	void PopulatePrerequisites(const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs) const
	{
		// Gather any master tasks
		InPrerequisites.FilterByComponent(*OutGatheredPrereqs, FComponentTypeID::Invalid());

		int Temp[] = { (UE::MovieScene::PopulatePrerequisites(&Accessors.template Get<Indices>(), InPrerequisites, OutGatheredPrereqs), 0)..., 0 };
		(void)Temp;
	}

	/** Utility function called when the task is dispatched to populate the filter based on our component typs */
	void PopulateSubsequents(const FGraphEventRef& InEvent, FSystemSubsequentTasks& OutSubsequents) const
	{
		OutSubsequents.AddMasterTask(InEvent);

		int Temp[] = { (UE::MovieScene::PopulateSubsequents(&Accessors.template Get<Indices>(), InEvent, OutSubsequents), 0)..., 0 };
		(void)Temp;
	}

	/** Lock the component headers that we need to access */
	void Lock(FEntityAllocation* Allocation) const
	{
		//SCOPE_CYCLE_COUNTER(MovieSceneEval_AquireComponentAccessLocks);

		int Temp[] = { (LockHeader(&Accessors.template Get<Indices>(), Allocation), 0)..., 0 };
		(void)Temp;
	}

	/** Unlock the component headers that we accessed in Lock() */
	void Unlock(FEntityAllocation* Allocation, uint64 SystemSerial) const
	{
		//SCOPE_CYCLE_COUNTER(MovieSceneEval_ReleaseComponentAccessLocks);

		int Temp[] = { ( UnlockHeader(&Accessors.template Get<Indices>(), Allocation, SystemSerial), 0)..., 0 };
		(void)Temp;
	}

	/**
	 * Perform a thread-safe iteration of the specified allocation using this task, inline on the current thread
	 *
	 * @param Allocation  The allocation to iterate
	 * @return An iterator that defines the full range of entities in the allocation
	 */
	EntityRangeType IterateAllocation(const FEntityAllocation* Allocation) const
	{
		EntityRangeType Result(Allocation->Num());

		static_assert(TAnd<TSupportsDirectEntityIteration<T>...>::Value, "Accessor type(s) do not support direct entity iteration - must be resolved separately.");

		checkf(IsValid(), TEXT("Attempting to use a component pack with an invalid component type."));

		int Unused[] = { (Result.template GetComponentArrayReference<Indices>() = Accessors.template Get<Indices>().Resolve(Allocation), 0)..., 0 };
		(void)Unused;

		return Result;
	}

	/**
	 * Perform a thread-safe iteration of the specified entity range using this task, inline on the current thread
	 *
	 * @param Allocation  The allocation to iterate
	 * @return An iterator that defines the full range of entities in the allocation
	 */
	EntityRangeType IterateRange(const FEntityRange& EntityRange) const
	{
		check(EntityRange.ComponentStartOffset >= 0 && EntityRange.ComponentStartOffset + EntityRange.Num <= EntityRange.Allocation->Num());

		EntityRangeType Result = IterateAllocation(EntityRange.Allocation);
		Result.Slice(EntityRange.ComponentStartOffset, EntityRange.Num);
		return Result;
	}

	/**
	 * Perform a thread-safe iteration of all matching allocations within the specified entity manager using this task, inline on the current thread
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param InCallback     A callable type that matches the signature defined by ForEachAllocation ie void(FEntityAllocation*, const TFilteredEntityTask<T...>&)
	 */
	template<typename Callback>
	void Iterate_PerAllocation(FEntityManager* EntityManager, Callback&& InCallback) const
	{
		FEntityComponentFilter Filter;
		PopulateFilter(&Filter);
		Iterate_PerAllocationImpl(EntityManager, Filter, InCallback);
	}

	/**
	 * Perform a thread-safe iteration of all matching entities specified entity manager using this task, inline on the current thread
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param InCallback     A callable type that matches the signature defined by ForEachEntity ie void(typename T::AccessType...)
	 */
	template<typename Callback>
	void Iterate_PerEntity(FEntityManager* EntityManager, Callback&& InCallback) const
	{
		FEntityComponentFilter Filter;
		PopulateFilter(&Filter);
		Iterate_PerEntityImpl(EntityManager, Filter, InCallback);
	}

	/**
	 * Implementation function for Iterate_PerEntity
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param Filter         Filter that at least must match the types specified by this task
	 * @param InCallback     A callable type that matches the signature defined by ForEachEntity ie void(typename T::AccessType...)
	 */
	template<typename Callback>
	void Iterate_PerEntityImpl(FEntityManager* EntityManager, const FEntityComponentFilter& Filter, Callback&& InCallback) const
	{
		if (IsValid())
		{
			const uint64 SystemSerial = EntityManager->GetSystemSerial();
			for (FEntityAllocation* Allocation : EntityManager->Iterate(&Filter))
			{
				FEntityIterationResult Result;

				// Lock on the components we want to access
				Lock(Allocation);

				auto IterState = MakeTuple( Accessors.template Get<Indices>().CreateIterState(Allocation)... );

				const int32 Num = Allocation->Num();
				for (int32 ComponentOffset = 0; ComponentOffset < Num && Result.Value; ++ComponentOffset )
				{
					Result = (InCallback( *IterState.template Get<Indices>()... ), Result);

					int Temp[] = { (++IterState.template Get<Indices>(), 0)..., 0 };
					(void)Temp;
				}

				// Unlock from the components we wanted to access
				Unlock(Allocation, SystemSerial);
			}
		}
	}

	/**
	 * Implementation function for Iterate_PerAllocation
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param Filter         Filter that at least must match the types specified by this task
	 * @param InCallback     A callable type that matches the signature defined by ForEachAllocation ie void(FEntityAllocation*, const TFilteredEntityTask<T...>&)
	 */
	template<typename Callback>
	void Iterate_PerAllocationImpl(FEntityManager* EntityManager, const FEntityComponentFilter& Filter, Callback&& InCallback) const
	{
		if (IsValid())
		{
			const uint64 SystemSerial = EntityManager->GetSystemSerial();
			for (FEntityAllocation* Allocation : EntityManager->Iterate(&Filter))
			{
				// Lock on the components we want to access
				Lock(Allocation);

				FEntityIterationResult Result = (InCallback(Allocation, Accessors.template Get<Indices>()...), FEntityIterationResult{});

				// Unlock from the components we wanted to access
				Unlock(Allocation, SystemSerial);

				if (!Result)
				{
					break;
				}
			}
		}
	}

public:

	/**
	 * Get the accessor for a specific index within this task
	 */
	template<int Index>
	FORCEINLINE auto GetAccessor() const
	{
		static_assert(Index < sizeof...(T), TEXT("Invalid component index specified"));
		return Accessors.template Get<Index>();
	}

	FString ToString(FEntityManager* EntityManager) const
	{
#if UE_MOVIESCENE_ENTITY_DEBUG
		FString Result;

		int Unused[] = { (AccessorToString(&Accessors.template Get<Indices>(), EntityManager, Result), 0)... };
		(void)Unused;

		return Result;
#else
		return TEXT("<debug info compiled out> - enable UE_MOVIESCENE_ENTITY_DEBUG");
#endif
	}

protected:

	template<typename... ConstructionTypes>
	explicit TEntityTaskComponentsImpl(ConstructionTypes&&... InTypes)
		: Accessors{ Forward<ConstructionTypes>(InTypes)... }
	{}

protected:

	template<typename...> friend struct TEntityTaskComponentsImpl;

	TTuple<T...> Accessors;
};


/**
 * Main entry point utility for create tasks that run over component data
 */
struct FEntityTaskBuilder : TEntityTaskComponents<>
{
	FEntityTaskBuilder() : TEntityTaskComponents<>() {}
};


template<typename... T>
struct TFilteredEntityTask
{
	TFilteredEntityTask(const TEntityTaskComponents<T...>& InComponents)
		: Components(InComponents)
		, bBreakOnRun(InComponents.bBreakOnRun)
		, CurrentThread(InComponents.CurrentThread)
		, DesiredThread(InComponents.DesiredThread)
		, StatId(InComponents.StatId)
	{
		Components.PopulateFilter(&Filter);
	}
	TFilteredEntityTask(const TEntityTaskComponents<T...>& InComponents, const FEntityComponentFilter& InFilter)
		: Components(InComponents)
		, Filter(InFilter)
		, bBreakOnRun(InComponents.bBreakOnRun)
		, CurrentThread(InComponents.CurrentThread)
		, DesiredThread(InComponents.DesiredThread)
		, StatId(InComponents.StatId)
	{
		Components.PopulateFilter(&Filter);
	}


	/**
	 * Constrain this task to only run for entities that have all the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterAll(const FComponentMask& InComponentMask)
	{
		Filter.All(InComponentMask);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that have all the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterAll(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		Filter.All(InComponentTypes);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that have none the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterNone(const FComponentMask& InComponentMask)
	{
		Filter.None(InComponentMask);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that have none the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterNone(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		Filter.None(InComponentTypes);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that have at least one of the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterAny(const FComponentMask& InComponentMask)
	{
		Filter.Any(InComponentMask);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that have at least one of the specified components or tags
	 */
	TFilteredEntityTask< T... >& FilterAny(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		Filter.Any(InComponentTypes);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that do not have the specific combination of components or tags
	 */
	TFilteredEntityTask< T... >& FilterOut(const FComponentMask& InComponentMask)
	{
		Filter.Deny(InComponentMask);
		return *this;
	}

	/**
	 * Constrain this task to only run for entities that do not have the specific combination of components or tags
	 */
	TFilteredEntityTask< T... >& FilterOut(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		Filter.Deny(InComponentTypes);
		return *this;
	}

	/**
	 * Combine this task's filter with the specified filter
	 */
	TFilteredEntityTask< T... >& CombineFilter(const FEntityComponentFilter& InFilter)
	{
		Filter.Combine(InFilter);
		return *this;
	}

	/**
	 * Assign the current thread for task dispatch to ensure that it is issued on the correct thread.
	 * This should only be required for tasks dispatched outside of the main linker execution, or tasks dispatched for the global entity manager
	 */
	TFilteredEntityTask< T... >& SetCurrentThread(ENamedThreads::Type InCurrentThread)
	{
		CurrentThread = InCurrentThread;
		return *this;
	}

	/**
	 * Assign a desired thread for this task to run on
	 */
	TFilteredEntityTask< T... >& SetDesiredThread(ENamedThreads::Type InDesiredThread)
	{
		DesiredThread = InDesiredThread;
		return *this;
	}

	/**
	 * Assign a stat ID for this task
	 */
	TFilteredEntityTask< T... >& SetStat(TStatId InStatId)
	{
		StatId = InStatId;
		return *this;
	}

	/**
	 * Access the pre-populated filter that should be used for iterating relevant entities for this task
	 */
	const FEntityComponentFilter& GetFilter() const
	{
		return Filter;
	}

	/**
	 * Access the underlying component access definitions
	 */
	const TEntityTaskComponents<T...>& GetComponents() const
	{
		return Components;
	}


	/**
	 * Dispatch a task for every entity that matches the filters and component types. Must be explicitly instantiated with the task type to dispatch. Construction arguments are deduced.
	 * Tasks must implement a ForEachEntity function that matches this task's component accessor types.
	 * 
	 * For example:
	 * struct FForEachEntity
	 * {
	 *     void ForEachEntity(FMovieSceneEntityID InEntityID, const FMovieSceneFloatChannel& Channel);
	 * };
	 *
	 * TComponentTypeID<FMovieSceneFloatChannel> FloatChannelComponent = ...;
	 *
	 * FGraphEventRef Task = FEntityTaskBuilder()
	 * .ReadEntityIDs()
	 * .Read(FloatChannelComponent)
	 * .SetStat(GET_STATID(MyStatName))
	 * .SetDesiredThread(ENamedThreads::AnyThread)
	 * .Dispatch_PerEntity<FForEachEntity>(EntityManager, Prerequisites);
	 *
	 * @param EntityManager    The entity manager to run the task on. All component types *must* relate to this entity manager.
	 * @param Prerequisites    Prerequisite tasks that must run before this one, or nullptr if there are no prerequisites
	 * @param Subsequents      (Optional) Subsequent task tracking that this task should be added to for each writable component type
	 * @param InArgs           Optional arguments that are forwarded to the constructor of TaskImpl
	 * @return A pointer to the graph event for the task, or nullptr if this task is not valid (ie contains invalid component types that would be necessary for the task to run), or threading is disabled
	 */
	template<typename TaskImpl, typename... TaskConstructionArgs>
	FGraphEventRef Dispatch_PerAllocation(FEntityManager* EntityManager, const FSystemTaskPrerequisites& Prerequisites, FSystemSubsequentTasks* Subsequents, TaskConstructionArgs&&... InArgs) const
	{
		checkfSlow(IsInGameThread(), TEXT("Tasks can only be dispatched from the game thread."));

		if (!Components.IsValid())
		{
			return nullptr;
		}

		if (EntityManager->GetThreadingModel() == EEntityThreadingModel::NoThreading)
		{
			TaskImpl Task{ Forward<TaskConstructionArgs>(InArgs)... };
			TEntityAllocationTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
			return nullptr;
		}
		else
		{
			FGraphEventArray GatheredPrereqs;
			Components.PopulatePrerequisites(Prerequisites, &GatheredPrereqs);

			ENamedThreads::Type ThisThread = CurrentThread == ENamedThreads::AnyThread ? EntityManager->GetDispatchThread() : CurrentThread;
			checkSlow(ThisThread != ENamedThreads::AnyThread);

			FGraphEventRef NewTask = TGraphTask< TEntityAllocationTask<TaskImpl, T...> >::CreateTask(GatheredPrereqs.Num() != 0 ? &GatheredPrereqs : nullptr, ThisThread)
				.ConstructAndDispatchWhenReady( EntityManager, *this, DesiredThread, StatId, bBreakOnRun, Forward<TaskConstructionArgs>(InArgs)... );

			if (Subsequents)
			{
				Components.PopulateSubsequents(NewTask, *Subsequents);
			}

			return NewTask;
		}
	}

	template<typename TaskImpl>
	void RunInline_PerAllocation(FEntityManager* EntityManager, TaskImpl& Task) const
	{
		if (Components.IsValid())
		{
			TEntityAllocationTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
		}
	}

	/**
	 * Dispatch a task for every entity that matches the filters and component types. Must be explicitly instantiated with the task type to dispatch. Construction arguments are deduced.
	 * Tasks must implement a ForEachEntity function that matches this task's component accessor types.
	 * 
	 * For example:
	 * struct FForEachEntity
	 * {
	 *     void ForEachEntity(FMovieSceneEntityID InEntityID, const FMovieSceneFloatChannel& Channel);
	 * };
	 *
	 * TComponentTypeID<FMovieSceneFloatChannel> FloatChannelComponent = ...;
	 *
	 * FGraphEventRef Task = FEntityTaskBuilder()
	 * .ReadEntityIDs()
	 * .Read(FloatChannelComponent)
	 * .SetStat(GET_STATID(MyStatName))
	 * .SetDesiredThread(ENamedThreads::AnyThread)
	 * .Dispatch_PerEntity<FForEachEntity>(EntityManager, Prerequisites);
	 *
	 * @param EntityManager    The entity manager to run the task on. All component types *must* relate to this entity manager.
	 * @param Prerequisites    Prerequisite tasks that must run before this one, or nullptr if there are no prerequisites
	 * @param Subsequents      (Optional) Subsequent task tracking that this task should be added to for each writable component type
	 * @param InArgs           Optional arguments that are forwarded to the constructor of TaskImpl
	 * @return A pointer to the graph event for the task, or nullptr if this task is not valid (ie contains invalid component types that would be necessary for the task to run), or threading is disabled
	 */
	template<typename TaskImpl, typename... TaskConstructionArgs>
	FGraphEventRef Dispatch_PerEntity(FEntityManager* EntityManager, const FSystemTaskPrerequisites& Prerequisites, FSystemSubsequentTasks* Subsequents, TaskConstructionArgs&&... InArgs) const
	{
		checkfSlow(IsInGameThread(), TEXT("Tasks can only be dispatched from the game thread."));

		if (!Components.IsValid())
		{
			return nullptr;
		}

		if (EntityManager->GetThreadingModel() == EEntityThreadingModel::NoThreading)
		{
			TaskImpl Task{ Forward<TaskConstructionArgs>(InArgs)... };
			TEntityTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
			return nullptr;
		}
		else
		{
			FGraphEventArray GatheredPrereqs;
			Components.PopulatePrerequisites(Prerequisites, &GatheredPrereqs);

			ENamedThreads::Type ThisThread = CurrentThread == ENamedThreads::AnyThread ? EntityManager->GetDispatchThread() : CurrentThread;
			checkSlow(ThisThread != ENamedThreads::AnyThread);

			FGraphEventRef NewTask = TGraphTask< TEntityTask<TaskImpl, T...> >::CreateTask(GatheredPrereqs.Num() != 0 ? &GatheredPrereqs : nullptr, ThisThread)
				.ConstructAndDispatchWhenReady( EntityManager, *this, DesiredThread, StatId, bBreakOnRun, Forward<TaskConstructionArgs>(InArgs)... );

			if (Subsequents)
			{
				Components.PopulateSubsequents(NewTask, *Subsequents);
			}

			return NewTask;
		}
	}

	template<typename TaskImpl>
	void RunInline_PerEntity(FEntityManager* EntityManager, TaskImpl Task) const
	{
		if (Components.IsValid())
		{
			TEntityTaskBase<TaskImpl, T...>(EntityManager, *this).Run(Task);
		}
	}

	/**
	 * Perform a thread-safe iteration of all matching entities specified entity manager using this task, inline on the current thread
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param InCallback     A callable type that matches the signature defined by ForEachEntity ie void(typename T::AccessType...)
	 */
	template<typename Callback>
	void Iterate_PerEntity(FEntityManager* EntityManager, Callback&& InCallback) const
	{
		Components.Iterate_PerEntityImpl(EntityManager, Filter, Forward<Callback>(InCallback));
	}

	/**
	 * Perform a thread-safe iteration of all matching allocations within the specified entity manager using this task, inline on the current thread
	 *
	 * @param EntityManager  The entity manager to iterate allocations for. All component type IDs in this class must relate to this entity manager
	 * @param InCallback     A callable type that matches the signature defined by ForEachAllocation ie void(FEntityAllocation*, const TFilteredEntityTask<T...>&)
	 */
	template<typename Callback>
	void Iterate_PerAllocation(FEntityManager* EntityManager, Callback&& InCallback) const
	{
		Components.Iterate_PerAllocationImpl(EntityManager, Filter, Forward<Callback>(InCallback));
	}

private:

	TEntityTaskComponents<T...> Components;

	FEntityComponentFilter Filter;

	bool bBreakOnRun;

	ENamedThreads::Type CurrentThread;

	ENamedThreads::Type DesiredThread;

	TStatId StatId;
};

template<typename TaskImpl, typename... ComponentTypes>
struct TEntityTaskBase
{
	explicit TEntityTaskBase(FEntityManager* InEntityManager, const TEntityTaskComponents<ComponentTypes...>& InComponents)
		: FilteredTask(InComponents)
		, EntityManager(InEntityManager)
		, SystemSerial(InEntityManager->GetSystemSerial())
	{}

	explicit TEntityTaskBase(FEntityManager* InEntityManager, const TFilteredEntityTask<ComponentTypes...>& InFilteredTask)
		: FilteredTask(InFilteredTask)
		, EntityManager(InEntityManager)
		, SystemSerial(InEntityManager->GetSystemSerial())
	{}

	void Run(TaskImpl& TaskImplInstance)
	{
		UE_LOG(LogMovieScene, VeryVerbose, TEXT("Running entity task the following components: %s"), *FilteredTask.GetComponents().ToString(EntityManager));

		PreTask(&TaskImplInstance);

		for (FEntityAllocation* Allocation : EntityManager->Iterate(&FilteredTask.GetFilter()))
		{
			FilteredTask.GetComponents().Lock(Allocation);

			Caller::ForEachEntity(TaskImplInstance, Allocation, FilteredTask.GetComponents());

			FilteredTask.GetComponents().Unlock(Allocation, SystemSerial);
		}

		PostTask(&TaskImplInstance);
	}

private:

	static void PreTask(void*, ...){}
	template <typename T> static void PreTask(T* InTask, decltype(&T::PreTask)* = 0)
	{
		InTask->PreTask();
	}

	static void PostTask(void*, ...){}
	template <typename T> static void PostTask(T* InTask, decltype(&T::PostTask)* = 0)
	{
		InTask->PostTask();
	}

	using Caller = TEntityTaskCaller< TaskImpl, sizeof...(ComponentTypes), TEntityTaskTraits<TaskImpl>::AutoExpandAccessors >;

	TFilteredEntityTask<ComponentTypes...> FilteredTask;
	FEntityManager* EntityManager;
	uint64 SystemSerial;
};


template<typename TaskImpl, typename... ComponentTypes>
struct TEntityTask : TEntityTaskBase<TaskImpl, ComponentTypes...>
{

	template<typename... ArgTypes>
	explicit TEntityTask(FEntityManager* InEntityManager, const TEntityTaskComponents<ComponentTypes...>& InComponents, ENamedThreads::Type InDesiredThread, TStatId InStatId, bool bInBreakOnRun, ArgTypes&&... InArgs)
		: TEntityTaskBase<TaskImpl, ComponentTypes...>(InEntityManager, InComponents)
		, TaskImplInstance{ Forward<ArgTypes>(InArgs)... }
		, DesiredThread(InDesiredThread)
		, StatId(InStatId)
		, bBreakOnRun(bInBreakOnRun)
	{}

	template<typename... ArgTypes>
	explicit TEntityTask(FEntityManager* InEntityManager, const TFilteredEntityTask<ComponentTypes...>& InFilteredTask, ENamedThreads::Type InDesiredThread, TStatId InStatId, bool bInBreakOnRun, ArgTypes&&... InArgs)
		: TEntityTaskBase<TaskImpl, ComponentTypes...>(InEntityManager, InFilteredTask)
		, TaskImplInstance{ Forward<ArgTypes>(InArgs)... }
		, DesiredThread(InDesiredThread)
		, StatId(InStatId)
		, bBreakOnRun(bInBreakOnRun)
	{}

	TStatId GetStatId() const
	{
		return StatId;
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return DesiredThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionGraphEvent)
	{
		if (bBreakOnRun)
		{
			UE_DEBUG_BREAK();
		}

		if ((DesiredThread & ENamedThreads::AnyThread) == 0)
		{
			checkf(CurrentThread == DesiredThread, TEXT("MovieScene evaluation task is not being run on its desired thread"));
		}

		this->Run(TaskImplInstance);
	}

private:

	TaskImpl TaskImplInstance;

	ENamedThreads::Type DesiredThread;

	TStatId StatId;

	bool bBreakOnRun;
};

template<typename TaskImpl, typename... ComponentTypes>
struct TEntityAllocationTaskBase
{
	explicit TEntityAllocationTaskBase(FEntityManager* InEntityManager, const TEntityTaskComponents<ComponentTypes...>& InComponents)
		: ComponentFilter(InComponents)
		, EntityManager(InEntityManager)
		, SystemSerial(InEntityManager->GetSystemSerial())
	{}

	explicit TEntityAllocationTaskBase(FEntityManager* InEntityManager, const TFilteredEntityTask<ComponentTypes...>& InComponentFilter)
		: ComponentFilter(InComponentFilter)
		, EntityManager(InEntityManager)
		, SystemSerial(InEntityManager->GetSystemSerial())
	{}

	void Run(TaskImpl& TaskImplInstance)
	{
		UE_LOG(LogMovieScene, VeryVerbose, TEXT("Running entity task the following components: %s"), *ComponentFilter.GetComponents().ToString(EntityManager));

		PreTask(&TaskImplInstance);

		for (FEntityAllocation* Allocation : EntityManager->Iterate(&ComponentFilter.GetFilter()))
		{
			// Lock on the components we want to access
			ComponentFilter.GetComponents().Lock(Allocation);

			Caller::ForEachAllocation(TaskImplInstance, Allocation, ComponentFilter.GetComponents());

			// Unlock from the components we wanted to access
			ComponentFilter.GetComponents().Unlock(Allocation, SystemSerial);
		}

		PostTask(&TaskImplInstance);
	}

private:

	static void PreTask(void*, ...){}
	template <typename T> static void PreTask(T* InTask, decltype(&T::PreTask)* = 0)
	{
		InTask->PreTask();
	}

	static void PostTask(void*, ...){}
	template <typename T> static void PostTask(T* InTask, decltype(&T::PostTask)* = 0)
	{
		InTask->PostTask();
	}

protected:

	using Caller = TEntityTaskCaller< TaskImpl, sizeof...(ComponentTypes), TEntityTaskTraits<TaskImpl>::AutoExpandAccessors >;

	TFilteredEntityTask<ComponentTypes...> ComponentFilter;
	FEntityManager* EntityManager;
	uint64 SystemSerial;
};

template<typename TaskImpl, typename... ComponentTypes>
struct TEntityAllocationTask : TEntityAllocationTaskBase<TaskImpl, ComponentTypes...>
{
	template<typename... ArgTypes>
	explicit TEntityAllocationTask(FEntityManager* InEntityManager, const TEntityTaskComponents<ComponentTypes...>& InComponents, ENamedThreads::Type InDesiredThread, TStatId InStatId, bool bInBreakOnRun, ArgTypes&&... InArgs)
		: TEntityAllocationTaskBase<TaskImpl, ComponentTypes...>(InEntityManager, InComponents)
		, TaskImplInstance{ Forward<ArgTypes>(InArgs)... }
		, DesiredThread(InDesiredThread)
		, StatId(InStatId)
		, bBreakOnRun(bInBreakOnRun)
	{}

	template<typename... ArgTypes>
	explicit TEntityAllocationTask(FEntityManager* InEntityManager, const TFilteredEntityTask<ComponentTypes...>& InComponentFilter, ENamedThreads::Type InDesiredThread, TStatId InStatId, bool bInBreakOnRun, ArgTypes&&... InArgs)
		: TEntityAllocationTaskBase<TaskImpl, ComponentTypes...>(InEntityManager, InComponentFilter)
		, TaskImplInstance{ Forward<ArgTypes>(InArgs)... }
		, DesiredThread(InDesiredThread)
		, StatId(InStatId)
		, bBreakOnRun(bInBreakOnRun)
	{}

	TStatId GetStatId() const
	{
		return StatId;
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return DesiredThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionGraphEvent)
	{
		if (bBreakOnRun)
		{
			UE_DEBUG_BREAK();
		}

		if ((DesiredThread & ENamedThreads::AnyThread) == 0)
		{
			checkf(CurrentThread == DesiredThread, TEXT("MovieScene evaluation task is not being run on its desired thread"));
		}

		this->Run(TaskImplInstance);
	}

private:

	TaskImpl TaskImplInstance;

	ENamedThreads::Type DesiredThread;

	TStatId StatId;

	bool bBreakOnRun;
};



template<typename...> struct TEntityTaskCaller_AutoExpansion;

template<typename TaskImpl, int... Indices>
struct TEntityTaskCaller_AutoExpansion<TaskImpl, TIntegerSequence<int, Indices...>>
{
	template<typename... ComponentTypes>
	FORCEINLINE static void ForEachEntity(TaskImpl& TaskImplInstance, const FEntityAllocation* Allocation, const TEntityTaskComponents<ComponentTypes...>& Components)
	{
		auto IterState = MakeTuple( Components.template GetAccessor<Indices>().CreateIterState(Allocation)... );

		const int32 Num = Allocation->Num();
		for (int32 ComponentOffset = 0; ComponentOffset < Num; ++ComponentOffset )
		{
			TaskImplInstance.ForEachEntity( *IterState.template Get<Indices>()... );
			int Temp[] = { (++IterState.template Get<Indices>(), 0)... };
			(void)Temp;
		}
	}

	template<typename... ComponentTypes>
	FORCEINLINE static void ForEachAllocation(TaskImpl& TaskImplInstance, const FEntityAllocation* Allocation, const TEntityTaskComponents<ComponentTypes...>& Components)
	{
		TaskImplInstance.ForEachAllocation(Allocation, Components.template GetAccessor<Indices>()...);
	}
};

template<typename TaskImpl, int NumComponents>
struct TEntityTaskCaller<TaskImpl, NumComponents, true> : TEntityTaskCaller_AutoExpansion<TaskImpl, TMakeIntegerSequence<int, NumComponents>>
{
};

template<typename TaskImpl, int32 NumComponents>
struct TEntityTaskCaller<TaskImpl, NumComponents, false>
{
	template<typename... ComponentTypes>
	FORCEINLINE static void ForEachEntity(TaskImpl& TaskImplInstance, const FEntityAllocation* Allocation, const TEntityTaskComponents<ComponentTypes...>& Components)
	{
		for (const auto& Entity : Components.IterateAllocation(Allocation))
		{
			TaskImplInstance.ForEachEntity(Entity);
		}
	}

	template<typename... ComponentTypes>
	FORCEINLINE static void ForEachAllocation(TaskImpl& TaskImplInstance, const FEntityAllocation* Allocation, const TEntityTaskComponents<ComponentTypes...>& Components)
	{
		TaskImplInstance.ForEachAllocation(Allocation, Components);
	}
};


} // namespace MovieScene
} // namespace UE
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "InstancedStructStream.h"
#include "Misc/MTAccessDetector.h"

#include "MassCommandBuffer.generated.h"


namespace ECommandBufferOperationType
{
	constexpr int None = 1 << 0;
	constexpr int Add = 1 << 1;
	constexpr int Remove = 1 << 2;
};

//@TODO: Consider debug information in case there is an assert when replaying the command buffer
// (e.g., which system added the command, or even file/line number in development builds for the specific call via a macro)
//@TODO: Support more commands

struct FMassObservedTypeCollection
{
	void Add(const UScriptStruct& TypeType, FMassEntityHandle Entity)
	{
		if (&TypeType != LastAddedType)
		{
			LastAddedType = &TypeType;
			LastUsedCollection = &Types.FindOrAdd(&TypeType);
		}
		CA_ASSUME(LastUsedCollection);
		LastUsedCollection->Add(Entity);
	}

	void Reset()
	{
		LastAddedType = nullptr;
		LastUsedCollection = nullptr;
		Types.Reset();
	}

	const TMap<const UScriptStruct*, TArray<FMassEntityHandle>>& GetTypes() const 
	{ 
		return Types; 
	}

private:
	const UScriptStruct* LastAddedType = nullptr;
	TArray<FMassEntityHandle>* LastUsedCollection = nullptr;
	TMap<const UScriptStruct*, TArray<FMassEntityHandle>> Types;
};

struct FMassCommandsObservedTypes
{
	void Reset();
	void FragmentAdded(const UScriptStruct* TypeType, FMassEntityHandle Entity)
	{
		check(TypeType);
		FragmentsToAdd.Add(*TypeType, Entity);
	}
	void FragmentRemoved(const UScriptStruct* TypeType, FMassEntityHandle Entity)
	{
		check(TypeType);
		FragmentsToRemove.Add(*TypeType, Entity);
	}

	const TMap<const UScriptStruct*, TArray<FMassEntityHandle>>& GetFragmentsToAdd() const
	{
		return FragmentsToAdd.GetTypes();
	}

	const TMap<const UScriptStruct*, TArray<FMassEntityHandle>>& GetFragmentsToRemove() const
	{
		return FragmentsToRemove.GetTypes();
	}

protected:
	FMassObservedTypeCollection FragmentsToAdd;
	FMassObservedTypeCollection FragmentsToRemove;
};


USTRUCT()
struct MASSENTITY_API FCommandBufferEntryBase
{
	GENERATED_BODY()

	FMassEntityHandle TargetEntity;

	FCommandBufferEntryBase() = default;
	virtual ~FCommandBufferEntryBase() = default;

	FCommandBufferEntryBase(FMassEntityHandle InEntity)
		: TargetEntity(InEntity)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes) { ensure(false); }
	virtual void Execute(UMassEntitySubsystem& System) const PURE_VIRTUAL(FCommandBufferEntryBase::Execute, );
};
template<> struct TStructOpsTypeTraits<FCommandBufferEntryBase> : public TStructOpsTypeTraitsBase2<FCommandBufferEntryBase> { enum { WithPureVirtual = true, }; };

/**
 * Command dedicated to execute a captured lambda 
 */
USTRUCT()
struct MASSENTITY_API FDeferredCommand : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum 
	{
		Type = ECommandBufferOperationType::None
	};

	FDeferredCommand() = default;

	FDeferredCommand(const TFunction<void(UMassEntitySubsystem& System)>& InDeferredFunction)
		: DeferredFunction(InDeferredFunction)
	{}

	virtual void Execute(UMassEntitySubsystem& System) const override
	{
		DeferredFunction(System);
	}

protected:
	TFunction<void(UMassEntitySubsystem& System)> DeferredFunction;
};

/**
 * Command dedicated to building an entity from an existing fragment instance
 */
USTRUCT()
struct MASSENTITY_API FBuildEntityFromFragmentInstance : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum 
	{
		Type = ECommandBufferOperationType::Add
	};

	FBuildEntityFromFragmentInstance() = default;
	FBuildEntityFromFragmentInstance(const FMassEntityHandle Entity, FStructView InStruct)
		: FCommandBufferEntryBase(Entity)
		, Struct(InStruct)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		ObservedTypes.FragmentAdded(Struct.GetScriptStruct(), TargetEntity);
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FInstancedStruct Struct;
};

/**
* Command dedicated to building an entity from a list of existing fragment instances
*/
USTRUCT()
struct MASSENTITY_API FBuildEntityFromFragmentInstances : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Add
	};

	FBuildEntityFromFragmentInstances() = default;
	FBuildEntityFromFragmentInstances(const FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> InInstances)
		: FCommandBufferEntryBase(Entity)
		, Instances(InInstances)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		for (const FInstancedStruct& Struct : Instances)
		{
			ObservedTypes.FragmentAdded(Struct.GetScriptStruct(), TargetEntity);
		}
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<FInstancedStruct> Instances;
};

/**
 * Command dedicated to add a new fragment to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddFragment : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Add
	};

	FCommandAddFragment() = default;
	FCommandAddFragment(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		ObservedTypes.FragmentAdded(StructParam, TargetEntity);
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command dedicated to add a new fragment from an existing instance
 */
USTRUCT()
struct MASSENTITY_API FCommandAddFragmentInstance : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Add
	};

	FCommandAddFragmentInstance() = default;
	FCommandAddFragmentInstance(const FMassEntityHandle InEntity, FStructView InStruct)
        : FCommandBufferEntryBase(InEntity)
        , Struct(InStruct)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		ObservedTypes.FragmentAdded(Struct.GetScriptStruct(), TargetEntity);
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FInstancedStruct Struct;
};

/**
 * Command that adds a list of instanced fragments to a given entity
 */
USTRUCT()
struct MASSENTITY_API FMassCommandAddFragmentInstanceList : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Add
	};

	FMassCommandAddFragmentInstanceList() = default;

	FMassCommandAddFragmentInstanceList(const FMassEntityHandle InEntity, std::initializer_list<FInstancedStruct> InitList)
		: FCommandBufferEntryBase(InEntity)
		, FragmentList(InitList)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		for (const FInstancedStruct& Struct : FragmentList)
		{
			ObservedTypes.FragmentAdded(Struct.GetScriptStruct(), TargetEntity);
		}
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<FInstancedStruct> FragmentList;
};

/**
 * Command dedicated to remove a fragment from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveFragment : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Remove
	};
	
	FCommandRemoveFragment() = default;
	FCommandRemoveFragment(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		ObservedTypes.FragmentRemoved(StructParam, TargetEntity);
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command performing addition of a list of fragments to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddFragmentList : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Add
	};

	FCommandAddFragmentList() = default;
	FCommandAddFragmentList(FMassEntityHandle InEntity, TConstArrayView<const UScriptStruct*> InFragmentList)
		: FCommandBufferEntryBase(InEntity)
		, FragmentList(InFragmentList)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		for (const UScriptStruct* StructParam : FragmentList)
		{
			ObservedTypes.FragmentAdded(StructParam, TargetEntity);
		}
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<const UScriptStruct*> FragmentList;
};

/**
 * Command performing a removal of a list of fragments from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveFragmentList : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Remove
	};

	FCommandRemoveFragmentList() = default;
	FCommandRemoveFragmentList(FMassEntityHandle InEntity, TConstArrayView<const UScriptStruct*> InFragmentList)
		: FCommandBufferEntryBase(InEntity)
		, FragmentList(InFragmentList)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		for (const UScriptStruct* StructParam : FragmentList)
		{
			ObservedTypes.FragmentRemoved(StructParam, TargetEntity);
		}
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<const UScriptStruct*> FragmentList;
};

/**
 * Command dedicated to add a new tag to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddTag: public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::None
	};

	FCommandAddTag() = default;
	FCommandAddTag(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command dedicated to remove a tag from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveTag : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::None
	};

	FCommandRemoveTag() = default;
	FCommandRemoveTag(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command for swapping given tags for a given entity. Note that the entity doesn't need to own the "old tag", the 
 * "new tag" will be added regardless.
 */
USTRUCT()
struct MASSENTITY_API FCommandSwapTags : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::None //ECommandBufferOperationType::Add | ECommandBufferOperationType::Remove
	};

	FCommandSwapTags() = default;
	FCommandSwapTags(const FMassEntityHandle InEntity, const UScriptStruct* InOldTagType, const UScriptStruct* InNewTagType)
		: FCommandBufferEntryBase(InEntity)
		, OldTagType(InOldTagType)
		, NewTagType(InNewTagType)
	{
		checkf((InOldTagType == nullptr) || InOldTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("FCommandSwapTags works only with tags while '%s' is not one."), *GetPathNameSafe(InOldTagType));
		checkf((InNewTagType == nullptr) || InNewTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("FCommandSwapTags works only with tags while '%s' is not one."), *GetPathNameSafe(InNewTagType));
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	const UScriptStruct* OldTagType = nullptr;
	const UScriptStruct* NewTagType = nullptr;
};

/**
 * Command performing a removal of a collection of fragments and tags
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveComposition : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	enum
	{
		Type = ECommandBufferOperationType::Remove
	};

	FCommandRemoveComposition() = default;
	FCommandRemoveComposition(FMassEntityHandle InEntity, const FMassArchetypeCompositionDescriptor& InDescriptor)
		: FCommandBufferEntryBase(InEntity)
		, Descriptor(InDescriptor)
	{}

	void AppendAffectedEntitesPerType(FMassCommandsObservedTypes& ObservedTypes)
	{
		if (Descriptor.Fragments.IsEmpty() == false)
		{
			// @todo this is way too slow and I hate it. I'm considering adding abother flag to EOperationType to indicate
			// a composition-based operation and call a different function or process those at flush time. Thoughts?
			TArray<const UScriptStruct*> Fragments;
			Descriptor.Fragments.ExportTypes(Fragments);
			for (const UScriptStruct* StructParam : Fragments)
			{
				ObservedTypes.FragmentRemoved(StructParam, TargetEntity);
			}
		}
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FMassArchetypeCompositionDescriptor Descriptor;
};

struct MASSENTITY_API FMassCommandBuffer
{
public:
	FMassCommandBuffer() = default;

	/** Push any command, requires to derive from FCommandBufferEntryBase */
	template< typename T, typename = typename TEnableIf<TIsDerivedFrom<typename TRemoveReference<T>::Type, FCommandBufferEntryBase>::IsDerived, void>::Type >
	void PushCommand(const T& Command)
	{
		EmplaceCommand<T>(Command);
	}

	/** Emplace any command, requires to derive from FCommandBufferEntryBase */
	template< typename T, typename = typename TEnableIf<TIsDerivedFrom<typename TRemoveReference<T>::Type, FCommandBufferEntryBase>::IsDerived, void>::Type, typename... TArgs >
	void EmplaceCommand(TArgs&&... InArgs)
	{
		EmplaceCommand_GetRef<T>(Forward<TArgs>(InArgs)...);
	}

	/** Emplace any command and return its ref, requires to derive from FCommandBufferEntryBase */
	template< typename T, typename = typename TEnableIf<TIsDerivedFrom<typename TRemoveReference<T>::Type, FCommandBufferEntryBase>::IsDerived, void>::Type, typename... TArgs >
	T& EmplaceCommand_GetRef(TArgs&&... InArgs)
	{
		UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
		T& Command = PendingCommands.Emplace_GetRef<T>(Forward<TArgs>(InArgs)...); 
		if (constexpr bool bIsModifyingComposition = ((T::Type & (ECommandBufferOperationType::Add | ECommandBufferOperationType::Remove)) != 0))
		{	
			Command.AppendAffectedEntitesPerType(ObservedTypes);
		}
		return Command;
	}
	
public:
	template<typename T>
	void AddFragment(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct type is not a valid fragment type.");
		EmplaceCommand<FCommandAddFragment>(Entity, T::StaticStruct());
	}

	template<typename T>
	void RemoveFragment(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct type is not a valid fragment type.");
		EmplaceCommand<FCommandRemoveFragment>(Entity, T::StaticStruct());
	}

	template<typename T>
	void AddTag(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		EmplaceCommand<FCommandAddTag>(Entity, T::StaticStruct());
	}

	template<typename T>
	void RemoveTag(FMassEntityHandle Entity)
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct type is not a valid tag type.");
		EmplaceCommand<FCommandRemoveTag>(Entity, T::StaticStruct());
	}

	void DestroyEntity(FMassEntityHandle Entity)
	{
		EntitiesToDestroy.Add(Entity);
	}

	void BatchDestroyEntities(const TArray<FMassEntityHandle>& InEntitiesToDestroy)
	{
		EntitiesToDestroy.Append(InEntitiesToDestroy);
	}

	void ReplayBufferAgainstSystem(UMassEntitySubsystem* System);

	SIZE_T GetAllocatedSize() const { return PendingCommands.GetAllocatedSize(); }

	/** 
	 * Appends the commands from the passed buffer into this one
	 * @param InOutOther the source buffer to copy the commands from. Note that after the call the InOutOther will be 
	 *	emptied due to the function using Move semantics
	 */
	void MoveAppend(FMassCommandBuffer& InOutOther);

	bool HasPendingCommands() const { return PendingCommands.Num() > 0 || EntitiesToDestroy.Num() > 0; }

private:
	FInstancedStructStream PendingCommands;
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(PendingCommandsDetector);
	FCriticalSection AppendingCommandsCS;

	TArray<FMassEntityHandle> EntitiesToDestroy;

	FMassCommandsObservedTypes ObservedTypes;
};

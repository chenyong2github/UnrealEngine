// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "InstancedStructStream.h"
#include "Misc/MTAccessDetector.h"

#include "MassCommandBuffer.generated.h"

//@TODO: Consider debug information in case there is an assert when replaying the command buffer
// (e.g., which system added the command, or even file/line number in development builds for the specific call via a macro)
//@TODO: Support more commands

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

	FBuildEntityFromFragmentInstance() = default;
	FBuildEntityFromFragmentInstance(const FMassEntityHandle Entity, FStructView InStruct)
		: FCommandBufferEntryBase(Entity)
		, Struct(InStruct)
	{}

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

	FBuildEntityFromFragmentInstances() = default;
	FBuildEntityFromFragmentInstances(const FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> InInstances)
		: FCommandBufferEntryBase(Entity)
		, Instances(InInstances)
	{}

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

	FCommandAddFragment() = default;
	FCommandAddFragment(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

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

	FCommandAddFragmentInstance() = default;
	FCommandAddFragmentInstance(const FMassEntityHandle InEntity, FStructView InStruct)
        : FCommandBufferEntryBase(InEntity)
        , Struct(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FInstancedStruct Struct;
};

/**
 * Command dedicated to remove a fragment from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveFragment : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandRemoveFragment() = default;
	FCommandRemoveFragment(FMassEntityHandle InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

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

	FCommandAddFragmentList() = default;
	FCommandAddFragmentList(FMassEntityHandle InEntity, TConstArrayView<const UScriptStruct*> InFragmentList)
		: FCommandBufferEntryBase(InEntity)
		, FragmentList(InFragmentList)
	{}

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

	FCommandRemoveFragmentList() = default;
	FCommandRemoveFragmentList(FMassEntityHandle InEntity, TConstArrayView<const UScriptStruct*> InFragmentList)
		: FCommandBufferEntryBase(InEntity)
		, FragmentList(InFragmentList)
	{}

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

	FCommandRemoveComposition() = default;
	FCommandRemoveComposition(FMassEntityHandle InEntity, const FMassArchetypeCompositionDescriptor& InDescriptor)
		: FCommandBufferEntryBase(InEntity)
		, Descriptor(InDescriptor)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FMassArchetypeCompositionDescriptor Descriptor;
};

struct MASSENTITY_API FMassCommandBuffer
{
public:

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
		UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
		return PendingCommands.Emplace<T>(Forward<TArgs>(InArgs)...);
	}

	/** Emplace any command and return its ref, requires to derive from FCommandBufferEntryBase */
	template< typename T, typename = typename TEnableIf<TIsDerivedFrom<typename TRemoveReference<T>::Type, FCommandBufferEntryBase>::IsDerived, void>::Type, typename... TArgs >
	T& EmplaceCommand_GetRef(TArgs&&... InArgs)
	{
		UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
		return PendingCommands.Emplace_GetRef<T>(Forward<TArgs>(InArgs)...);
	}

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

	int32 GetPendingCommandsCount() const { return PendingCommands.Num(); }

private:
	FInstancedStructStream PendingCommands;
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(PendingCommandsDetector);
	FCriticalSection AppendingCommandsCS;

	TArray<FMassEntityHandle> EntitiesToDestroy;
};

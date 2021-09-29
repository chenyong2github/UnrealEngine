// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "MassEntitySubsystem.h"
#include "InstancedStructStream.h"
#include "Misc/MTAccessDetector.h"

#include "LWCCommandBuffer.generated.h"

//@TODO: Consider debug information in case there is an assert when replaying the command buffer
// (e.g., which system added the command, or even file/line number in development builds for the specific call via a macro)
//@TODO: Support more commands

USTRUCT()
struct MASSENTITY_API FCommandBufferEntryBase
{
	GENERATED_BODY()

	FLWEntity TargetEntity;

	FCommandBufferEntryBase() = default;
	virtual ~FCommandBufferEntryBase() = default;

	FCommandBufferEntryBase(FLWEntity InEntity)
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
 * Command dedicated to building an entity from an existing component instance
 */
USTRUCT()
struct MASSENTITY_API FBuildEntityFromComponentInstance : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FBuildEntityFromComponentInstance() = default;
	FBuildEntityFromComponentInstance(const FLWEntity Entity, FStructView InStruct)
		: FCommandBufferEntryBase(Entity)
		, Struct(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FInstancedStruct Struct;
};

/**
* Command dedicated to building an entity from a list of existing component instances
*/
USTRUCT()
struct MASSENTITY_API FBuildEntityFromComponentInstances : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FBuildEntityFromComponentInstances() = default;
	FBuildEntityFromComponentInstances(const FLWEntity Entity, TConstArrayView<FInstancedStruct> InInstances)
		: FCommandBufferEntryBase(Entity)
		, Instances(InInstances)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<FInstancedStruct> Instances;
};

/**
 * Command dedicated to add a new component to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddComponent : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandAddComponent() = default;
	FCommandAddComponent(FLWEntity InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command dedicated to add a new component from an existing instance
 */
USTRUCT()
struct MASSENTITY_API FCommandAddComponentInstance : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandAddComponentInstance() = default;
	FCommandAddComponentInstance(const FLWEntity InEntity, FStructView InStruct)
        : FCommandBufferEntryBase(InEntity)
        , Struct(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FInstancedStruct Struct;
};

/**
 * Command dedicated to remove a component from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveComponent : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandRemoveComponent() = default;
	FCommandRemoveComponent(FLWEntity InEntity, UScriptStruct* InStruct)
		: FCommandBufferEntryBase(InEntity)
		, StructParam(InStruct)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	UScriptStruct* StructParam = nullptr;
};

/**
 * Command performing addition of a list of components to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddComponentList : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandAddComponentList() = default;
	FCommandAddComponentList(FLWEntity InEntity, TConstArrayView<const UScriptStruct*> InComponentList)
		: FCommandBufferEntryBase(InEntity)
		, ComponentList(InComponentList)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<const UScriptStruct*> ComponentList;
};

/**
 * Command performing a removal of a list of components from an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveComponentList : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandRemoveComponentList() = default;
	FCommandRemoveComponentList(FLWEntity InEntity, TConstArrayView<const UScriptStruct*> InComponentList)
		: FCommandBufferEntryBase(InEntity)
		, ComponentList(InComponentList)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	TArray<const UScriptStruct*> ComponentList;
};

/**
 * Command dedicated to add a new tag to an existing entity
 */
USTRUCT()
struct MASSENTITY_API FCommandAddTag: public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandAddTag() = default;
	FCommandAddTag(FLWEntity InEntity, UScriptStruct* InStruct)
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
	FCommandRemoveTag(FLWEntity InEntity, UScriptStruct* InStruct)
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

	FCommandSwapTags(const FLWEntity InEntity, const UScriptStruct* InOldTagType, const UScriptStruct* InNewTagType)
		: FCommandBufferEntryBase(InEntity)
		, OldTagType(InOldTagType)
		, NewTagType(InNewTagType)
	{
		checkf((InOldTagType == nullptr) || InOldTagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("FCommandSwapTags works only with tags while '%s' is not one."), *GetPathNameSafe(InOldTagType));
		checkf((InNewTagType == nullptr) || InNewTagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("FCommandSwapTags works only with tags while '%s' is not one."), *GetPathNameSafe(InNewTagType));
	}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	const UScriptStruct* OldTagType = nullptr;
	const UScriptStruct* NewTagType = nullptr;
};

/**
 * Command performing a removal of a collection of components and tags
 */
USTRUCT()
struct MASSENTITY_API FCommandRemoveComposition : public FCommandBufferEntryBase
{
	GENERATED_BODY()

	FCommandRemoveComposition() = default;
	FCommandRemoveComposition(FLWEntity InEntity, const FLWCompositionDescriptor& InDescriptor)
		: FCommandBufferEntryBase(InEntity)
		, Descriptor(InDescriptor)
	{}

protected:
	virtual void Execute(UMassEntitySubsystem& System) const override;

	FLWCompositionDescriptor Descriptor;
};

struct MASSENTITY_API FLWCCommandBuffer
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
	void AddComponent(FLWEntity Entity)
	{
		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct type is not a valid component type.");
		EmplaceCommand<FCommandAddComponent>(Entity, T::StaticStruct());
	}

	template<typename T>
	void RemoveComponent(FLWEntity Entity)
	{
		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct type is not a valid component type.");
		EmplaceCommand<FCommandRemoveComponent>(Entity, T::StaticStruct());
	}

	template<typename T>
	void AddTag(FLWEntity Entity)
	{
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived, "Given struct type is not a valid tag type.");
		EmplaceCommand<FCommandAddTag>(Entity, T::StaticStruct());
	}

	template<typename T>
	void RemoveTag(FLWEntity Entity)
	{
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived, "Given struct type is not a valid tag type.");
		EmplaceCommand<FCommandRemoveTag>(Entity, T::StaticStruct());
	}

	void DestroyEntity(FLWEntity Entity)
	{
		EntitiesToDestroy.Add(Entity);
	}

	void BatchDestroyEntities(const TArray<FLWEntity>& InEntitiesToDestroy)
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
	void MoveAppend(FLWCCommandBuffer& InOutOther);

	int32 GetPendingCommandsCount() const { return PendingCommands.Num(); }

private:
	FInstancedStructStream PendingCommands;
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(PendingCommandsDetector);
	FCriticalSection AppendingCommandsCS;

	TArray<FLWEntity> EntitiesToDestroy;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "EntitySubsystem.h"
#include "MassCommonTypes.h"
#include "MassTranslator.h"
#include "MassEntityTemplate.generated.h"


struct FEntityView;


UENUM()
enum class EMassEntityTemplateIDType : uint8
{
	None,
	ScriptStruct,
	Class,
	Instance
};

USTRUCT()
struct MASSSPAWNER_API FMassObjectHandlers
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<const UPipeProcessor*> Initializers;
	UPROPERTY()
	TArray<const UPipeProcessor*> Deinitializers;
};

//ID of the template an entity is using
USTRUCT()
struct MASSSPAWNER_API FMassEntityTemplateID
{
	GENERATED_BODY()

	FMassEntityTemplateID(uint32 InHash, EMassEntityTemplateIDType InType)
	: Hash(InHash)
	, Type(InType)
	{}

	FMassEntityTemplateID() = default;

	uint32 GetHash() const { return Hash; }
	void SetHash(uint32 InHash) { Hash = InHash; }

	EMassEntityTemplateIDType GetType() const { return Type; }
	void SetType(EMassEntityTemplateIDType InType) { Type = InType; }

	bool operator==(const FMassEntityTemplateID& Other) const
	{
		return (Hash == Other.Hash) && (Type == Other.Type);
	}

	friend uint32 GetTypeHash(const FMassEntityTemplateID& TemplateID)
	{
		return HashCombine(TemplateID.Hash, (uint32)TemplateID.Type);
	}

	bool IsValid() { return Type != EMassEntityTemplateIDType::None; }

	FString ToString() const;

protected:
	UPROPERTY()
	uint32 Hash = 0;

	UPROPERTY()
	EMassEntityTemplateIDType Type = EMassEntityTemplateIDType::None;
};

/** @todo document	*/
USTRUCT()
struct MASSSPAWNER_API FMassEntityTemplate
{
	GENERATED_BODY()

	typedef TFunction<void(UObject& /*Owner*/, FEntityView& /*EntityView*/, const EMassTranslationDirection /*CurrentDirection*/)> FObjectFragmentInitializerFunction;

	FMassEntityTemplate() = default;
	/** InArchetype is expected to be valid. The function will crash-check it. */
	void SetArchetype(const FArchetypeHandle& InArchetype);
	const FArchetypeHandle& GetArchetype() const { return Archetype; }
	/** Configures initialization and deinitialization pipeline from ObjectHandlers */
	void SetUpProcessors(const FMassObjectHandlers& ObjectHandlers, UObject& PipelineOwner);
	
	const FRuntimePipeline& GetInitializationPipeline() const { return InitializationPipeline; }
	FRuntimePipeline& GetMutableInitializationPipeline() { return InitializationPipeline; }

	const FRuntimePipeline& GetDeinitializationPipeline() const { return DeinitializationPipeline; }
	FRuntimePipeline& GetMutableDeinitializationPipeline() { return DeinitializationPipeline; }

	TConstArrayView<FObjectFragmentInitializerFunction> GetObjectFragmentInitializers() const { return ObjectInitializers; }
	TArray<FObjectFragmentInitializerFunction>& GetMutableObjectFragmentInitializers() { return ObjectInitializers; }

	bool IsValid() const { return Archetype.IsValid() && (FragmentCollection.IsEmpty() == false); }

	void SetTemplateID(FMassEntityTemplateID InTemplateID) { TemplateID = InTemplateID; }
	FMassEntityTemplateID GetTemplateID() const { return TemplateID; }

	int32 GetFragmentsNum() const { return FragmentCollection.GetFragmentsNum(); }
	TArrayView<const FInstancedStruct> GetFragments() const { return FragmentCollection.GetFragments(); }
	TArray<const UScriptStruct*>& GetMutableFragmentTypes() { return FragmentTypes; }
	TConstArrayView<const UScriptStruct*> GetFragmentTypes() const { return FragmentTypes; }
	FMassUniqueFragmentCollection& GetMutableFragmentCollection() { return FragmentCollection; }
	TConstArrayView<FInstancedStruct> GetChunkFragments() const { return ChunkFragments; }

	FLWCompositionDescriptor GetCompositionDescriptor() const { return FLWCompositionDescriptor(FragmentCollection.GetFragmentBitSet(), TagBitSet, ChunkFragmentBitSet); }

	template<typename T>
	void AddTag()
	{
		TagBitSet.Add<T>();
	}
	
	void AddTag(const UScriptStruct& TagType)
	{
		TagBitSet.Add(TagType);
	}

	const FLWTagBitSet& GetTags() const { return TagBitSet; }	
	FLWTagBitSet& GetMutableTags() { return TagBitSet; }

	template<typename T>
	void AddChunkFragment()
	{
		static_assert(TIsDerivedFrom<T, FLWChunkComponent>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FLWChunkComonent or one of its child-types.");
		ChunkFragments.AddUnique(FInstancedStruct(T::StaticStruct()));
		ChunkFragmentBitSet.Add<T>();
	}

	template<typename T>
	T& AddChunkFragment_GetRef()
	{
		static_assert(TIsDerivedFrom<T, FLWChunkComponent>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FLWChunkComonent or one of its child-types.");
		ChunkFragmentBitSet.Add<T>();
		const int32 Index = ChunkFragments.AddUnique(FInstancedStruct(T::StaticStruct()));
		return ChunkFragments[Index].template GetMutable<T>();
	}

	FString DebugGetDescription(UEntitySubsystem* EntitySubsystem = nullptr) const;
	FString DebugGetArchetypeDescription(UEntitySubsystem& EntitySubsystem) const;

private:
	FArchetypeHandle Archetype;

	FMassUniqueFragmentCollection FragmentCollection;
	FLWTagBitSet TagBitSet;
	FLWChunkComponentBitSet ChunkFragmentBitSet;
	
	UPROPERTY()
	FRuntimePipeline InitializationPipeline;
	
	UPROPERTY()
	FRuntimePipeline DeinitializationPipeline;

	/** An mirror of FMassUniqueFragmentCollection.Fragments' types, used for adding fragment types instead of fragment instances */
	UPROPERTY()
	TArray<const UScriptStruct*> FragmentTypes;

	UPROPERTY()
	TArray<FInstancedStruct> ChunkFragments;

	// These functions will be called to initialize entity's UObject-based fragments
	TArray<FObjectFragmentInitializerFunction> ObjectInitializers;

	FMassEntityTemplateID TemplateID;
};

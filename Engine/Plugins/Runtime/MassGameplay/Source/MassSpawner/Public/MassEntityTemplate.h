// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessingTypes.h"
#include "MassEntitySubsystem.h"
#include "MassCommonTypes.h"
#include "MassTranslator.h"
#include "MassEntityTemplate.generated.h"


struct FMassEntityView;


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
	TArray<const UMassFragmentInitializer*> DefaultInitializers;
	UPROPERTY()
	TArray<const UMassProcessor*> Initializers;
	UPROPERTY()
	TArray<const UMassProcessor*> Deinitializers;
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

	typedef TFunction<void(UObject& /*Owner*/, FMassEntityView& /*EntityView*/, const EMassTranslationDirection /*CurrentDirection*/)> FObjectFragmentInitializerFunction;

	FMassEntityTemplate() = default;
	/** InArchetype is expected to be valid. The function will crash-check it. */
	void SetArchetype(const FArchetypeHandle& InArchetype);
	const FArchetypeHandle& GetArchetype() const { return Archetype; }
	/** Configures initialization and deinitialization pipeline from ObjectHandlers */
	void SetUpProcessors(const FMassObjectHandlers& ObjectHandlers, UObject& PipelineOwner);
	
	const FMassRuntimePipeline& GetInitializationPipeline() const { return InitializationPipeline; }
	FMassRuntimePipeline& GetMutableInitializationPipeline() { return InitializationPipeline; }

	const FMassRuntimePipeline& GetDeinitializationPipeline() const { return DeinitializationPipeline; }
	FMassRuntimePipeline& GetMutableDeinitializationPipeline() { return DeinitializationPipeline; }

	TConstArrayView<FObjectFragmentInitializerFunction> GetObjectFragmentInitializers() const { return ObjectInitializers; }
	TArray<FObjectFragmentInitializerFunction>& GetMutableObjectFragmentInitializers() { return ObjectInitializers; }

	bool IsValid() const { return Archetype.IsValid() && (FragmentCollection.IsEmpty() == false); }

	void SetTemplateID(FMassEntityTemplateID InTemplateID) { TemplateID = InTemplateID; }
	FMassEntityTemplateID GetTemplateID() const { return TemplateID; }

	int32 GetFragmentsNum() const { return FragmentCollection.GetFragmentsNum(); }
	TArrayView<const FInstancedStruct> GetFragments() const { return FragmentCollection.GetFragments(); }
	FMassUniqueFragmentCollection& GetMutableFragmentCollection() { return FragmentCollection; }
	TConstArrayView<FInstancedStruct> GetChunkFragments() const { return ChunkFragments; }

	FMassArchetypeCompositionDescriptor GetCompositionDescriptor() const { return FMassArchetypeCompositionDescriptor(FragmentCollection.GetFragmentBitSet(), TagBitSet, ChunkFragmentBitSet); }
	FMassArchetypeFragmentsInitialValues GetArchetypeFragmentsInitialValues() const { return FMassArchetypeFragmentsInitialValues(ChunkFragments); }

	template<typename T>
	void AddTag()
	{
		TagBitSet.Add<T>();
	}
	
	void AddTag(const UScriptStruct& TagType)
	{
		TagBitSet.Add(TagType);
	}

	const FMassTagBitSet& GetTags() const { return TagBitSet; }	
	FMassTagBitSet& GetMutableTags() { return TagBitSet; }

	template<typename T>
	void AddChunkFragment()
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FLWChunkComonent or one of its child-types.");
		ChunkFragments.AddUnique(FInstancedStruct(T::StaticStruct()));
		ChunkFragmentBitSet.Add<T>();
	}

	template<typename T>
	T& AddChunkFragment_GetRef()
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FLWChunkComonent or one of its child-types.");
		ChunkFragmentBitSet.Add<T>();
		const int32 Index = ChunkFragments.AddUnique(FInstancedStruct(T::StaticStruct()));
		return ChunkFragments[Index].template GetMutable<T>();
	}

	FString DebugGetDescription(UMassEntitySubsystem* EntitySubsystem = nullptr) const;
	FString DebugGetArchetypeDescription(UMassEntitySubsystem& EntitySubsystem) const;

	template<typename T>
	bool HasFragment() const
	{
		return FragmentCollection.GetFragmentBitSet().Contains(*T::StaticStruct());
	}
	
	template<typename T>
	bool HasTag() const
	{
		return TagBitSet.Contains(*T::StaticStruct());
	}

	template<typename T>
	bool HasChunkFragment() const
	{
		return ChunkFragmentBitSet.Contains(*T::StaticStruct());
	}

private:
	FArchetypeHandle Archetype;

	FMassUniqueFragmentCollection FragmentCollection;
	FMassTagBitSet TagBitSet;
	FMassChunkFragmentBitSet ChunkFragmentBitSet;
	
	UPROPERTY()
	FMassRuntimePipeline InitializationPipeline;
	
	UPROPERTY()
	FMassRuntimePipeline DeinitializationPipeline;

	UPROPERTY()
	TArray<FInstancedStruct> ChunkFragments;

	// These functions will be called to initialize entity's UObject-based fragments
	TArray<FObjectFragmentInitializerFunction> ObjectInitializers;

	FMassEntityTemplateID TemplateID;
};

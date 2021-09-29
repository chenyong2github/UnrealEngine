// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstancedStruct.h"
#include "LWComponentTypes.h"
#include "MassEntityTypes.generated.h"

#define WITH_PIPE_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && WITH_AGGREGATETICKING_DEBUG && 1)

MASSENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogPipe, Warning, All);

class UEntitySubsystem;
class UPipeProcessor;
class UPipeSchematic;
class UPipeCompositeProcessor;

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EProcessorExecutionFlags : uint8
{
	None = 0 UMETA(Hidden),
	Standalone = 1 << 0,
	Server = 1 << 1,
	Client = 1 << 2,
	All = Standalone | Server | Client UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EProcessorExecutionFlags);

USTRUCT()
struct FProcessorAuxDataBase
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct MASSENTITY_API FPipeContext
{
	GENERATED_BODY()

	UPROPERTY()
	UEntitySubsystem* EntitySubsystem = nullptr;
	
	UPROPERTY()
	float DeltaSeconds = 0.f;

	UPROPERTY()
	FInstancedStruct AuxData;

	FPipeContext() = default;
	FPipeContext(UEntitySubsystem& InEntities, const float InDeltaSeconds);
};

/** 
 *  A helper type that converts a set of UPipeSchematics into a runtime-usable array of PipeProcessor copies
 */
USTRUCT()
struct MASSENTITY_API FRuntimePipeline
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<UPipeProcessor*> Processors;

	void Reset();
	void Initialize(UObject& Owner);
	
	/** Creates runtime copies of the given UPipeProcessors collection. */
	void SetProcessors(TArray<UPipeProcessor*>&& InProcessors);

	/** Creates runtime copies of UPipeProcessors declared in Schematics using InOwner as new UPipeProcessors' outer. */
	void InitializeFromSchematics(TConstArrayView<TSoftObjectPtr<UPipeSchematic>> Schematics, UObject& InOwner);

	/** Creates runtime copies of UPipeProcessors given in InProcessors input parameter, using InOwner as new UPipeProcessors' outer. */
	void CreateFromArray(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner);

	/** Calls CreateFromArray and calls Initialize on all processors afterwards. */
	void InitializeFromArray(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner);
	
	/** Creates runtime instances of UPipeProcessors for each processor class given via InProcessorClasses. 
	 *  The instances will be created with InOwner as outer. */
	void InitializeFromClassArray(TConstArrayView<TSubclassOf<UPipeProcessor>> InProcessorClasses, UObject& InOwner);

	/** Creates a runtime instance of every processors in the given InProcessors array. If a processor of that class
	 *  already exists in Processors array it gets overridden. Otherwise it gets added to the end of the collection.*/
	void AppendOrOverrideRuntimeProcessorCopies(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner);

	/** Creates a runtime instance of every processors in the given array if there's no processor of that class in Processors already.
	 *  Call this function when adding processors to an already configured FRuntimePipeline instance. If you're creating 
	 *  one from scratch calling any of the InitializeFrom* methods will be more efficient (and will produce same results)
	 *  or call AppendOrOverrideRuntimeProcessorCopies.*/
	void AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner);

	/** Adds InProcessor to Processors without any additional checks */
	void AppendProcessor(UPipeProcessor& Processor);

	/** goes through Processor looking for a UPipeCompositeProcessor instance which GroupName matches the one given as the parameter */
	UPipeCompositeProcessor* FindTopLevelGroupByName(const FName GroupName);

	void DebugOutputDescription(FOutputDevice& Ar) const;

	bool HasProcessorOfExactClass(TSubclassOf<UPipeProcessor> InClass) const;
	bool IsEmpty() const { return Processors.IsEmpty();}

	MASSENTITY_API friend uint32 GetTypeHash(const FRuntimePipeline& Instance);
};

UENUM()
enum class EPipeProcessingPhase : uint8
{
	PrePhysics,
	StartPhysics,
	DuringPhysics,
	EndPhysics,
	PostPhysics,
	FrameEnd,
	MAX,
};

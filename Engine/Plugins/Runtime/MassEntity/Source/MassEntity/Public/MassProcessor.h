// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySubsystem.h"
#include "MassEntityTypes.h"
#include "Async/TaskGraphInterfaces.h"
#include "LWCCommandBuffer.h"
#include "MassProcessorDependencySolver.h"
#include "MassProcessor.generated.h"


struct FPipeProcessingPhaseConfig;

enum class EProcessorCompletionStatus : uint8
{
	Invalid,
	Threaded,
	Postponed,
	Done
};

USTRUCT()
struct FPipeProcessorExecutionOrder
{
	GENERATED_BODY()

	/** Determines which processing group this processor will be placed in. Leaving it empty ("None") means "top-most group for my ProcessingPhase" */
	UPROPERTY(EditAnywhere, Category = Processor, config)
	FName ExecuteInGroup = FName();

	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteBefore;

	UPROPERTY(EditAnywhere, Category = Processor, config)
	TArray<FName> ExecuteAfter;
};


UCLASS(abstract, EditInlineNew, CollapseCategories, config = Game, defaultconfig)
class MASSENTITY_API UPipeProcessor : public UObject
{
	GENERATED_BODY()
public:
	UPipeProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Initialize(UObject& Owner) {}
	virtual FGraphEventRef DispatchProcessorTasks(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray());

	EProcessorExecutionFlags GetExecutionFlags() const { return (EProcessorExecutionFlags)ExecutionFlags; }

	/** Whether this processor should execute according the CurrentExecutionFlags parameters */
	bool ShouldExecute(const EProcessorExecutionFlags CurrentExecutionFlags) const { return (GetExecutionFlags() & CurrentExecutionFlags) != EProcessorExecutionFlags::None; }
	void CallExecute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context);
	
	bool AllowDuplicates() const { return bAllowDuplicates; }

	virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent = 0) const;
	virtual FString GetProcessorName() const { return GetName(); }

	//----------------------------------------------------------------------//
	// Ordering functions 
	//----------------------------------------------------------------------//
	virtual EPipeProcessingPhase GetProcessingPhase() const { return ProcessingPhase; }
	virtual void SetProcessingPhase(EPipeProcessingPhase Phase) { ProcessingPhase = Phase; }
	bool DoesRequireGameThreadExecution() const { return bRequiresGameThreadExecution; }
	
	const FPipeProcessorExecutionOrder& GetExecutionOrder() const { return ExecutionOrder; }

	TConstArrayView<const int32> GetPrerequisiteIndices() const { return DependencyIndices; }

	bool ShouldAutoAddToGlobalList() const { return bAutoRegisterWithProcessingPhases; }
#if WITH_EDITOR
	bool ShouldShowUpInSettings() const { return ShouldAutoAddToGlobalList() || bCanShowUpInSettings; }
#endif // WITH_EDITORONLY_DATA

	/** Sets bAutoRegisterWithProcessingPhases. Setting it to true will result in this processor class being always 
	 * instantiated to be automatically evaluated every frame. @see UPipeProcessingPhaseManager
	 * Note that calling this function is only valid on CDOs. Calling it on a regular instance will fail an ensure and 
	 * have no other effect, i.e. CDO's value won't change */
	void SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister);

#if CPUPROFILERTRACE_ENABLED
	FString StatId;
#endif
	
protected:
	virtual void ConfigureQueries() PURE_VIRTUAL(UPipeProcessor::ConfigureQueries);
	virtual void PostInitProperties() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) PURE_VIRTUAL(UPipeProcessor::Execute);
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

protected:
	/** Whether this processor should be executed on StandAlone or Server or Client */
	UPROPERTY(EditAnywhere, Category = "Pipeline", meta = (Bitmask, BitmaskEnum = EProcessorExecutionFlags), config)
	int32 ExecutionFlags;

	/** Processing phase this processor will be automatically run as part of. */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	EPipeProcessingPhase ProcessingPhase = EPipeProcessingPhase::PrePhysics;

	/** Configures when this given processor can be executed in relation to other processors and processing groups, within its processing phase. */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	FPipeProcessorExecutionOrder ExecutionOrder;

	/** Configures whether this processor should be automatically included in the global list of processors executed every tick (see ProcessingPhase and ExecutionOrder). */
	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	bool bAutoRegisterWithProcessingPhases = true;

	/** meant as a class property, make sure to set it in subclass' constructor. Controls whether there can be multiple 
	 *  instances of a given class in a single FRuntimePipeline */
	bool bAllowDuplicates = false;

	UPROPERTY(EditDefaultsOnly, Category = Processor, config)
	bool bRequiresGameThreadExecution = false;

#if WITH_EDITORONLY_DATA
	/** Used to permanently remove a given processor class from PipeSetting's listing. Used primarily for test-time 
	 *  processor classes, but can also be used by project-specific code to prune the processor list. */
	UPROPERTY(config)
	bool bCanShowUpInSettings = true;
#endif // WITH_EDITORONLY_DATA

	friend class UPipeCompositeProcessor;
	TArray<int32> DependencyIndices;
	TArray<int32> TransientDependencyIndices;
};


UCLASS()
class MASSENTITY_API UPipeCompositeProcessor : public UPipeProcessor
{
	GENERATED_BODY()

	struct FDependencyNode
	{
		FName Name;
		UPipeProcessor* Processor = nullptr;
		TArray<int32> Dependencies;
	};

public:
	UPipeCompositeProcessor();

	void SetChildProcessors(TArray<UPipeProcessor*>&& InProcessors);

	virtual void Initialize(UObject& Owner) override;
	virtual void DebugOutputDescription(FOutputDevice& Ar, int32 Indent = 0) const override;
	virtual void SetProcessingPhase(EPipeProcessingPhase Phase) override;

	void SetGroupName(FName NewName);
	FName GetGroupName() const { return GroupName; }

	virtual void CopyAndSort(const FPipeProcessingPhaseConfig& PhaseConfig, const FString& DependencyGraphFileName = FString());

	/** adds SubProcessor to an appropriately named group. If RequestedGroupName == None then SubProcessor
	 *  will be added directly to ChildPipeline. If not then the indicated group will be searched for in ChildPipeline 
	 *  and if it's missing it will be created and AddGroupedProcessor will be called recursively */
	void AddGroupedProcessor(FName RequestedGroupName, UPipeProcessor& SubProcessor);

	virtual FGraphEventRef DispatchProcessorTasks(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites = FGraphEventArray()) override;

	bool IsEmpty() const { return ChildPipeline.IsEmpty(); }

	virtual FString GetProcessorName() const override { return GroupName.ToString(); }

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	/**
	 *  Called recursively to add processors and composite processors to ChildPipeline based on ProcessorsAndGroups
	 */
	int32 Populate(TArray<FProcessorDependencySolver::FOrderInfo>& ProcessorsAndGroups, const int32 StartIndex = 0);

	/** RequestedGroupName can indicate a multi-level group name, like so: A.B.C
	 *  We need to extract the highest-level group name ('A' in the example), and see if it already exists. 
	 *  If not, create it. 
	 *  @param RequestedGroupName name of the group for which we want to find or create the processor.
	 *  @param OutRemainingGroupName contains the group name after cutting the high-level group. In the used example it
	 *    will contain "B.C". This value is then used to recursively create subgroups */
	UPipeCompositeProcessor* FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName = nullptr);

protected:
	UPROPERTY(VisibleAnywhere, Category=Pipe)
	FRuntimePipeline ChildPipeline;

	/** Group name that will be used when resolving processor dependencies and grouping */
	UPROPERTY()
	FName GroupName;

	TArray<FDependencyNode> ProcessingFlatGraph;

	struct FProcessorCompletion
	{
		FGraphEventRef CompletionEvent;
		EProcessorCompletionStatus Status = EProcessorCompletionStatus::Invalid;

		bool IsDone() const 
		{
			return Status == EProcessorCompletionStatus::Done || (CompletionEvent.IsValid() && CompletionEvent->IsComplete());
		}

		void Wait()
		{
			if (CompletionEvent.IsValid())
			{
				CompletionEvent->Wait();
			}
		}
	};
	TArray<FProcessorCompletion> CompletionStatus;

	bool bRunInSeparateThread;
	bool bHasOffThreadSubGroups;
};

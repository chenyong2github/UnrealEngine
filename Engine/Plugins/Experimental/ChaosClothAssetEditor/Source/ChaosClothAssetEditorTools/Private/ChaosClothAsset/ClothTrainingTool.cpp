// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/CacheCollection.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothDataGenerationComponent.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "ClothingSystemRuntimeTypes.h"
#include "ComponentReregisterContext.h"
#include "ContextObjectStore.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "InteractiveToolManager.h"
#include "Internationalization/Regex.h"
#include "ModelingOperators.h"
#include "Misc/AsyncTaskNotification.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SkeletalRenderPublic.h"
#include "Tasks/Pipe.h"
#include "ToolTargetManager.h"
#include "UObject/SavePackage.h"

#include <atomic>

#define LOCTEXT_NAMESPACE "ClothTrainingTool"
DEFINE_LOG_CATEGORY_STATIC(LogClothTrainingTool, Log, All);

namespace UE::ClothTrainingTool::Private
{
	UChaosCache& GetCache(UChaosCacheCollection& CacheCollection)
	{
		static const FName CacheName = FName("SimulatedCache");
		return *CacheCollection.FindOrAddCache(CacheName);
	}

	TArray<int32> ParseFrames(const FString& FramesString)
	{
		TArray<int32> Result;
		static const FRegexPattern AllowedCharsPattern(TEXT("^[-,0-9\\s]+$"));

		if (!FRegexMatcher(AllowedCharsPattern, FramesString).FindNext())
		{
		    UE_LOG(LogClothTrainingTool, Error, TEXT("Input contains invalid characters."));
		    return Result;
		}

		static const FRegexPattern SingleNumberPattern(TEXT("^\\s*(\\d+)\\s*$"));
		static const FRegexPattern RangePattern(TEXT("^\\s*(\\d+)\\s*-\\s*(\\d+)\\s*$"));

		TArray<FString> Segments;
	    FramesString.ParseIntoArray(Segments, TEXT(","), true);
	    for (const FString& Segment : Segments)
	    {
	    	bool bSegmentValid = false;

	    	FRegexMatcher SingleNumberMatcher(SingleNumberPattern, Segment);
	    	if (SingleNumberMatcher.FindNext())
	    	{
	    	    const int32 SingleNumber = FCString::Atoi(*SingleNumberMatcher.GetCaptureGroup(1));
	    	    Result.Add(SingleNumber);
	    	    bSegmentValid = true;
	    	}
	    	else
	    	{
	    		FRegexMatcher RangeMatcher(RangePattern, Segment);
	    		if (RangeMatcher.FindNext())
	    		{
	    		    const int32 RangeStart = FCString::Atoi(*RangeMatcher.GetCaptureGroup(1));
	    		    const int32 RangeEnd = FCString::Atoi(*RangeMatcher.GetCaptureGroup(2));

	    		    for (int32 i = RangeStart; i <= RangeEnd; ++i)
	    		    {
	    		        Result.Add(i);
	    		    }
	    		    bSegmentValid = true;
	    		}
	    	}
	    	
	    	if (!bSegmentValid)
	    	{
	    	    UE_LOG(LogClothTrainingTool, Error, TEXT("Invalid format in segment: %s"), *Segment);
	    	}
	    }

		return Result;
	}

	TArray<int32> Range(int32 End)
	{
		TArray<int32> Result;
		Result.Reserve(End);
		for (int32 Index = 0; Index < End; ++Index)
		{
			Result.Add(Index);
		}
		return Result;
	}

	class FTimeScope
	{
	public:
		explicit FTimeScope(FString InName)
			: Name(MoveTemp(InName))
			, StartTime(FDateTime::UtcNow())
		{
		}
		~FTimeScope()
		{
			const FTimespan Duration = FDateTime::UtcNow() - StartTime;
			UE_LOG(LogClothTrainingTool, Log, TEXT("%s took %f secs"), *Name, Duration.GetTotalSeconds());
		}
	private:
		FString Name;
		FDateTime StartTime;
	};
};

struct UClothTrainingTool::FSimResource
{
	UClothDataGenerationComponent* ClothComponent = nullptr;
	TSharedPtr<FProxy> Proxy;
	TUniquePtr<UE::Tasks::FPipe> Pipe;
	FEvent* SkinEvent = nullptr;
	std::atomic<bool> bNeedsSkin = false;
};

class UClothTrainingTool::FLaunchSimsOp : public UE::Geometry::TGenericDataOperator<FSkinnedMeshVertices>
{
public: 
	FLaunchSimsOp(TArray<FSimResource>& InSimResources, FCriticalSection& InSimMutex, TObjectPtr<UClothTrainingToolProperties> InToolProperties)
		: SimResources(InSimResources)
		, SimMutex(InSimMutex)
		, ToolProperties(InToolProperties)
	{
	}

	virtual void CalculateResult(FProgressCancel* Progress) override;

private:
	using FPipe = UE::Tasks::FPipe;

	enum class ESaveType
	{
		LastStep,
		EveryStep,
	};

	void Simulate(FSimResource &SimResource, int32 AnimFrame, int32 CacheFrame, UChaosCache& Cache, FProgressCancel* Progress, float ProgressStep);
	void PrepareAnimationSequence();
	void RestoreAnimationSequence();
	TArray<FTransform> GetBoneTransforms(UChaosClothComponent& InClothComponent, int32 Frame) const;
	bool GetSimPositions(FProxy& DataGenerationProxy, TArray<FVector3f>& OutPositions) const;
	void GetRenderPositions(FSimResource& SimResource, TArray<FVector3f>& OutPositions) const;
	void AddToCache(FSimResource& SimResource, UChaosCache& Cache, int32 Frame) const;

	TArray<FSimResource>& SimResources;
	FCriticalSection& SimMutex;
	TObjectPtr<UClothTrainingToolProperties> ToolProperties = nullptr;
	EAnimInterpolationType InterpolationTypeBackup = EAnimInterpolationType::Linear;

	inline static const FName PositionXName = TEXT("PositionX");
	inline static const FName PositionYName = TEXT("PositionY");
	inline static const FName PositionZName = TEXT("PositionZ");
};

struct UClothTrainingTool::FTaskResource
{
	TUniquePtr<FCriticalSection> SimMutex;
	TArray<FSimResource> SimResources;

	TUniquePtr<FExecuterType> Executer;
	TUniquePtr<FAsyncTaskNotification> Notification;
	UChaosCache *Cache = nullptr;
	TUniquePtr<FCacheUserToken> CacheUserToken;
	FDateTime StartTime;
	FDateTime LastUpdateTime;

	bool AllocateSimResources_GameThread(const UChaosClothComponent& InClothComponent, int32 Num);
	void FreeSimResources_GameThread();
	void FlushRendering();
};

void UClothTrainingTool::FLaunchSimsOp::PrepareAnimationSequence()
{
	TObjectPtr<UAnimSequence> AnimationSequence = ToolProperties->AnimationSequence;
	if (AnimationSequence)
	{
		InterpolationTypeBackup = AnimationSequence->Interpolation;
		AnimationSequence->Interpolation = EAnimInterpolationType::Step;
	}
}

void UClothTrainingTool::FLaunchSimsOp::RestoreAnimationSequence()
{
	TObjectPtr<UAnimSequence> AnimationSequence = ToolProperties->AnimationSequence;
	if (AnimationSequence)
	{
		AnimationSequence->Interpolation = InterpolationTypeBackup;
	}
}

TArray<FTransform> UClothTrainingTool::FLaunchSimsOp::GetBoneTransforms(UChaosClothComponent& InClothComponent, int32 Frame) const
{
	const UAnimSequence* AnimationSequence = ToolProperties->AnimationSequence;
	const double Time = AnimationSequence->GetTimeAtFrame(Frame);
	FAnimExtractContext ExtractionContext(Time);

	UChaosClothAsset* const ClothAsset = InClothComponent.GetClothAsset();
	const FReferenceSkeleton* const ReferenceSkeleton = ClothAsset ? &ClothAsset->GetRefSkeleton() : nullptr;
	USkeleton* const Skeleton = ClothAsset ? ClothAsset->GetSkeleton() : nullptr;
	const int32 NumBones = ReferenceSkeleton ? ReferenceSkeleton->GetNum() : 0;

	TArray<uint16> BoneIndices;
	BoneIndices.SetNumUninitialized(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		BoneIndices[Index] = (uint16)Index;
	}

	FBoneContainer BoneContainer;
	BoneContainer.SetUseRAWData(true);
	BoneContainer.InitializeTo(BoneIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);

	FCompactPose OutPose;
	OutPose.SetBoneContainer(&BoneContainer);
	FBlendedCurve OutCurve;
	OutCurve.InitFrom(BoneContainer);
	UE::Anim::FStackAttributeContainer TempAttributes;

	FAnimationPoseData AnimationPoseData(OutPose, OutCurve, TempAttributes);
	AnimationSequence->GetAnimationPose(AnimationPoseData, ExtractionContext);

	TArray<FTransform> ComponentSpaceTransforms;
	ComponentSpaceTransforms.SetNumUninitialized(NumBones);
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
		const int32 ParentIndex = ReferenceSkeleton->GetParentIndex(Index);
		ComponentSpaceTransforms[Index] = 
			ComponentSpaceTransforms.IsValidIndex(ParentIndex) && ParentIndex < Index ? 
			AnimationPoseData.GetPose()[CompactIndex] * ComponentSpaceTransforms[ParentIndex] : 
			ReferenceSkeleton->GetRefBonePose()[Index];
	}

	return ComponentSpaceTransforms;
}

void UClothTrainingTool::FLaunchSimsOp::AddToCache(FSimResource& SimResource, UChaosCache& OutCache, int32 Frame) const
{
	TArray<FVector3f> Positions;
	GetRenderPositions(SimResource, Positions);

	constexpr float CacheFPS = 30;
	const float Time = Frame / CacheFPS;
	FPendingFrameWrite NewFrame;
	NewFrame.Time = Time;

	const int32 NumParticles = Positions.Num();
	TArray<int32>& PendingID = NewFrame.PendingChannelsIndices;
	TArray<float> PendingPX, PendingPY, PendingPZ;
	TArray<float> PendingVX, PendingVY, PendingVZ;
	PendingID.SetNum(NumParticles);
	PendingPX.SetNum(NumParticles);
	PendingPY.SetNum(NumParticles);
	PendingPZ.SetNum(NumParticles);

	for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
	{
		const FVector3f& Position = Positions[ParticleIndex];
		PendingID[ParticleIndex] = ParticleIndex;
		PendingPX[ParticleIndex] = Position.X;
		PendingPY[ParticleIndex] = Position.Y;
		PendingPZ[ParticleIndex] = Position.Z;
	}

	NewFrame.PendingChannelsData.Add(PositionXName, PendingPX);
	NewFrame.PendingChannelsData.Add(PositionYName, PendingPY);
	NewFrame.PendingChannelsData.Add(PositionZName, PendingPZ);

	OutCache.AddFrame_Concurrent(MoveTemp(NewFrame));
}

void UClothTrainingTool::FLaunchSimsOp::CalculateResult(FProgressCancel* Progress)
{
	using UE::ClothTrainingTool::Private::ParseFrames;
	using UE::ClothTrainingTool::Private::Range;
	using UE::ClothTrainingTool::Private::GetCache;

	const TArray<int32> FramesToSimulate = ToolProperties->FramesToSimulate.Len() > 0
		? ParseFrames(ToolProperties->FramesToSimulate) 
		: Range(ToolProperties->AnimationSequence->GetNumberOfSampledKeys());

	const int32 NumFrames = ToolProperties->bDebug ? 1 : FramesToSimulate.Num();
	if (NumFrames == 0)
	{
		return;
	}
	const float ProgressStep = 1.f / NumFrames;

	UChaosCache& Cache = ToolProperties->bDebug ? GetCache(*ToolProperties->DebugCacheCollection) : GetCache(*ToolProperties->CacheCollection);
	PrepareAnimationSequence();

	const int32 NumThreads = ToolProperties->bDebug ? 1 : ToolProperties->NumThreads;
	FScopeLock Lock(&SimMutex);

	bool bCancelled = false;
	for (int32 Frame = 0; Frame < NumFrames; Frame++)
	{
		if (Progress)
		{
			if (Progress->Cancelled())
			{
				bCancelled = true;
				break;
			}

			const int32 ThreadIdx = Frame % NumThreads;
			const int32 AnimFrame = FramesToSimulate[Frame];

			FSimResource& SimResource = SimResources[ThreadIdx];
			SimResource.Pipe->Launch(*FString::Printf(TEXT("SimFrame:%d"), AnimFrame), [this, &SimResource, AnimFrame, Frame, &Cache, Progress, ProgressStep]()
			{ 
				FMemMark Mark(FMemStack::Get());
				Simulate(SimResource, AnimFrame, Frame, Cache, Progress, ProgressStep);
			});
		}
	}

	for (FSimResource& SimResource : SimResources)
	{
		SimResource.Pipe->WaitUntilEmpty();
	}

	RestoreAnimationSequence();
	Cache.FlushPendingFrames();
}

void UClothTrainingTool::FLaunchSimsOp::Simulate(FSimResource &SimResource, int32 AnimFrame, int32 CacheFrame, UChaosCache& Cache, FProgressCancel* Progress, float ProgressStep)
{
	UClothDataGenerationComponent& TaskComponent = *SimResource.ClothComponent;
	FProxy& DataGenerationProxy = *SimResource.Proxy;

	const float TimeStep = ToolProperties->TimeStep;
	const int32 NumSteps = ToolProperties->NumSteps;
	const ESaveType SaveType = ToolProperties->bDebug ? ESaveType::EveryStep : ESaveType::LastStep;

	const TArray<FTransform> Transforms = GetBoneTransforms(TaskComponent, AnimFrame);
	TaskComponent.Pose(Transforms);
	TaskComponent.ForceNextUpdateTeleportAndReset();
	DataGenerationProxy.FillSimulationContext(TimeStep);
	DataGenerationProxy.InitializeConfigs();
	bool bCancelled = false;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		if (Progress)
		{
			if (Progress->Cancelled())
			{
				bCancelled = true;
				break;
			}

			DataGenerationProxy.Tick();

			// Clear any reset flags at the end of the first step
			if (Step == 0 && NumSteps > 1)
			{
				TaskComponent.ResetTeleportMode();
				DataGenerationProxy.FillSimulationContext(TimeStep);	
			}

			if (SaveType == ESaveType::EveryStep)
			{
				DataGenerationProxy.WriteSimulationData();
				AddToCache(SimResource, Cache, Step);
			}
		}
	}

	if (SaveType == ESaveType::LastStep && !bCancelled)
	{
		DataGenerationProxy.WriteSimulationData();
		AddToCache(SimResource, Cache, CacheFrame);
	}

	Progress->AdvanceCurrentScopeProgressBy(ProgressStep);
}

bool UClothTrainingTool::FLaunchSimsOp::GetSimPositions(FProxy& DataGenerationProxy, TArray<FVector3f> &OutPositions) const
{
	const TMap<int32, FClothSimulData> &SimulDataMap = DataGenerationProxy.GetCurrentSimulationData_AnyThread();
	const FClothSimulData* const SimulData = SimulDataMap.Find(0);
	if (SimulDataMap.Num() > 1)
	{
		ensureMsgf(false, TEXT("Multiple cloth is not yet supported."));
		return false;
	}
	if (SimulData == nullptr)
	{
		ensureMsgf(false, TEXT("ClothSimulData is nullptr"));
		return false;
	}

	const TArray<FVector3f>& SimPositions = SimulData->Positions;
	OutPositions.SetNum(SimPositions.Num());
	for (int32 Index = 0; Index < SimPositions.Num(); ++Index)
	{
		OutPositions[Index] = FVector3f(SimulData->ComponentRelativeTransform.TransformPosition(FVector(SimPositions[Index])));
	}
	return true;
}

void UClothTrainingTool::FLaunchSimsOp::GetRenderPositions(FSimResource& SimResource, TArray<FVector3f> &OutPositions) const
{
	check(SimResource.ClothComponent);
	TArray<FFinalSkinVertex> OutVertices;
	// This could potentially be slow. 
	SimResource.ClothComponent->RecreateRenderState_Concurrent();
	SimResource.bNeedsSkin.store(true);
	SimResource.SkinEvent->Wait();
	
	SimResource.ClothComponent->GetCPUSkinnedCachedFinalVertices(OutVertices);
	OutPositions.SetNum(OutVertices.Num());
	for (int32 Index = 0; Index < OutVertices.Num(); ++Index)
	{
		OutPositions[Index] = OutVertices[Index].Position;
	}
}

bool UClothTrainingTool::IsClothComponentValid() const
{
	if (ClothComponent == nullptr)
	{
		return false;
	}
	if (USkinnedMeshComponent* const LeaderComponent = ClothComponent->LeaderPoseComponent.Get())
	{
		UE_LOG(LogClothTrainingTool, Error, TEXT("Leader pose component is not supported yet."));
		return false;
	}
	else
	{
		return true;
	}
}

// ------------------- Properties -------------------

void UClothTrainingToolActionProperties::PostAction(EClothTrainingToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}

// ------------------- Builder -------------------

const FToolTargetTypeRequirements& UClothTrainingToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({ 
		UPrimitiveComponentBackedTarget::StaticClass(),
		UClothAssetBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UClothTrainingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1);
}

UInteractiveTool* UClothTrainingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UClothTrainingTool* NewTool = NewObject<UClothTrainingTool>();
	
	UToolTarget* Target = SceneState.TargetManager->BuildFirstSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTarget(Target);

	return NewTool;
}

// ------------------- Tool -------------------
UClothTrainingTool::UClothTrainingTool() = default;

UClothTrainingTool::UClothTrainingTool(FVTableHelper& Helper)
	: Super(Helper)
{
}

UClothTrainingTool::~UClothTrainingTool() = default;


void UClothTrainingTool::Setup()
{
	UInteractiveTool::Setup();

	if (const UClothComponentToolTarget* const ClothComponentTarget = Cast<const UClothComponentToolTarget>(Target))
	{
		const UChaosClothComponent* const TargetClothComponent = ClothComponentTarget->GetClothComponent();
		ClothComponent = TargetClothComponent;
	}

	ToolProperties = NewObject<UClothTrainingToolProperties>(this);
	AddToolPropertySource(ToolProperties);
	ToolProperties->RestoreProperties(this);

	ActionProperties = NewObject<UClothTrainingToolActionProperties>(this);
	ActionProperties->ParentTool = this;
	AddToolPropertySource(ActionProperties);
}

void UClothTrainingTool::StartTraining()
{
	check(PendingAction == EClothTrainingToolActions::StartTrain);
	if (!IsClothComponentValid() || ToolProperties->AnimationSequence == nullptr)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	UChaosCacheCollection* const CacheCollection = GetCacheCollection();
	if (CacheCollection == nullptr)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	if (TaskResource != nullptr)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	TaskResource = MakeUnique<FTaskResource>();
	if (!TaskResource->AllocateSimResources_GameThread(*ClothComponent, ToolProperties->NumThreads))
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}

	using UE::ClothTrainingTool::Private::GetCache;
	UChaosCache* const Cache = &GetCache(*CacheCollection);
	TaskResource->Cache = Cache;
	TaskResource->CacheUserToken = MakeUnique<FCacheUserToken>(Cache->BeginRecord(ClothComponent, FGuid(), FTransform::Identity));

	TUniquePtr<FLaunchSimsOp> NewOp = MakeUnique<FLaunchSimsOp>(TaskResource->SimResources, *TaskResource->SimMutex, ToolProperties);
	TaskResource->Executer = MakeUnique<FExecuterType>(MoveTemp(NewOp));
	TaskResource->Executer->StartBackgroundTask();

	FAsyncTaskNotificationConfig NotificationConfig;
	NotificationConfig.TitleText = LOCTEXT("SimulateCloth", "Simulating Cloth");
	NotificationConfig.ProgressText = FText::FromString(TEXT("0%"));
	NotificationConfig.bCanCancel = true;
	NotificationConfig.bKeepOpenOnSuccess = true;
	NotificationConfig.bKeepOpenOnFailure = true;
	TaskResource->Notification = MakeUnique<FAsyncTaskNotification>(NotificationConfig);
	TaskResource->StartTime = FDateTime::UtcNow();
	TaskResource->LastUpdateTime = TaskResource->StartTime;

	PendingAction = EClothTrainingToolActions::TickTrain;
}

void UClothTrainingTool::TickTraining()
{
	check(PendingAction == EClothTrainingToolActions::TickTrain && TaskResource != nullptr);

	bool bFinished = false;
	const bool bCancelled = TaskResource->Notification->GetPromptAction() == EAsyncTaskNotificationPromptAction::Cancel;
	if (bCancelled)
	{
		TaskResource->Executer.Release()->CancelAndDelete();
		bFinished = true;
	}
	else if (TaskResource->Executer->IsDone())
	{
		bFinished = true;
	}

	if (!bFinished)
	{
		TaskResource->FlushRendering();
		const FDateTime CurrentTime = FDateTime::UtcNow();
		const double SinceLastUpdate = (CurrentTime - TaskResource->LastUpdateTime).GetTotalSeconds();
		if (SinceLastUpdate < 0.2)
		{
			return;
		}

		float ProgressFrac;
		FText ProgressMessage;
		const bool bMadeProgress = TaskResource->Executer->PollProgress(ProgressFrac, ProgressMessage);
		if (bMadeProgress)
		{
			ProgressMessage = FText::FromString(FString::Printf(TEXT("%d%%"), int32(ProgressFrac * 100)));
			TaskResource->Notification->SetProgressText(ProgressMessage);
		}
		TaskResource->LastUpdateTime = CurrentTime;
	}
	else
	{
		FreeTaskResource(bCancelled);
		PendingAction = EClothTrainingToolActions::NoAction;
	}
}

void UClothTrainingTool::OnTick(float DeltaTime)
{
	if (PendingAction == EClothTrainingToolActions::StartTrain)
	{
		StartTraining();
	}
	else if (PendingAction == EClothTrainingToolActions::TickTrain)
	{
		TickTraining();
	}
}

void UClothTrainingTool::RequestAction(EClothTrainingToolActions ActionType)
{
	if (PendingAction != EClothTrainingToolActions::NoAction)
	{
		return;
	}
	PendingAction = ActionType;
}

void UClothTrainingTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (TaskResource != nullptr)
	{
		if (TaskResource->Executer != nullptr)
		{
			TaskResource->Executer.Release()->CancelAndDelete();
		}
		constexpr bool bCancelled = true;
		FreeTaskResource(bCancelled);
	}
	Super::Shutdown(ShutdownType);
	ToolProperties->SaveProperties(this);
}

bool UClothTrainingTool::FTaskResource::AllocateSimResources_GameThread(const UChaosClothComponent& InClothComponent, int32 Num)
{
	SimResources.SetNumUninitialized(Num);
	for(int32 Index = 0; Index < Num; ++Index)
	{
		UClothDataGenerationComponent* const CopyComponent = NewObject<UClothDataGenerationComponent>(InClothComponent.GetOuter());
		CopyComponent->SetClothAsset(InClothComponent.GetClothAsset());
		CopyComponent->RegisterComponent();
		CopyComponent->SetWorldTransform(InClothComponent.GetComponentTransform());

		USkinnedMeshComponent* const PoseComponent = CopyComponent->LeaderPoseComponent.Get() ? CopyComponent->LeaderPoseComponent.Get() : CopyComponent;
		constexpr int32 LODIndex = 0;
		PoseComponent->SetForcedLOD(LODIndex + 1);
		PoseComponent->UpdateLODStatus();
		PoseComponent->RefreshBoneTransforms(nullptr);
		CopyComponent->bRenderStatic = false;
		constexpr bool bRecreateRenderStateImmediately = true;
		CopyComponent->SetCPUSkinningEnabled(true, bRecreateRenderStateImmediately);
		CopyComponent->ResumeSimulation();

		FSimResource& SimResource = SimResources[Index];
		SimResource.ClothComponent = CopyComponent;
		SimResource.Proxy = CopyComponent->GetProxy().Pin();
		check(SimResource.Proxy != nullptr);
		SimResource.Pipe = MakeUnique<UE::Tasks::FPipe>(*FString::Printf(TEXT("SimPipe:%d"), Index));
		SimResource.SkinEvent = FPlatformProcess::GetSynchEventFromPool();
		SimResource.bNeedsSkin.store(false);

		if (CopyComponent == nullptr || SimResource.Proxy == nullptr || SimResource.Pipe == nullptr)
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Failed to allocate simulation resources"));
			return false;
		}
	}
	SimMutex = MakeUnique<FCriticalSection>();
	return true;
}

void UClothTrainingTool::FTaskResource::FreeSimResources_GameThread()
{
	while (!SimMutex->TryLock())
	{
		FlushRendering();
		FPlatformProcess::Sleep(0.1f);
	}
	for (FSimResource& SimResource : SimResources)
	{
		FPlatformProcess::ReturnSynchEventToPool(SimResource.SkinEvent);
		SimResource.Pipe.Reset();
		SimResource.ClothComponent->UnregisterComponent();
		SimResource.ClothComponent->DestroyComponent();
	}
	SimResources.Empty();
	SimMutex->Unlock();
	SimMutex.Reset();
}

void UClothTrainingTool::FTaskResource::FlushRendering()
{
	// Copy bNeedsSkin
	TArray<bool> NeedsSkin;
	NeedsSkin.SetNum(SimResources.Num());
	bool bAnyNeedsSkin = false;
	for (int32 Index = 0; Index < SimResources.Num(); ++Index)
	{
		const bool bNeedsSkin = SimResources[Index].bNeedsSkin.load();
		bAnyNeedsSkin |= bNeedsSkin;
		NeedsSkin[Index] = bNeedsSkin;
	}

	if (bAnyNeedsSkin)
	{
		FlushRenderingCommands();
		for (int32 Index = 0; Index < SimResources.Num(); ++Index)
		{
			if (NeedsSkin[Index])
			{
				SimResources[Index].bNeedsSkin.store(false);
				SimResources[Index].SkinEvent->Trigger();
			}
		}
	}

}

UChaosCacheCollection* UClothTrainingTool::GetCacheCollection() const
{
	UChaosCacheCollection* CacheCollection = nullptr;
	if (ToolProperties->bDebug)
	{
		CacheCollection = ToolProperties->DebugCacheCollection;
		if (CacheCollection == nullptr)
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Debug cache is None. Please select a valid cache for output."));
		}
	}
	else
	{
		CacheCollection = ToolProperties->CacheCollection;
		if (CacheCollection == nullptr)
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Generated Cache is None. Please select a valid cache for output."));
		}
	}
	return CacheCollection;
}

bool UClothTrainingTool::SaveCacheCollection(UChaosCacheCollection* CacheCollection) const
{
	if (CacheCollection == nullptr)
	{
		return false;
	}
	UPackage* const Package = CacheCollection->GetPackage();
	if (Package == nullptr)
	{
		UE_LOG(LogClothTrainingTool, Error, TEXT("Failed to get package for %s"), *(CacheCollection->GetFName().ToString()));
		return false;
	}
	const FString SavePath = Package->GetFName().ToString();
	UE_LOG(LogClothTrainingTool, Display, TEXT("Save to %s"), *SavePath);

	FSavePackageArgs SaveArgs;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaveSucced = UPackage::SavePackage(Package, CacheCollection, *SavePath, SaveArgs);
	if (!bSaveSucced)
	{
		UE_LOG(LogClothTrainingTool, Error, TEXT("Failed to save cache collection"));
	}
	return bSaveSucced;
}

void UClothTrainingTool::FreeTaskResource(bool bCancelled)
{
	TaskResource->Notification->SetProgressText(LOCTEXT("Finishing", "Finishing, please wait"));
	TaskResource->FreeSimResources_GameThread();
	const FDateTime CurrentTime = FDateTime::UtcNow();
	UE_LOG(LogClothTrainingTool, Log, TEXT("Training finished in %f seconds"), (CurrentTime - TaskResource->StartTime).GetTotalSeconds());

	{
		UE::ClothTrainingTool::Private::FTimeScope TimeScope(TEXT("Saving"));
		TaskResource->Cache->bCompressChannels = true;
		TaskResource->Cache->EndRecord(*TaskResource->CacheUserToken);

		UChaosCacheCollection* const CacheCollection = GetCacheCollection();
		ensureMsgf(CacheCollection != nullptr, TEXT("CacheCollection should not be nullptr"));
		SaveCacheCollection(CacheCollection);
	}
	if (bCancelled)
	{
		TaskResource->Notification->SetProgressText(LOCTEXT("Cancelled", "Cancelled"));
		TaskResource->Notification->SetComplete(false);
	}
	else
	{
		TaskResource->Notification->SetProgressText(LOCTEXT("Finished", "Finished"));
		TaskResource->Notification->SetComplete(true);
	}
	TaskResource.Reset();
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "CoreMinimal.h"

#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Chaos/ChaosCache.h"
#include "Chaos/CacheCollection.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothAsset.h"
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
#include "Misc/DateTime.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Tasks/Pipe.h"
#include "ToolTargetManager.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "ClothTrainingTool"
DEFINE_LOG_CATEGORY_STATIC(LogClothTrainingTool, Log, All);

class UClothTrainingTool::FClothSimulationDataGenerationProxy : public UE::Chaos::ClothAsset::FClothSimulationProxy
{
public:
	explicit FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent);
	~FClothSimulationDataGenerationProxy();

	using UE::Chaos::ClothAsset::FClothSimulationProxy::Tick;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::FillSimulationContext;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::InitializeConfigs;
	using UE::Chaos::ClothAsset::FClothSimulationProxy::WriteSimulationData;
};

UClothTrainingTool::FClothSimulationDataGenerationProxy::FClothSimulationDataGenerationProxy(const UChaosClothComponent& InClothComponent)
	: FClothSimulationProxy(InClothComponent)
{	
}

UClothTrainingTool::FClothSimulationDataGenerationProxy::~FClothSimulationDataGenerationProxy()
{
}

namespace UE::ClothTrainingTool::Private
{
	UChaosCache* GetCache(UChaosCacheCollection* CacheCollection)
	{
		static const FName CacheName = FName("SimulatedCache");
		return CacheCollection ? CacheCollection->FindOrAddCache(CacheName) : nullptr;
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
	UChaosClothComponent* ClothComponent = nullptr;
	TUniquePtr<FClothSimulationDataGenerationProxy> Proxy = nullptr;
	TUniquePtr<UE::Tasks::FPipe> Pipe = nullptr;
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
	using FProxy = UClothTrainingTool::FClothSimulationDataGenerationProxy;

	enum class ESaveType
	{
		LastStep,
		EveryStep,
	};

	void Simulate(FSimResource &SimResource, int32 AnimFrame, int32 CacheFrame, UChaosCache& Cache, FProgressCancel* Progress, float ProgressStep);
	void PrepareAnimationSequence();
	void RestoreAnimationSequence();
	TArray<FTransform> GetBoneTransforms(UChaosClothComponent& ClothComponent, int32 Frame) const;
	bool GetSimPositions(FProxy& DataGenerationProxy, TArray<FVector3f>& OutPositions) const;
	void AddToCache(FProxy& DataGenerationProxy, UChaosCache& Cache, int32 Frame) const;

	TArray<FSimResource>& SimResources;
	FCriticalSection& SimMutex;
	TObjectPtr<UClothTrainingToolProperties> ToolProperties = nullptr;
	EAnimInterpolationType InterpolationTypeBackup = EAnimInterpolationType::Linear;

	inline static const FName PositionXName = TEXT("PositionX");
	inline static const FName PositionYName = TEXT("PositionY");
	inline static const FName PositionZName = TEXT("PositionZ");
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

TArray<FTransform> UClothTrainingTool::FLaunchSimsOp::GetBoneTransforms(UChaosClothComponent& ClothComponent, int32 Frame) const
{
	const UAnimSequence* AnimationSequence = ToolProperties->AnimationSequence;
	const double Time = AnimationSequence->GetTimeAtFrame(Frame);
	FAnimExtractContext ExtractionContext(Time);

	UChaosClothAsset* const ClothAsset = ClothComponent.GetClothAsset();
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

void UClothTrainingTool::FLaunchSimsOp::AddToCache(FProxy& DataGenerationProxy, UChaosCache& OutCache, int32 Frame) const
{
	TArray<FVector3f> SimPositions;
	if (!GetSimPositions(DataGenerationProxy, SimPositions))
	{
		return;
	}

	constexpr float CacheFPS = 30;
	const float Time = Frame / CacheFPS;
	FPendingFrameWrite NewFrame;
	NewFrame.Time = Time;

	const int32 NumParticles = SimPositions.Num();
	TArray<int32>& PendingID = NewFrame.PendingChannelsIndices;
	TArray<float> PendingPX, PendingPY, PendingPZ;
	TArray<float> PendingVX, PendingVY, PendingVZ;
	PendingID.SetNum(NumParticles);
	PendingPX.SetNum(NumParticles);
	PendingPY.SetNum(NumParticles);
	PendingPZ.SetNum(NumParticles);

	for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
	{
		const FVector3f& Position = SimPositions[ParticleIndex];
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

	UChaosCache& Cache = ToolProperties->bDebug ? *GetCache(ToolProperties->DebugCacheCollection) : *GetCache(ToolProperties->CacheCollection);
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
	UChaosClothComponent& TaskComponent = *SimResource.ClothComponent;
	FProxy& DataGenerationProxy = *SimResource.Proxy.Get();

	const float TimeStep = ToolProperties->TimeStep;
	const int32 NumSteps = ToolProperties->NumSteps;
	const ESaveType SaveType = ToolProperties->bDebug ? ESaveType::EveryStep : ESaveType::LastStep;

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
				AddToCache(DataGenerationProxy, Cache, Step);
			}
		}
	}

	if (SaveType == ESaveType::LastStep && !bCancelled)
	{
		DataGenerationProxy.WriteSimulationData();
		AddToCache(DataGenerationProxy, Cache, CacheFrame);
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

void UClothTrainingTool::RunTraining()
{
	if (!IsClothComponentValid() || ToolProperties->AnimationSequence == nullptr)
	{
		return;
	}
	UChaosCacheCollection* const CacheCollection = GetCacheCollection();
	if (CacheCollection == nullptr)
	{
		return;
	}
	using UE::ClothTrainingTool::Private::GetCache;
	UChaosCache* const Cache = GetCache(CacheCollection);
	if (Cache == nullptr)
	{
		return;
	}
	
	using FTaskType = UE::Geometry::TModelingOpTask<FLaunchSimsOp>;
	using FExecuterType = UE::Geometry::FAsyncTaskExecuterWithProgressCancel<FTaskType>;

	const FText DefaultMessage(LOCTEXT("ClothTrainingMessage", "Generate training data..."));

	if (!AllocateSimResources_GameThread(ToolProperties->NumThreads))
	{
		return;
	}
	FCacheUserToken CacheUserToken = Cache->BeginRecord(ClothComponent, FGuid(), FTransform::Identity);

	TUniquePtr<FLaunchSimsOp> NewOp = MakeUnique<FLaunchSimsOp>(SimResources, *SimMutex, ToolProperties);
	TUniquePtr<FExecuterType> BackgroundTaskExecuter = MakeUnique<FExecuterType>(MoveTemp(NewOp));
	BackgroundTaskExecuter->StartBackgroundTask();

	FScopedSlowTask SlowTask(1, DefaultMessage);
	SlowTask.MakeDialog(true);

	{
		UE::ClothTrainingTool::Private::FTimeScope TimeScope(TEXT("Cloth generation"));
		while (true)
		{
			if (SlowTask.ShouldCancel())
			{
				// Release ownership to the TDeleterTask that is spawned by CancelAndDelete()
				BackgroundTaskExecuter.Release()->CancelAndDelete();
				break;
			}
			if (BackgroundTaskExecuter->IsDone())
			{
				break;
			}
			FPlatformProcess::Sleep(.2); // SlowTask::ShouldCancel will throttle any updates faster than .2 seconds
			float ProgressFrac;
			FText ProgressMessage;
			const bool bMadeProgress = BackgroundTaskExecuter->PollProgress(ProgressFrac, ProgressMessage);
			if (bMadeProgress)
			{
				// SlowTask expects progress to be reported before it happens; we work around this by directly updating the progress amount
				SlowTask.CompletedWork = ProgressFrac;
				SlowTask.EnterProgressFrame(0, ProgressMessage);
			}
			else
			{
				SlowTask.TickProgress(); // Still tick the UI when we don't get new progress frames
			}
		}
		FreeSimResources_GameThread();
	}
	{
		UE::ClothTrainingTool::Private::FTimeScope TimeScope(TEXT("Saving"));
		Cache->bCompressChannels = true;
		Cache->EndRecord(CacheUserToken);

		SaveCacheCollection(CacheCollection);
	}
}


void UClothTrainingTool::OnTick(float DeltaTime)
{
	if (PendingAction != EClothTrainingToolActions::NoAction)
	{
		if (PendingAction == EClothTrainingToolActions::Train)
		{
			RunTraining();
		}
		PendingAction = EClothTrainingToolActions::NoAction;
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
	Super::Shutdown(ShutdownType);
	ToolProperties->SaveProperties(this);
}

bool UClothTrainingTool::AllocateSimResources_GameThread(int32 Num)
{
	SimResources.Reserve(Num);
	for(int32 Index = 0; Index < Num; ++Index)
	{
		UChaosClothComponent* const CopyComponent = DuplicateObject<UChaosClothComponent>(ClothComponent, ClothComponent->GetOuter());
		CopyComponent->RegisterComponent();
		CopyComponent->SetWorldTransform(ClothComponent->GetComponentTransform());
		FSimResource SimResource;
		SimResource.ClothComponent = CopyComponent;
		SimResource.Proxy = MakeUnique<FClothSimulationDataGenerationProxy>(*CopyComponent);
		SimResource.Pipe = MakeUnique<UE::Tasks::FPipe>(*FString::Printf(TEXT("SimPipe:%d"), Index));

		if (CopyComponent == nullptr || SimResource.Proxy == nullptr || SimResource.Pipe == nullptr)
		{
			UE_LOG(LogClothTrainingTool, Error, TEXT("Failed to allocate simulation resources"));
			return false;
		}
		SimResources.Add(MoveTemp(SimResource));
	}
	SimMutex = MakeUnique<FCriticalSection>();
	return true;
}

void UClothTrainingTool::FreeSimResources_GameThread()
{
	{
		FScopeLock Lock(SimMutex.Get());
		for (FSimResource& SimResource : SimResources)
		{
			SimResource.Pipe.Reset();
			SimResource.Proxy.Reset();
			SimResource.ClothComponent->UnregisterComponent();
			SimResource.ClothComponent->DestroyComponent();
		}
		SimResources.Empty();
	}
	SimMutex.Reset();
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

#undef LOCTEXT_NAMESPACE

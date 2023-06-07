// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "Animation/AttributesRuntime.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BonePose.h"
#include "Chaos/TriangleMesh.h"
#include "ChaosCloth/ChaosClothingSimulationConfig.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothDataGenerationComponent.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothComponentToolTarget.h"
#include "ChaosClothAsset/ClothCollection.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "ClothingSystemRuntimeTypes.h"
#include "ClothTrainingToolProperties.h"
#include "ComponentReregisterContext.h"
#include "ContextObjectStore.h"
#include "Engine/SkinnedAssetCommon.h"
#include "FileHelpers.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "GeometryCache.h"
#include "GeometryCacheCodecV1.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrackStreamable.h"
#include "GeometryCacheConstantTopologyWriter.h"
#include "InteractiveToolManager.h"
#include "Internationalization/Regex.h"
#include "ModelingOperators.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/Optional.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalRenderPublic.h"
#include "Tasks/Pipe.h"
#include "ToolTargetManager.h"
#include "UObject/SavePackage.h"

#include <atomic>

#define LOCTEXT_NAMESPACE "ClothTrainingTool"
DEFINE_LOG_CATEGORY_STATIC(LogClothTrainingTool, Log, All);

namespace UE::ClothTrainingTool::Private
{
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

	TArray<uint32> Range(uint32 Start, uint32 End)
	{
		TArray<uint32> Result;
		const uint32 Num = End - Start;
		Result.SetNumUninitialized(Num);
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			Result.Add(Index + Start);
		}
		return Result;
	}

	int32 GetNumVertices(const FSkeletalMeshLODRenderData& LODData)
	{
		int32 NumVertices = 0;
		for(const FSkelMeshRenderSection& Section : LODData.RenderSections)
		{
			NumVertices += Section.NumVertices;
		}
		return NumVertices;
	}

	TArrayView<TArray<FVector3f>> ShrinkToValidFrames(const TArrayView<TArray<FVector3f>>& Positions, int32 NumVertices)
	{
		int32 NumValidFrames = 0;
		for (const TArray<FVector3f>& Frame : Positions)
		{
			if (Frame.Num() != NumVertices)
			{
				break;
			}
			++NumValidFrames;
		}
		return TArrayView<TArray<FVector3f>>(Positions.GetData(), NumValidFrames);
	}

	void SaveGeometryCache(UGeometryCache& GeometryCache, const USkinnedAsset& Asset, TConstArrayView<uint32> ImportedVertexNumbers, TArrayView<TArray<FVector3f>> PositionsToMoveFrom)
	{
		const FSkeletalMeshRenderData* RenderData = Asset.GetResourceForRendering();
		constexpr int32 LODIndex = 0;
		if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
		{
			return;
		}
		const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
		const int32 NumVertices = GetNumVertices(LODData);
		PositionsToMoveFrom = ShrinkToValidFrames(PositionsToMoveFrom, NumVertices);

		using UE::GeometryCacheHelpers::FGeometryCacheConstantTopologyWriter;
		using UE::GeometryCacheHelpers::AddTrackWriterFromSkinnedAsset;
		using FTrackWriter = FGeometryCacheConstantTopologyWriter::FTrackWriter;
		FGeometryCacheConstantTopologyWriter Writer(GeometryCache);
		const int32 Index = AddTrackWriterFromSkinnedAsset(Writer, Asset);
		if (Index == INDEX_NONE)
		{
			return;
		}
		FTrackWriter& TrackWriter = Writer.GetTrackWriter(Index);
		TrackWriter.ImportedVertexNumbers = ImportedVertexNumbers;
		TrackWriter.WriteAndClose(PositionsToMoveFrom);
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

	template<class T>
	T* CreateOrLoad(const FString& PackageName)
	{
		const FName AssetName(FPackageName::GetLongPackageAssetName(PackageName));		
		if (UPackage* const Package = CreatePackage(*PackageName))
		{
			LoadPackage(nullptr, *PackageName, LOAD_Quiet | LOAD_EditorOnly);
			T* Asset = FindObject<T>(Package, *AssetName.ToString());
			if (!Asset)
			{
				Asset = NewObject<T>(Package, *AssetName.ToString(), RF_Public | RF_Standalone | RF_Transactional);
				Asset->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(Asset);
			}
			return Asset;
		}
		return nullptr;
	}

	void SavePackage(UObject& Object)
	{
		TArray<UPackage*> PackagesToSave = { Object.GetOutermost() };
		constexpr bool bCheckDirty = false;
		constexpr bool bPromptToSave = false;
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
	}
	
	TOptional<TArray<int32>> GetMeshImportVertexMap(const USkinnedAsset& MLDeformerAsset, const UChaosClothAsset& ClothAsset)
	{
		constexpr int32 LODIndex = 0;
		const TOptional<TArray<int32>> None;
		const FSkeletalMeshModel* const MLDModel = MLDeformerAsset.GetImportedModel();
		if (!MLDModel || !MLDModel->LODModels.IsValidIndex(LODIndex))
		{
			return None;
		}
		const FSkeletalMeshLODModel& MLDLOD = MLDModel->LODModels[LODIndex];
		const TArray<int32>& Map = MLDLOD.MeshToImportVertexMap;
		if (Map.IsEmpty())
		{
			UE_LOG(LogClothTrainingTool, Warning, TEXT("MeshToImportVertexMap is empty. MLDeformer Asset should be an imported SkeletalMesh (e.g. from fbx)."));
			return None;
		}
		const FSkeletalMeshModel* const ClothModel = ClothAsset.GetImportedModel();
		if (!ClothModel || !ClothModel->LODModels.IsValidIndex(LODIndex))
		{
			UE_LOG(LogClothTrainingTool, Warning, TEXT("ClothAsset has no imported model."));
			return None;
		}
		const FSkeletalMeshLODModel& ClothLOD = ClothModel->LODModels[LODIndex];

		if (MLDLOD.NumVertices != ClothLOD.NumVertices || MLDLOD.Sections.Num() != ClothLOD.Sections.Num())
		{
			UE_LOG(LogClothTrainingTool, Warning, TEXT("MLDeformerAsset and ClothAsset have different number of vertices or sections. Check if the assets have the same mesh."));
			return None;
		}
		
		for (int32 SectionIndex = 0; SectionIndex < MLDLOD.Sections.Num(); ++SectionIndex)
		{
			const FSkelMeshSection& MLDSection = MLDLOD.Sections[SectionIndex];
			const FSkelMeshSection& ClothSection = ClothLOD.Sections[SectionIndex];
			if (MLDSection.NumVertices != ClothSection.NumVertices)
			{
				UE_LOG(LogClothTrainingTool, Warning, TEXT("MLDeformerAsset and ClothAsset have different number of vertices in section %d. Check if the assets have the same mesh."), SectionIndex);
				return None;
			}
			for (int32 VertexIndex = 0; VertexIndex < MLDSection.NumVertices; ++VertexIndex)
			{
				const FVector3f& MLDPosition = MLDSection.SoftVertices[VertexIndex].Position;
				const FVector3f& ClothPosition = ClothSection.SoftVertices[VertexIndex].Position;
				if (!MLDPosition.Equals(ClothPosition, UE_KINDA_SMALL_NUMBER))
				{
					UE_LOG(LogClothTrainingTool, Warning, TEXT("MLDeformerAsset and ClothAsset have different vertex positions. Check if the assets have the same vertex order."));
					return None;
				}
			}
		}

		return Map;
	}
};

// @@@@@@@@@ TODO: Change this to whatever makes sense for output
struct FSkinnedMeshVertices
{
	TArray<FVector3f> Vertices;
};

struct UClothTrainingTool::FSimResource
{
	UClothDataGenerationComponent* ClothComponent = nullptr;
	TSharedPtr<FProxy> Proxy;
	TUniquePtr<UE::Tasks::FPipe> Pipe;
	FEvent* SkinEvent = nullptr;
	std::atomic<bool> bNeedsSkin = false;
	TArrayView<TArray<FVector3f>> SimulatedPositions;
};

struct UClothTrainingTool::FTaskResource
{
	TUniquePtr<FCriticalSection> SimMutex;
	TArray<FSimResource> SimResources;

	TUniquePtr<FExecuterType> Executer;
	TUniquePtr<FAsyncTaskNotification> Notification;
	FDateTime StartTime;
	FDateTime LastUpdateTime;

	TArray<TArray<FVector3f>> SimulatedPositions;
	TArray<uint32> ImportedVertexNumbers;
	UGeometryCache* Cache = nullptr;

	bool AllocateSimResources_GameThread(const UChaosClothComponent& InClothComponent, int32 Num);
	void FreeSimResources_GameThread();
	void FlushRendering();
};

class UClothTrainingTool::FLaunchSimsOp : public UE::Geometry::TGenericDataOperator<FSkinnedMeshVertices>
{
public: 
	FLaunchSimsOp(FTaskResource& InTaskResource, TObjectPtr<UClothTrainingToolProperties> InToolProperties)
		: SimResources(InTaskResource.SimResources)
		, SimMutex(*InTaskResource.SimMutex)
		, SimulatedPositions(InTaskResource.SimulatedPositions)
		, ToolProperties(InToolProperties)
	{}

	virtual void CalculateResult(FProgressCancel* Progress) override;

private:
	using FPipe = UE::Tasks::FPipe;

	enum class ESaveType
	{
		LastStep,
		EveryStep,
	};

	void Simulate(FSimResource &SimResource, int32 AnimFrame, int32 CacheFrame, FProgressCancel* Progress, float ProgressStep);
	void PrepareAnimationSequence();
	void RestoreAnimationSequence();
	TArray<FTransform> GetBoneTransforms(UChaosClothComponent& InClothComponent, int32 Frame) const;
	TArray<FVector3f> GetRenderPositions(FSimResource& SimResource) const;

	TArray<FSimResource>& SimResources;
	FCriticalSection& SimMutex;
	TArray<TArray<FVector3f>>& SimulatedPositions;
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

TArray<FTransform> UClothTrainingTool::FLaunchSimsOp::GetBoneTransforms(UChaosClothComponent& InClothComponent, int32 Frame) const
{
	const UAnimSequence* AnimationSequence = ToolProperties->AnimationSequence;
	const double Time = FMath::Clamp(AnimationSequence->GetSamplingFrameRate().AsSeconds(Frame), 0., (double)AnimationSequence->GetPlayLength());
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

void UClothTrainingTool::FLaunchSimsOp::CalculateResult(FProgressCancel* Progress)
{
	using UE::ClothTrainingTool::Private::ParseFrames;
	using UE::ClothTrainingTool::Private::Range;

	const TArray<int32> FramesToSimulate = ToolProperties->bDebug 
		? TArray<int32>{ (int32)ToolProperties->DebugFrame } 
		: (ToolProperties->FramesToSimulate.Len() > 0
			? ParseFrames(ToolProperties->FramesToSimulate) 
			: Range(ToolProperties->AnimationSequence->GetNumberOfSampledKeys()));

	const int32 NumFrames = FramesToSimulate.Num();
	if (NumFrames == 0)
	{
		return;
	}
	SimulatedPositions.SetNum(ToolProperties->bDebug ? ToolProperties->NumSteps : NumFrames);
	const float ProgressStep = 1.f / NumFrames;
	PrepareAnimationSequence();

	const int32 NumThreads = ToolProperties->bDebug ? 1 : ToolProperties->NumThreads;
	FScopeLock Lock(&SimMutex);

	for (int32 Frame = 0; Frame < NumFrames; Frame++)
	{
		if (Progress && !Progress->Cancelled())
		{
			const int32 ThreadIdx = Frame % NumThreads;
			const int32 AnimFrame = FramesToSimulate[Frame];

			FSimResource& SimResource = SimResources[ThreadIdx];
			SimResource.Pipe->Launch(*FString::Printf(TEXT("SimFrame:%d"), AnimFrame), [this, &SimResource, AnimFrame, Frame, Progress, ProgressStep]()
			{ 
				FMemMark Mark(FMemStack::Get());
				Simulate(SimResource, AnimFrame, Frame, Progress, ProgressStep);
			});
		}
		else
		{
			break;
		}
	}

	for (FSimResource& SimResource : SimResources)
	{
		SimResource.Pipe->WaitUntilEmpty();
	}

	RestoreAnimationSequence();
}

void UClothTrainingTool::FLaunchSimsOp::Simulate(FSimResource &SimResource, int32 AnimFrame, int32 CacheFrame, FProgressCancel* Progress, float ProgressStep)
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
		if (Progress && !Progress->Cancelled())
		{
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
				SimulatedPositions[Step] = GetRenderPositions(SimResource);
			}
		}
		else
		{
			bCancelled = true;
			break;
		}
	}

	if (SaveType == ESaveType::LastStep && !bCancelled)
	{
		DataGenerationProxy.WriteSimulationData();
		SimulatedPositions[CacheFrame] = GetRenderPositions(SimResource);
	}

	Progress->AdvanceCurrentScopeProgressBy(ProgressStep);
}

TArray<FVector3f> UClothTrainingTool::FLaunchSimsOp::GetRenderPositions(FSimResource& SimResource) const
{
	check(SimResource.ClothComponent);
	TArray<FVector3f> Positions;
	TArray<FFinalSkinVertex> OutVertices;
	// This could potentially be slow. 
	SimResource.ClothComponent->RecreateRenderState_Concurrent();
	SimResource.bNeedsSkin.store(true);
	SimResource.SkinEvent->Wait();
	
	SimResource.ClothComponent->GetCPUSkinnedCachedFinalVertices(OutVertices);
	Positions.SetNum(OutVertices.Num());
	for (int32 Index = 0; Index < OutVertices.Num(); ++Index)
	{
		Positions[Index] = OutVertices[Index].Position;
	}
	return Positions;
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
	UGeometryCache* const Cache = GetCache();
	if (Cache == nullptr)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	if (TaskResource != nullptr)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	if (!ToolProperties->MLDeformerAsset)
	{
		PendingAction = EClothTrainingToolActions::NoAction;
		return;
	}
	using UE::ClothTrainingTool::Private::GetMeshImportVertexMap;
	TOptional<TArray<int32>> OptionalMap = GetMeshImportVertexMap(*ToolProperties->MLDeformerAsset, *ClothComponent->GetClothAsset());
	if (!OptionalMap)
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
	TaskResource->Cache = Cache;

	TUniquePtr<FLaunchSimsOp> NewOp = MakeUnique<FLaunchSimsOp>(*TaskResource, ToolProperties);
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

	const TArray<int32>& Map = OptionalMap.GetValue();
	TaskResource->ImportedVertexNumbers = TArray<uint32>(reinterpret_cast<const uint32*>(Map.GetData()), Map.Num());

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
	SimResources.SetNum(Num);
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

		SimResource.SimulatedPositions = TArrayView<TArray<FVector3f>>(SimulatedPositions);

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

UGeometryCache* UClothTrainingTool::GetCache() const
{
	const FString PackageName = ToolProperties->bDebug ? ToolProperties->DebugCacheName : ToolProperties->SimulatedCacheName;
	return UE::ClothTrainingTool::Private::CreateOrLoad<UGeometryCache>(PackageName);
}

void UClothTrainingTool::FreeTaskResource(bool bCancelled)
{
	TaskResource->Notification->SetProgressText(LOCTEXT("Finishing", "Finishing, please wait"));
	TaskResource->FreeSimResources_GameThread();
	const FDateTime CurrentTime = FDateTime::UtcNow();
	UE_LOG(LogClothTrainingTool, Log, TEXT("Training finished in %f seconds"), (CurrentTime - TaskResource->StartTime).GetTotalSeconds());

	{
		UE::ClothTrainingTool::Private::FTimeScope TimeScope(TEXT("Saving"));

		using UE::ClothTrainingTool::Private::SaveGeometryCache;
		using UE::ClothTrainingTool::Private::SavePackage;
		SaveGeometryCache(*TaskResource->Cache, *ClothComponent->GetClothAsset(), TaskResource->ImportedVertexNumbers, TaskResource->SimulatedPositions);
		SavePackage(*TaskResource->Cache);
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

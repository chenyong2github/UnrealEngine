// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshPoseTrack.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "AnimationSharedData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#include "UObject/SoftObjectPtr.h"
#include "InsightsSkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#endif


#define LOCTEXT_NAMESPACE "SkeletalMeshPoseTrack"

const FName FSkeletalMeshPoseTrack::TypeName(TEXT("Events"));
const FName FSkeletalMeshPoseTrack::SubTypeName(TEXT("Animation.SkeletalMeshPose"));

FSkeletalMeshPoseTrack::FSkeletalMeshPoseTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, const TCHAR* InName)
	: TGameplayTrackMixin<FTimingEventsTrack>(InObjectID, FSkeletalMeshPoseTrack::TypeName, FSkeletalMeshPoseTrack::SubTypeName, FText::Format(LOCTEXT("TrackNameFormat", "Pose - {0}"), FText::FromString(FString(InName))))
	, SharedData(InSharedData)
	, Color(FLinearColor::MakeRandomColor())
	, bDrawPose(false)
	, bDrawSkeleton(false)
	, bPotentiallyDebugged(false)
{
#if WITH_ENGINE
	OnWorldDestroyedHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FSkeletalMeshPoseTrack::OnWorldCleanup);
#endif
}

FSkeletalMeshPoseTrack::~FSkeletalMeshPoseTrack()
{
#if WITH_ENGINE
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldDestroyedHandle);

	for(auto& WorldCacheEntry : WorldCache)
	{
		if(WorldCacheEntry.Value.Component)
		{
			WorldCacheEntry.Value.Component->UnregisterComponent();
			WorldCacheEntry.Value.Component->MarkPendingKill();
			WorldCacheEntry.Value.Component = nullptr;
		}

		if(WorldCacheEntry.Value.Actor)
		{
			WorldCacheEntry.Value.Actor->Destroy();
			WorldCacheEntry.Value.Actor = nullptr;
		}
	}

	WorldCache.Empty();
#endif
}

void FSkeletalMeshPoseTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [&Context, &Builder](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(Context.GetViewport().GetStartTime(), Context.GetViewport().GetEndTime(), [&Builder](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
			{
				static TCHAR Buffer[256];
				FCString::Snprintf(Buffer, 256, TEXT("%d Bones"), InMessage.NumTransforms);
				Builder.AddEvent(InStartTime, InEndTime, 0, Buffer);
			});
		});
	}
}

void FSkeletalMeshPoseTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	DrawEvents(Context);
	GetGameplayTrack().DrawHeaderForTimingTrack(Context, *this, false);
}

void FSkeletalMeshPoseTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindSkeletalMeshPoseMessage(SearchParameters, [this, &Tooltip](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FSkeletalMeshPoseMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(LOCTEXT("SkeletalMeshPoseTooltipTitle", "Skeletal Mesh Pose").ToString());

		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(InFoundStartTime).ToString());

		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
			const FObjectInfo* SkeletalMeshObjectInfo = GameplayProvider->FindObjectInfo(InMessage.MeshId);
			if(SkeletalMeshObjectInfo != nullptr)
			{
				Tooltip.AddNameValueTextLine(LOCTEXT("Mesh", "Mesh").ToString(), SkeletalMeshObjectInfo->PathName);

				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				if(!AssetRegistryModule.Get().GetAssetByObjectPath(SkeletalMeshObjectInfo->PathName).IsValid())
				{
					Tooltip.AddTextLine(LOCTEXT("MeshNotFound", "Mesh not found").ToString(), FLinearColor::Red);
				}
			}
		}

		Tooltip.AddNameValueTextLine(LOCTEXT("BoneCount", "Bone Count").ToString(), FText::AsNumber(InMessage.NumTransforms).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("CurveCount", "Curve Count").ToString(), FText::AsNumber(InMessage.NumCurves).ToString());

		Tooltip.UpdateLayout();
	});
}

const TSharedPtr<const ITimingEvent> FSkeletalMeshPoseTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindSkeletalMeshPoseMessage(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FSkeletalMeshPoseMessage& InFoundMessage)
	{
		FoundEvent = MakeShared<const FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

void FSkeletalMeshPoseTrack::FindSkeletalMeshPoseMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FSkeletalMeshPoseMessage&)> InFoundPredicate) const
{
	TTimingEventSearch<FSkeletalMeshPoseMessage>::Search(
		InParameters,

		[this](TTimingEventSearch<FSkeletalMeshPoseMessage>::FContext& InContext)
		{
			const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

			if(AnimationProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

				AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [&InContext](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
				{
					InTimeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
					{
						InContext.Check(InEventStartTime, InEventEndTime, 0, InMessage);
					});
				});
			}
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FSkeletalMeshPoseMessage& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		});
}

void FSkeletalMeshPoseTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(TEXT("DrawingSection"), LOCTEXT("Drawing", "Drawing (Component)"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawPose", "Draw Pose"),
			LOCTEXT("ToggleDrawPose_Tooltip", "Draw the poses in this track"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					bDrawPose = !bDrawPose;
					UpdateComponentVisibility();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawPose; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleDrawSkeleton", "Draw Skeleton"),
			LOCTEXT("ToggleDrawSkeleton_Tooltip", "Draw the skeleton for poses in this track (when pose drawing is also enabled)"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this](){ bDrawSkeleton = !bDrawSkeleton; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return bDrawSkeleton; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());
	
		const FObjectInfo* ComponentObjectInfo = GameplayProvider->FindObjectInfo(GetGameplayTrack().GetObjectId());
		if(ComponentObjectInfo != nullptr)
		{
			// @FIXME: Outer does always equal owning actor, although does in nearly all cases with skeletal mesh components
			const FObjectInfo* ActorObjectInfo = GameplayProvider->FindObjectInfo(ComponentObjectInfo->OuterId);
			if(ActorObjectInfo != nullptr)
			{
				MenuBuilder.BeginSection(TEXT("DrawingSection"), FText::Format(LOCTEXT("DrawingActor", "Drawing ({0})"), FText::FromString(ActorObjectInfo->Name)));
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("ToggleDrawPoseActor", "Draw Pose for Actor"),
						LOCTEXT("ToggleDrawPoseActor_Tooltip", "Draw the poses in this track and all other tracks for the current actor"),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this, ActorObjectInfo, GameplayProvider]()
							{
								Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

								bool bSetDrawPose = true;
								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bSetDrawPose](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										bSetDrawPose &= InTrack->bDrawPose;
									}
								});

								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bSetDrawPose](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										InTrack->bDrawPose = !bSetDrawPose;
										InTrack->UpdateComponentVisibility();
									}
								});
							}),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([this, ActorObjectInfo, GameplayProvider]()
							{
								Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

								bool bDrawPoseSet = true;
								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bDrawPoseSet](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										bDrawPoseSet &= InTrack->bDrawPose;
									}
								});

								return bDrawPoseSet;
							})),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);

					MenuBuilder.AddMenuEntry(
						LOCTEXT("ToggleDrawSkeletonActor", "Draw Skeleton for Actor"),
						LOCTEXT("ToggleDrawSkeletonActor_Tooltip", "Draw the skeleton for poses in this track and all other tracks for the current actor (when pose drawing is also enabled)"),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this, ActorObjectInfo, GameplayProvider]()
							{
								Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

								bool bSetDrawSkeleton = true;
								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bSetDrawSkeleton](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										bSetDrawSkeleton &= InTrack->bDrawSkeleton;
									}
								});

								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bSetDrawSkeleton](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										InTrack->bDrawSkeleton = !bSetDrawSkeleton;
										InTrack->UpdateComponentVisibility();
									}
								});
							}),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([this, ActorObjectInfo, GameplayProvider]()
							{
								Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

								bool bDrawSkeletonSet = true;
								SharedData.EnumerateSkeletalMeshPoseTracks([GameplayProvider, ActorObjectInfo, &bDrawSkeletonSet](const TSharedRef<FSkeletalMeshPoseTrack>& InTrack)
								{
									const FObjectInfo* OtherComponentObjectInfo = GameplayProvider->FindObjectInfo(InTrack->GetGameplayTrack().GetObjectId());
									if(OtherComponentObjectInfo->OuterId == ActorObjectInfo->Id)
									{
										bDrawSkeletonSet &= InTrack->bDrawSkeleton;
									}
								});

								return bDrawSkeletonSet;
							})),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);
				}
				MenuBuilder.EndSection();
			}
		}
	}
}

#if WITH_ENGINE

void FSkeletalMeshPoseTrack::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	FWorldComponentCache& CacheForWorld = GetWorldCache(InWorld);

	if(CacheForWorld.Component)
	{
		CacheForWorld.Component->UnregisterComponent();
		CacheForWorld.Component->MarkPendingKill();
		CacheForWorld.Component = nullptr;
	}

	WorldCache.Remove(TWeakObjectPtr<UWorld>(InWorld));
}

USkeletalMeshComponent* FSkeletalMeshPoseTrack::GetComponent(UWorld* InWorld)
{
	if(InWorld)
	{
		return GetWorldCache(InWorld).GetComponent();
	}

	return nullptr;
}

void FSkeletalMeshPoseTrack::DrawPoses(UWorld* InWorld, double InTime)
{
	if(SharedData.IsAnalysisSessionValid())
	{
		const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
		const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

		if(AnimationProvider && GameplayProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			FWorldComponentCache& CacheForWorld = GetWorldCache(InWorld);
			if(CacheForWorld.Component)
			{
				CacheForWorld.Component->SetVisibility(false);
			}

			AnimationProvider->ReadSkeletalMeshPoseTimeline(GetGameplayTrack().GetObjectId(), [this, &CacheForWorld, &AnimationProvider, &GameplayProvider, &InTime](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline)
			{
				InTimeline.EnumerateEvents(InTime, InTime, [this, &CacheForWorld, &AnimationProvider, &GameplayProvider, &InTime](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
				{
					if((InStartTime <= InTime && InEndTime > InTime))
					{
						const FSkeletalMeshInfo* SkeletalMeshInfo = AnimationProvider->FindSkeletalMeshInfo(InMessage.MeshId);
						const FObjectInfo* SkeletalMeshObjectInfo = GameplayProvider->FindObjectInfo(InMessage.MeshId);
						if(SkeletalMeshInfo && SkeletalMeshObjectInfo)
						{
							UInsightsSkeletalMeshComponent* Component = CacheForWorld.GetComponent();
							Component->SetVisibility(bDrawPose);

							if(CacheForWorld.Time != InTime)
							{
								USkeletalMesh* SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(SkeletalMeshObjectInfo->PathName)).LoadSynchronous();
								if(SkeletalMesh)
								{
									Component->SetSkeletalMesh(SkeletalMesh);
								}

								Component->SetPoseFromProvider(*AnimationProvider, InMessage, *SkeletalMeshInfo);

								CacheForWorld.Time = InTime;
							}

							Component->SetDrawDebugSkeleton(bDrawSkeleton);
							Component->SetDebugDrawColor(Color);
						}
					}
				});
			});
		}
	}
}

FSkeletalMeshPoseTrack::FWorldComponentCache& FSkeletalMeshPoseTrack::GetWorldCache(UWorld* InWorld)
{
	FWorldComponentCache& Cache = WorldCache.FindOrAdd(TWeakObjectPtr<UWorld>(InWorld));
	Cache.World = InWorld;
	return Cache;
}

UInsightsSkeletalMeshComponent* FSkeletalMeshPoseTrack::FWorldComponentCache::GetComponent()
{
	if(Actor == nullptr)
	{
		Actor = World->SpawnActor<AActor>();
		Actor->SetActorLabel(TEXT("Insights"));

		Time = 0.0;
	}

	if(Component == nullptr)
	{
		Component = NewObject<UInsightsSkeletalMeshComponent>(Actor);
		Component->PrimaryComponentTick.bStartWithTickEnabled = false;
		Component->PrimaryComponentTick.bCanEverTick = false;

		Actor->AddInstanceComponent(Component);

		Component->SetAnimationMode(EAnimationMode::AnimationCustomMode);
		Component->RegisterComponentWithWorld(World);

		Time = 0.0;
	}
	
	return Component;
}

void FSkeletalMeshPoseTrack::AddReferencedObjects(FReferenceCollector& Collector)
{
	for(auto& WorldCacheEntry : WorldCache)
	{
		Collector.AddReferencedObject(WorldCacheEntry.Value.Actor);
		Collector.AddReferencedObject(WorldCacheEntry.Value.Component);
	}
}

void FSkeletalMeshPoseTrack::UpdateComponentVisibility()
{
	for(auto& WorldCacheEntry : WorldCache)
	{
		if(WorldCacheEntry.Value.Component)
		{
			WorldCacheEntry.Value.Component->SetVisibility(bDrawPose);
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE
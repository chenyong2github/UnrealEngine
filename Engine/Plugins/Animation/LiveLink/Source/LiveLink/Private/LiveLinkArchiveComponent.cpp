// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkArchiveComponent.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY(LogLiveLinkArchiveComponent);


// Sets default values for this component's properties
ULiveLinkArchiveComponent::ULiveLinkArchiveComponent()
	: CaptureRate(60.0f)
	, ArchiveName(TEXT("LiveLinkArchive"))
	, bInterpolatePlayback(true)
	, WorldTimeAtArchivePlayStart(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	bTickInEditor = true;
}

void ULiveLinkArchiveComponent::OnRegister()
{
	bIsArchivePlaying = false;
	bIsArchivingFrames = false;
	Super::OnRegister();
}


// Called every frame
void ULiveLinkArchiveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bIsArchivingFrames)
	{
		CaptureRateTimer -= DeltaTime;
		if (CaptureRateTimer <= 0.0f)
		{
			if (HasLiveLinkClient())
			{
				const double CurrentTime = FPlatformTime::Seconds();
				const FLiveLinkSubjectFrame* FoundFrame = LiveLinkClient->GetSubjectDataAtWorldTime(LiveLinkSubjectToArchive, CurrentTime);

				if (FoundFrame)
				{
					//If we were the first frame, need to save off WorldTimeAtCaptureStart
					double TimeSinceCaptureStart = 0;
					if (ArchivedFrames.Num() == 0)
					{
						WorldTimeAtCaptureStart = CurrentTime;
                        UE_LOG(LogLiveLinkArchiveComponent, Verbose, TEXT("Setting WorldTimeAtCaptureStart: %f"), WorldTimeAtCaptureStart);
					}
					else
					{
						TimeSinceCaptureStart = (CurrentTime - WorldTimeAtCaptureStart);
					}
					
					FLiveLinkArchiveFrame NewArchiveFrame(*FoundFrame, TimeSinceCaptureStart);
					ArchivedFrames.Add(NewArchiveFrame);
                    
                    UE_LOG(LogLiveLinkArchiveComponent, Verbose, TEXT("Adding Frame at index:%d time:%f"), (ArchivedFrames.Num() -1 ), TimeSinceCaptureStart);
				}
			}

			//Reset capture rate timer
            CaptureRateTimer = (1 / CaptureRate);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool ULiveLinkArchiveComponent::HasLiveLinkClient()
{
	if (LiveLinkClient == nullptr)
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		}
	}

	return (LiveLinkClient != nullptr);
}

void ULiveLinkArchiveComponent::BeginLiveLinkCapture()
{
	ArchivedFrames.Reset();
	bIsArchivingFrames = true;

	//Capture first ticked frame immediately
	CaptureRateTimer = 0.0f;
    
    UE_LOG(LogLiveLinkArchiveComponent, Log, TEXT("Started LiveLinkArchive Capture"));
}

void ULiveLinkArchiveComponent::StopLiveLinkCapture()
{
	bIsArchivingFrames = false;
    
    UE_LOG(LogLiveLinkArchiveComponent, Log, TEXT("Stopped LiveLinkArchive Capture"));
}


void ULiveLinkArchiveComponent::GetSubjectDataAtWorldTime(const double WorldTimeIn, bool& bSuccess, FLiveLinkSubjectFrame& SubjectFrameHandle)
{
	bSuccess = false;
    
    //Need to adjust world time so we are looking for
    const double AdjustedWorldTime = WorldTimeIn - WorldTimeAtArchivePlayStart;
    UE_LOG(LogLiveLinkArchiveComponent, VeryVerbose, TEXT("Adjusting WorldTime. In:%f  StartTime:%f Adjusted Time: %f"), WorldTimeIn, WorldTimeAtArchivePlayStart, AdjustedWorldTime);
    
    if (bIsArchivePlaying && (ArchivedFrames.Num() > 0))
    {
        int StartingFrame = FindIndexOfStartingFrame(AdjustedWorldTime);
        if (ArchivedFrames.IsValidIndex(StartingFrame))
        {
            bSuccess = true;

            //If we aren't interpolating, or we don't have a future frame to interpolate with because we are the most recent frame, just return found frame
            if (!bInterpolatePlayback || (StartingFrame == (ArchivedFrames.Num() - 1)))
            {
                SubjectFrameHandle = ArchivedFrames[StartingFrame].ArchivedFrame;
            }
            else
            {
                const FLiveLinkArchiveFrame& PreFrame = ArchivedFrames[StartingFrame];
                const FLiveLinkArchiveFrame& PostFrame = ArchivedFrames[StartingFrame + 1];
                FLiveLinkSubjectFrame OutFrame;
                
                //Grab all these. They should be the same for Pre and Post frame, so just grab Post Frame
                OutFrame.RefSkeleton = PostFrame.ArchivedFrame.RefSkeleton;
                OutFrame.RefSkeletonGuid = PostFrame.ArchivedFrame.RefSkeletonGuid;
                OutFrame.CurveKeyData = PostFrame.ArchivedFrame.CurveKeyData;

                // Calc blend weight (Amount through frame gap / frame gap). Protected in case we have 2 frames captured at 0.00f
                const float BlendWeight = (PostFrame.FrameTime > 0) ? (PreFrame.FrameTime) / (PostFrame.FrameTime) : 1.0f;
                
                CopyFrameDataBlended(PreFrame.ArchivedFrame, PostFrame.ArchivedFrame, BlendWeight, OutFrame);
                SubjectFrameHandle = OutFrame;
            }
        }
    }
}

int ULiveLinkArchiveComponent::FindIndexOfStartingFrame(const double WorldTime) const
{
	int FoundIndex = INDEX_NONE;

	if (ArchivedFrames.Num() > 0)
	{
		for (int SearchIndex = 0; SearchIndex < ArchivedFrames.Num(); ++SearchIndex)
		{
			//If we can't go further in the array, or the next element in the array started after our search time,
			//then our current element is the best match for the beginning of the desired frame
			if ((!ArchivedFrames.IsValidIndex(SearchIndex + 1))
				|| (ArchivedFrames[SearchIndex + 1].FrameTime > WorldTime))
			{
				FoundIndex = SearchIndex;
	
                UE_LOG(LogLiveLinkArchiveComponent, Verbose, TEXT("Found Frame at: %d . Frame Time: %f , World Time: %f"), FoundIndex, ArchivedFrames[SearchIndex].FrameTime, WorldTime);
                break;
			}
		}
	}
    
	return FoundIndex;
}

void ULiveLinkArchiveComponent::CopyFrameDataBlended(const FLiveLinkSubjectFrame& PreFrame, const FLiveLinkSubjectFrame& PostFrame, float BlendWeight, FLiveLinkSubjectFrame& OutFrame)
{
	LiveLinkArchiveBlendHelpers::Blend(PreFrame.Transforms, PostFrame.Transforms, OutFrame.Transforms, BlendWeight);
	LiveLinkArchiveBlendHelpers::Blend(PreFrame.Curves, PostFrame.Curves, OutFrame.Curves, BlendWeight);
}

void ULiveLinkArchiveComponent::PlayFromArchive()
{
	WorldTimeAtArchivePlayStart = FPlatformTime::Seconds();
	bIsArchivePlaying = true;
    
    UE_LOG(LogLiveLinkArchiveComponent, Log, TEXT("Started playing LiveLinkArchive at %f"), WorldTimeAtArchivePlayStart);
}

void ULiveLinkArchiveComponent::StopPlaying()
{
	bIsArchivePlaying = false;
    
    UE_LOG(LogLiveLinkArchiveComponent, Log, TEXT("Stopped playing LiveLinkArchive"));
}

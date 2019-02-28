// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "ILiveLinkClient.h"
#include "LiveLinkBlueprintStructs.h"
#include "LiveLinkTypes.h"
#include "LiveLinkArchiveComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLiveLinkArchiveComponent, Log, All);

class LiveLinkArchiveBlendHelpers
{
public:
	//Blend Functions stolen from LiveLinkClient
	static void BlendItem(const FTransform& A, const FTransform& B, FTransform& Output, float BlendWeight)
	{
		const ScalarRegister ABlendWeight(1.0f - BlendWeight);
		const ScalarRegister BBlendWeight(BlendWeight);

		Output = A * ABlendWeight;
		Output.AccumulateWithShortestRotation(B, BBlendWeight);
		Output.NormalizeRotation();
	}

	static void BlendItem(const FOptionalCurveElement& A, const FOptionalCurveElement& B, FOptionalCurveElement& Output, float BlendWeight)
	{
		Output.Value = (A.Value * (1.0f - BlendWeight)) + (B.Value * BlendWeight);
		Output.bValid = A.bValid || B.bValid;
	}

	template<class Type>
	static void Blend(const TArray<Type>& A, const TArray<Type>& B, TArray<Type>& Output, float BlendWeight)
	{
		check(A.Num() == B.Num());
		Output.SetNum(A.Num(), false);

		for (int32 BlendIndex = 0; BlendIndex < A.Num(); ++BlendIndex)
		{
			BlendItem(A[BlendIndex], B[BlendIndex], Output[BlendIndex], BlendWeight);
		}
	}
};

//Helper struct to store off archive information
struct FLiveLinkArchiveFrame
{
	FLiveLinkSubjectFrame ArchivedFrame;

	//Stores world time of when this frame was recorded
	//First frame in archive should be 0, if next frame happened .5 seconds later it
	//will be .5 and so on
	double FrameTime;

	FLiveLinkArchiveFrame(FLiveLinkSubjectFrame FrameIn, double FrameTimeIn)
		: ArchivedFrame(FrameIn)
		, FrameTime(FrameTimeIn)
	{
	}
};


// An actor component to enable saving LiveLink data into a Frame Archive and then
// supply those frames from the archive on demand later
UCLASS( ClassGroup=(LiveLink), meta=(BlueprintSpawnableComponent) )
class LIVELINK_API ULiveLinkArchiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	ULiveLinkArchiveComponent();

protected:
	virtual void OnRegister() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// How frequently we would like to capture LiveLink data and save it in the archive. Value is Frames/Sec so 60.0 = 60 FPS capture.
	UPROPERTY(EditAnywhere, Category = "LiveLinkArchive")
	float CaptureRate;
	
	//Name used to look for this component by systems that interact with LiveLinkArchive (Different from LiveLink Subject names!)
	UPROPERTY(EditAnywhere, Category = "LiveLinkArchive")
	FName ArchiveName;

	//FName corresponding to the LiveLink subject we need to track and archive
	UPROPERTY(EditAnywhere, Category = "LiveLinkArchive")
	FName LiveLinkSubjectToArchive;

	//Determines if we should interpolate between stored archive frames during playback
	UPROPERTY(EditAnywhere, Category = "LiveLinkArchive")
	bool bInterpolatePlayback;

	UFUNCTION(BlueprintCallable, Category = "LiveLinkArchive")
	void BeginLiveLinkCapture();

	UFUNCTION(BlueprintCallable, Category = "LiveLinkArchive")
	void StopLiveLinkCapture();

	// When this function is called, this component will reset to the beginning of its archived LiveLink frames and begin playing through them
	UFUNCTION(BlueprintCallable, Category = "LiveLinkArchive")
	void PlayFromArchive();

	UFUNCTION(BlueprintCallable, Category = "LiveLinkArchive")
	void StopPlaying();

	// Returns a handle to the current frame of data in LiveLink Archive. PlayFromArchive must be called first, or this will fail
	void GetSubjectDataAtWorldTime(const double WorldTime, bool& bSuccess, FLiveLinkSubjectFrame& SubjectFrameHandle);

private:

	bool HasLiveLinkClient();
	
	int FindIndexOfStartingFrame(const double WorldTime) const;

	//Helper function to blend interpolated frame data
	void CopyFrameDataBlended(const FLiveLinkSubjectFrame& PreFrame, const FLiveLinkSubjectFrame& PostFrame, float BlendWeight, FLiveLinkSubjectFrame& OutFrame);

	//Used to offset the frame that should be gathered 
	double WorldTimeAtArchivePlayStart;

	//Used to offset the frame time when looking through archived frames
	double WorldTimeAtCaptureStart;
	
	//Used to track if its time to poll LiveLink for more data
	float CaptureRateTimer;

	bool bIsArchivePlaying;
	bool bIsArchivingFrames;
	TArray<FLiveLinkArchiveFrame> ArchivedFrames;
	ILiveLinkClient* LiveLinkClient;
};

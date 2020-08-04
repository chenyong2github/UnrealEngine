// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "LiveLinkTypes.h"
#include "Components/ActorComponent.h"
#include "BoneContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "LSALiveLinkSkelMeshSource.generated.h"

class ULSALiveLinkFrameTranslator;

/**
	* Bare bones Live Link source that will let us publish tracked skeletal mesh data.
	*/
class FLSALiveLinkSkelMeshSource : public ILiveLinkSource
{
public:

	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override
	{
		LiveLinkClient = InClient;
		SourceGuid = InSourceGuid;
	}

	virtual void Update() override
	{
	}

	virtual bool CanBeDisplayedInUI() const override
	{
		return false;
	}

	virtual bool IsSourceStillValid() const override
	{
		return true;
	}

	virtual bool RequestSourceShutdown() override
	{
		LiveLinkClient = nullptr;
		SourceGuid = FGuid();
		return true;
	}

	virtual FText GetSourceType() const override
	{
		return FText();
	}

	virtual FText GetSourceMachineName() const override
	{
		return FText();
	}

	virtual FText GetSourceStatus() const override
	{
		return FText();
	}

	ILiveLinkClient* GetLiveLinkClient() const
	{
		return LiveLinkClient;
	}

	FGuid GetGuid() const
	{
		return SourceGuid;
	}

private:

	ILiveLinkClient* LiveLinkClient;
	FGuid SourceGuid;
};

/**
 * Component that can be used to track positions in a Skel Mesh every frame, and publish them as a Live Link subject.
 */
UCLASS(BlueprintType, Blueprintable, Category="Live Stream Animation|Live Link", Meta=(BlueprintSpawnableComponent))
class LSALIVELINK_API ULiveLinkTestSkelMeshTrackerComponent : public UActorComponent, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	ULiveLinkTestSkelMeshTrackerComponent();

	/**
	 * Start tracking the specified 
	 */
	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	void StartTrackingSkelMesh(FName InSubjectName);

	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	void StopTrackingSkelMesh();

	//~ Begin ActorComponent Interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End ActorComponent Interface

	//~ Begin IBoneReferenceSkeletonProvider Interface
	virtual class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError) override;
	//~ End IBoneReferenceSkeletonProviderInterface

private:

	class ILiveLinkClient* GetLiveLinkClient() const;

	FLiveLinkSubjectKey GetSubjectKey() const;

	UPROPERTY(EditAnywhere, Category = "Live Stream Animation|Live Link")
	FName TranslationProfile;

	// The SkeletalMeshComponent that we are going to track.
	UPROPERTY(EditAnywhere, Category = "Live Stream Animation|Live Link", meta = (UseComponentPicker, AllowedClasses = "SkeletalMeshComponent", AllowPrivateAccess="True"))
	FComponentReference SkelMeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Live Stream Animation|Live Link", meta = (AllowPrivateAccess = "True"))
	TWeakObjectPtr<USkeletalMeshComponent> WeakSkelMeshComp;

	// When non-empty, this is the set of bones that we want to track.
	// Mocap typically will only track a subset of bones, and this lets us replicate that behavior.
	// This needs to be set before StartTrackingSkelMesh is called (or after StopTrackingSkelMesh is called).
	UPROPERTY(EditDefaultsOnly, Category = "Live Stream Animation|Live Link", Meta = (AllowPrivateAccess = "true"))
	TArray<FBoneReference> BonesToTrack;

	TWeakObjectPtr<USkeletalMeshComponent> UsingSkelMeshComp;
	TSet<int32> UsingBones;

	// The Subject Name that the tracked Skel Mesh will be published as to Live Link.
	FLiveLinkSubjectName SubjectName;

	// The LiveLink source that we created to track the skeleton.
	// May become invalid if it is forcibly removed from Live Link.
	TWeakPtr<const FLSALiveLinkSkelMeshSource> Source;

	// If BonesToTrack is non-empty and has at least one valid bone, then we will populate this array
	// with the correct bone indices so we can quickly scape 
	TArray<int32> BoneIndicesToTrack;

	USkeletalMeshComponent* GetSkelMeshComp() const;
};
// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "LiveLinkTypes.h"
#include "Components/ActorComponent.h"
#include "SkelMeshToLiveLinkSource.generated.h"

namespace LiveStreamAnimation
{
	class FSkelMeshToLiveLinkSource : public ILiveLinkSource
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
}

UCLASS(BlueprintType, Blueprintable, Category="Live Stream Animation|Live Link")
class LIVESTREAMANIMATION_API ULiveLinkTestSkelMeshTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	ULiveLinkTestSkelMeshTrackerComponent();

	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	void StartTrackingSkelMesh(class USkeletalMeshComponent* InSkelMeshComp, FName InSubjectName);

	void StopTrackingSkelMesh();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	class ILiveLinkClient* GetLiveLinkClient() const;

	FLiveLinkSubjectKey GetSubjectKey() const;

	UPROPERTY()
	USkeletalMeshComponent* SkelMeshComp;

	FLiveLinkSubjectName SubjectName;

	TWeakPtr<const LiveStreamAnimation::FSkelMeshToLiveLinkSource> Source;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveStreamAnimationDataHandler.h"
#include "LiveStreamAnimationFwd.h"
#include "LSALiveLinkSourceOptions.h"
#include "LSALiveLinkDataHandler.generated.h"

UCLASS(BlueprintType, Category = "Live Stream Animation|Live Link")
class LSALIVELINK_API ULSALiveLinkDataHandler : public ULiveStreamAnimationDataHandler
{
	GENERATED_BODY()

public:

	//~ Begin LiveStreamAnimationDataHandler Interface
	virtual void OnStartup() override;
	virtual void OnShutdown() override;
	virtual void OnPacketReceived(const TArrayView<const uint8> ReceivedPacket) override;
	virtual void OnAnimationRoleChanged(const ELiveStreamAnimationRole NewRole) override;
	virtual void GetJoinInProgressPackets(TArray<TArray<uint8>>& OutPackets) override;
	//~ End LiveStreamAnimationDataHandler Interface

	/**
	 * Start tracking a Live Link subject that is active on this machine, serializing its data
	 * to animation packets, and forward those to others connections.
	 * Requires Animation Tracking to be enabled.
	 *
	 * The Registered Name passed in *must* be available / configured in the AllowedRegisteredNames
	 * list, and that list is expected to be the same on all instances.
	 *
	 * @param LiveLinkSubject		The Live Link Subject that we are pulling animation data from locally.
	 *
	 * @param RegisteredName		The registered Live Link Subject name that will be used for clients
	 *								evaluating animation data remotely.
	 *								This name must be present in the HandleNames list.
	 *
	 * @param Options				Options describing the type of data we will track and send.
	 *
	 * @param TranslationProfile	The Translation Profile that we should use for this subject.
	 *								This name must be present in the HandleNames list, otherwise the translation will not
	 *								be applied.
	 *								@see ULSALiveLinkFrameTranslator.
	 *
	 * @return Whether or not we successfully registered the subject for tracking.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	bool StartTrackingLiveLinkSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandleWrapper RegisteredName,
		const FLSALiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandleWrapper TranslationProfile);

	bool StartTrackingLiveLinkSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandle RegisteredName,
		const FLSALiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandle TranslationProfile);

	/**
	 * Stop tracking a Live Link subject.
	 *
	 * @param RegisteredName		The registered remote name for the Live Link Subject.
	 */
	UFUNCTION(BlueprintCallable, Category = "Live Stream Animation|Live Link")
	void StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandleWrapper RegisteredName);
	void StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandle RegisteredName);

	TSharedPtr<const class FLSALiveLinkSkelMeshSource> GetOrCreateLiveLinkSkelMeshSource();

private:

	TSharedPtr<class FLSALiveLinkStreamingHelper> LiveLinkStreamingHelper;
};
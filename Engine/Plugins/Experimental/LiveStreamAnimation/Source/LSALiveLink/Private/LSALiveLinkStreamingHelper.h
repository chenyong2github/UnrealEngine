// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "LiveStreamAnimationHandle.h"
#include "LSALiveLinkSourceOptions.h"
#include "LSALiveLinkFrameTranslator.h"

#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationTypes.h"

class FLSALiveLinkStreamingHelper
{
private:

	struct FLiveLinkTrackedSubject
	{
		/** The actual Live Link subject we're reading frames from. */
		FLiveLinkSubjectName LiveLinkSubject;

		/** Streaming handle that we'll use to refer to this subject over the network. */
		FLiveStreamAnimationHandle SubjectHandle;

		//~ TODO: This could be rolled up into the translation settings.
		//~			That would also allow us to not need to send these all the time.

		/** Options used for animation frame updates. */
		FLSALiveLinkSourceOptions Options;

		/** Translation profile we will use for this subject. */
		FLiveStreamAnimationHandle TranslationHandle;

		/** The last sent skeleton data. */
		FLiveLinkSkeletonStaticData LastKnownSkeleton;

		FDelegateHandle StaticDataReceivedHandle;
		FDelegateHandle FrameDataReceivedHandle;

		bool ReceivedFrameData(const FLiveLinkAnimationFrameData& AnimationData, FLiveLinkAnimationFrameData& OutAnimationData) const;
		bool ReceivedStaticData(const FLiveLinkSkeletonStaticData& SkeletonData);

		FString ToString() const;

		static FLiveLinkTrackedSubject CreateFromReceivedPacket(
			FLiveLinkSubjectName InLiveLinkSubject,
			FLiveStreamAnimationHandle InSubjectHandle,
			const FLiveLinkSkeletonStaticData& InSkeleton);

		static FLiveLinkTrackedSubject CreateFromTrackingRequest(
			FLiveLinkSubjectName InLiveLinkSubject,
			FLiveStreamAnimationHandle InSubjectHandle,
			FLSALiveLinkSourceOptions InOptions,
			FLiveStreamAnimationHandle InTranslationHandle,
			FDelegateHandle InStaticDataReceivedHandle,
			FDelegateHandle InFrameDataReceivedHandle
			);

	private:

		FLiveLinkTrackedSubject()
		{
		}

		TOptional<FLSALiveLinkTranslationProfile> TranslationProfile;
		TArray<int32> BoneTranslations;
	};

public:

	FLSALiveLinkStreamingHelper(class ULSALiveLinkDataHandler& InDataHandler);

	~FLSALiveLinkStreamingHelper();

	void OnPacketReceived(const TArrayView<const uint8> PacketData);
	void OnAnimationRoleChanged(ELiveStreamAnimationRole NewRole);
	void GetJoinInProgressPackets(TArray<TArray<uint8>>& JoinInProgressPackets);

	bool StartTrackingLiveLinkSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandle SubjectHandle,
		const FLSALiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandle TranslationHandle);

	void StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandle SubjectHandle);

	TSharedPtr<const class FLSALiveLinkSkelMeshSource> GetOrCreateLiveLinkSkelMeshSource();

private:

	void StartProcessingPackets();
	void StopProcessingPackets();
	void RemoveAllSubjects();

	void ReceivedStaticData(
		FLiveLinkSubjectKey InSubjectKey,
		TSubclassOf<class ULiveLinkRole> InSubjectRole,
		const FLiveLinkStaticDataStruct& InStaticData,
		const FLiveStreamAnimationHandle SubjectHandle);

	void ReceivedFrameData(
		FLiveLinkSubjectKey InSubjectKey,
		TSubclassOf<class ULiveLinkRole> InSubjectRole,
		const FLiveLinkFrameDataStruct& InFrameData,
		const FLiveStreamAnimationHandle SubjectHandle);

	bool SendPacketToServer(TUniquePtr<class FLSALiveLinkPacket>&& PacketToSend);

	TUniquePtr<class FLSALiveLinkPacket> CreateAddOrUpdateSubjectPacket(const FLiveLinkTrackedSubject& Subject);

	TUniquePtr<class FLSALiveLinkPacket> CreateRemoveSubjectPacket(const FLiveLinkTrackedSubject& Subject);

	TUniquePtr<class FLSALiveLinkPacket> CreateAnimationFramePacket(const FLiveLinkTrackedSubject& Subject, FLiveLinkAnimationFrameData&& AnimationData);

	void OnFrameTranslatorChanged();

	static class ILiveLinkClient* GetLiveLinkClient();

	TSharedPtr<class FLSALiveLinkSkelMeshSource> SkelMeshToLiveLinkSource;
	TSharedPtr<class FLSALiveLinkSource> LiveLinkSource;
	TMap<FLiveStreamAnimationHandle, FLiveLinkTrackedSubject> TrackedSubjects;
	class ULSALiveLinkDataHandler& DataHandler;
	FDelegateHandle OnRoleChangedHandle;
	FDelegateHandle OnFrameTranslatorChangedHandle;
};
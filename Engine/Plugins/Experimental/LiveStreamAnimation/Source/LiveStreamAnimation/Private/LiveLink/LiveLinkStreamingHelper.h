// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveStreamAnimationHandle.h"
#include "LiveLink/LiveStreamAnimationLiveLinkSourceOptions.h"

class ULiveLinkRole;
class ULiveStreamAnimationSubsystem;

namespace LiveStreamAnimation
{
	class FSkelMeshToLiveLinkSource;

	class FLiveLinkStreamingHelper
	{
	private:

		struct FLiveLinkTrackedSubject
		{
			/** The actual Live Link subject we're reading frames from. */
			FLiveLinkSubjectName LiveLinkSubject;

			/** Streaming handle that we'll use to refer to this subject over the network. */
			FLiveStreamAnimationHandle SubjectHandle;

			/** Options used for animation frame updates. */
			FLiveStreamAnimationLiveLinkSourceOptions Options;

			/** Translation profile we will use for this subject. */
			FLiveStreamAnimationHandle TranslationHandle;

			/** The last sent skeleton data. */
			FLiveLinkSkeletonStaticData LastKnownSkeleton;

			FDelegateHandle StaticDataReceivedHandle;
			FDelegateHandle FrameDataReceivedHandle;

			FString ToString() const
			{
				return FString::Printf(TEXT("LiveLinkSubject = %s, SubjectHandle = %s"),
					*LiveLinkSubject.ToString(), *SubjectHandle.ToString());
			}
		};

	public:

		FLiveLinkStreamingHelper(ULiveStreamAnimationSubsystem& InSubsystem);

		~FLiveLinkStreamingHelper();

		void HandleLiveLinkPacket(const TSharedRef<const FLiveStreamAnimationPacket>& Packet);

		void StartProcessingPackets();

		void StopProcessingPackets();

		bool StartTrackingSubject(
			const FName LiveLinkSubject,
			const FLiveStreamAnimationHandle SubjectHandle,
			const FLiveStreamAnimationLiveLinkSourceOptions Options,
			const FLiveStreamAnimationHandle TranslationHandle);

		void StopTrackingSubject(const FLiveStreamAnimationHandle SubjectHandle);

		void RemoveAllSubjects();

		void GetJoinInProgressPackets(TArray<TSharedRef<FLiveStreamAnimationPacket>>& JoinInProgressPackets);

		TWeakPtr<const LiveStreamAnimation::FSkelMeshToLiveLinkSource> GetOrCreateSkelMeshToLiveLinkSource();

	private:

		void ReceivedStaticData(
			FLiveLinkSubjectKey InSubjectKey,
			TSubclassOf<ULiveLinkRole> InSubjectRole,
			const FLiveLinkStaticDataStruct& InStaticData,
			const FLiveStreamAnimationHandle SubjectHandle);

		void ReceivedFrameData(
			FLiveLinkSubjectKey InSubjectKey,
			TSubclassOf<ULiveLinkRole> InSubjectRole,
			const FLiveLinkFrameDataStruct& InFrameData,
			const FLiveStreamAnimationHandle SubjectHandle);

		bool SendPacketToServer(const TSharedPtr<FLiveStreamAnimationPacket>& PacketToSend);

		TSharedPtr<FLiveStreamAnimationPacket> CreateAddOrUpdateSubjectPacket(const FLiveLinkTrackedSubject& Subject);

		TSharedPtr<FLiveStreamAnimationPacket> CreateRemoveSubjectPacket(const FLiveLinkTrackedSubject& Subject);

		TSharedPtr<FLiveStreamAnimationPacket> CreateAnimationFramePacket(const FLiveLinkTrackedSubject& Subject, FLiveLinkAnimationFrameData&& AnimationData);

		void OnRoleChanged(ELiveStreamAnimationRole NewRole);
		void OnFrameTranslatorChanged();

		TSharedPtr<FSkelMeshToLiveLinkSource> SkelMeshToLiveLinkSource;
		TSharedPtr<FLiveStreamAnimationLiveLinkSource> LiveLinkSource;
		TMap<FLiveStreamAnimationHandle, FLiveLinkTrackedSubject> TrackedSubjects;
		ULiveStreamAnimationSubsystem& Subsystem;
		FDelegateHandle OnRoleChangedHandle;
		FDelegateHandle OnFrameTranslatorChangedHandle;
	};
}
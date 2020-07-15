// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveLinkStreamingHelper.h"

#include "LiveStreamAnimationLog.h"
#include "LiveStreamAnimationSubsystem.h"
#include "LiveStreamAnimationPacket.h"
#include "LiveLink/LiveLinkPacket.h"
#include "LiveLink/LiveStreamAnimationLiveLinkSource.h"
#include "LiveLink/Test/SkelMeshToLiveLinkSource.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "ILiveLinkClient.h"
#include "Serialization/MemoryReader.h"
#include "Features/IModularFeatures.h"
#include "CoreGlobals.h"

namespace LiveStreamAnimation
{
	static ILiveLinkClient* GetLiveLinkClient()
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (!ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			UE_LOG(LogLiveStreamAnimation, Error, TEXT("GetLiveLinkClient: Live Link Unavailable."));
			return nullptr;
		}

		return &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}

	FLiveLinkStreamingHelper::FLiveLinkStreamingHelper(ULiveStreamAnimationSubsystem& InSubsystem)
		: Subsystem(InSubsystem)
		, OnRoleChangedHandle(Subsystem.GetOnRoleChanged().AddRaw(this, &FLiveLinkStreamingHelper::OnRoleChanged))
		, OnFrameTranslatorChangedHandle(Subsystem.GetOnLiveLinkFrameTranslatorChanged().AddRaw(this, &FLiveLinkStreamingHelper::OnFrameTranslatorChanged))
	{
		if (ELiveStreamAnimationRole::Processor == Subsystem.GetRole())
		{
			StartProcessingPackets();
		}
	}

	FLiveLinkStreamingHelper::~FLiveLinkStreamingHelper()
	{
		RemoveAllSubjects();
		StopProcessingPackets();

		if (SkelMeshToLiveLinkSource.IsValid())
		{
			if (!IsEngineExitRequested())
			{
				if (ILiveLinkClient* LiveLinkClient = GetLiveLinkClient())
				{
					LiveLinkClient->RemoveSource(SkelMeshToLiveLinkSource);
				}
			}
		}

		Subsystem.GetOnRoleChanged().Remove(OnRoleChangedHandle);
		Subsystem.GetOnLiveLinkFrameTranslatorChanged().Remove(OnFrameTranslatorChangedHandle);
	}

	void FLiveLinkStreamingHelper::HandleLiveLinkPacket(const TSharedRef<const FLiveStreamAnimationPacket>& Packet)
	{
		// TODO: We could probably add a way to peak Live Link Packet Type
		//			and just ignore Animation updates if we aren't going to
		//			process them, since we don't need to keep those records
		//			up to date.
		//			This could help perf, especially since non-animation updates
		//			would be rare.

		FMemoryReaderView Reader(Packet->GetPacketData());
		TUniquePtr<FLiveLinkPacket> LiveLinkPacketUniquePtr(FLiveLinkPacket::ReadFromStream(Reader));

		if (FLiveLinkPacket* LiveLinkPacket = LiveLinkPacketUniquePtr.Get())
		{
			if (FLiveStreamAnimationLiveLinkSource* LocalLiveLinkSource = LiveLinkSource.Get())
			{
				LocalLiveLinkSource->HandlePacket(MoveTemp(*LiveLinkPacket));
			}

			const FLiveStreamAnimationHandle SubjectHandle = LiveLinkPacket->GetSubjectHandle();

			// Now, update our records.
			switch (LiveLinkPacket->GetPacketType())
			{
			case ELiveLinkPacketType::RemoveSubject:
				TrackedSubjects.Remove(SubjectHandle);
				break;

			case ELiveLinkPacketType::AddOrUpdateSubject:
				{
					const FLiveLinkAddOrUpdateSubjectPacket& CastedPacket = static_cast<const FLiveLinkAddOrUpdateSubjectPacket&>(*LiveLinkPacket);
					if (FLiveLinkTrackedSubject * FoundSubject = TrackedSubjects.Find(SubjectHandle))
					{
						FoundSubject->LastKnownSkeleton = CastedPacket.GetStaticData();
					}
					else
					{
						FLiveLinkTrackedSubject NewSubject{
							SubjectHandle.GetName(),						// For processors and proxies, we don't care about the originating Live Link name.
																			// Instead we use the associated handle name.
							SubjectHandle,
							FLiveStreamAnimationLiveLinkSourceOptions(),	// It's OK to use the default options here, because
																			// we won't actually be tracking anim, just forwarding them.

							FLiveStreamAnimationHandle(),
							CastedPacket.GetStaticData()
						};

						TrackedSubjects.Add(SubjectHandle, NewSubject);
					}
				}

				break;
				
			default:
				break;
			}
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::HandleLiveLinkPacket: Received invalid Live Link Packet!"));
		}
	}

	void FLiveLinkStreamingHelper::StartProcessingPackets()
	{
		if (!LiveLinkSource.IsValid())
		{
			if (ILiveLinkClient* LiveLinkClient = GetLiveLinkClient())
			{
				LiveLinkSource = MakeShared<FLiveStreamAnimationLiveLinkSource>(Subsystem.GetLiveLinkFrameTranslator());
				LiveLinkClient->AddSource(StaticCastSharedPtr<ILiveLinkSource>(LiveLinkSource));

				// If we've already received data, go ahead and get our Source back up to date.
				for (auto It = TrackedSubjects.CreateIterator(); It; ++It)
				{
					const FLiveLinkTrackedSubject& TrackedSubject = It.Value();

					TUniquePtr<FLiveLinkPacket> Packet = FLiveLinkAddOrUpdateSubjectPacket::CreatePacket(
						TrackedSubject.SubjectHandle,
						FLiveLinkSkeletonStaticData(TrackedSubject.LastKnownSkeleton));
						
					if (Packet.IsValid())
					{
						LiveLinkSource->HandlePacket(MoveTemp(*Packet));
					}
				}
			}
		}
	}

	void FLiveLinkStreamingHelper::StopProcessingPackets()
	{
		if (!IsEngineExitRequested())
		{
			if (LiveLinkSource.IsValid())
			{
				if (ILiveLinkClient* LiveLinkClient = GetLiveLinkClient())
				{
					LiveLinkClient->RemoveSource(StaticCastSharedPtr<ILiveLinkSource>(LiveLinkSource));
				}
			}
		}
	}

	bool FLiveLinkStreamingHelper::StartTrackingSubject(
		const FName LiveLinkSubject,
		const FLiveStreamAnimationHandle SubjectHandle,
		const FLiveStreamAnimationLiveLinkSourceOptions Options,
		const FLiveStreamAnimationHandle TranslationHandle)
	{
		if (LiveLinkSubject == NAME_None)
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Invalid LiveLinkSubject."));
			return false;
		}

		if (!SubjectHandle.IsValid())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Invalid SubjectHandle."));
			return false;
		}

		if (!Options.IsValid())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Invalid Options."));
			return false;
		}

		if (FLiveLinkTrackedSubject* ExistingSubject = TrackedSubjects.Find(SubjectHandle))
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Subject is already tracked. ExistingSubject = (%s)"),
				*ExistingSubject->ToString());

			return ExistingSubject->LiveLinkSubject == LiveLinkSubject;
		}

		if (ILiveLinkClient * LiveLinkClient = GetLiveLinkClient())
		{
			FLiveLinkSubjectName LiveLinkSubjectName(LiveLinkSubject);

			FDelegateHandle StaticDataReceivedHandle;
			FOnLiveLinkSubjectStaticDataAdded::FDelegate OnStaticDataReceived;
			OnStaticDataReceived.BindRaw(this, &FLiveLinkStreamingHelper::ReceivedStaticData, SubjectHandle);

			FDelegateHandle FrameDataReceivedHandle;
			FOnLiveLinkSubjectFrameDataAdded::FDelegate OnFrameDataReceived;
			OnFrameDataReceived.BindRaw(this, &FLiveLinkStreamingHelper::ReceivedFrameData, SubjectHandle);

			TSubclassOf<ULiveLinkRole> SubjectRole;
			FLiveLinkStaticDataStruct StaticData;

			bool bSuccess = false;

			const bool bWasRegistered = LiveLinkClient->RegisterForSubjectFrames(
				LiveLinkSubjectName,
				OnStaticDataReceived,
				OnFrameDataReceived,
				StaticDataReceivedHandle,
				FrameDataReceivedHandle,
				SubjectRole,
				&StaticData);

			FLiveLinkTrackedSubject TrackedSubject{
				LiveLinkSubjectName,
				SubjectHandle,
				Options,
				TranslationHandle,
				FLiveLinkSkeletonStaticData(),
				StaticDataReceivedHandle,
				FrameDataReceivedHandle,
			};

			if (bWasRegistered)
			{
				if (ULiveLinkAnimationRole::StaticClass() != SubjectRole)
				{
					UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Subject had invalid role, subject won't be sent. Subject = (%s), Role = %s"),
						*TrackedSubject.ToString(), *GetPathNameSafe(SubjectRole.Get()));
				}
				else
				{
					if (const FLiveLinkSkeletonStaticData* SkeletonDataPtr = StaticData.Cast<FLiveLinkSkeletonStaticData>())
					{
						TrackedSubject.LastKnownSkeleton = *SkeletonDataPtr;
					}

					if (SendPacketToServer(CreateAddOrUpdateSubjectPacket(TrackedSubject)))
					{
						bSuccess = true;
						TrackedSubjects.Add(SubjectHandle, TrackedSubject);
					}
					else
					{
						UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Failed to send add subject packet. Subject = (%s)"),
							*TrackedSubject.ToString());
					}
				}

				if (!bSuccess)
				{
					LiveLinkClient->UnregisterSubjectFramesHandle(TrackedSubject.LiveLinkSubject, TrackedSubject.StaticDataReceivedHandle, TrackedSubject.FrameDataReceivedHandle);
				}
			}
			else
			{
				UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StartTrackingSubject: Failed to register subject. Subject = (%s)"),
					*TrackedSubject.ToString());
			}

			return bSuccess;
		}

		return false;
	}

	void FLiveLinkStreamingHelper::StopTrackingSubject(const FLiveStreamAnimationHandle SubjectHandle)
	{
		FLiveLinkTrackedSubject TrackedSubject;
		if (TrackedSubjects.RemoveAndCopyValue(SubjectHandle, TrackedSubject))
		{
			if (ILiveLinkClient* LiveLinkClient = GetLiveLinkClient())
			{
				LiveLinkClient->UnregisterSubjectFramesHandle(TrackedSubject.LiveLinkSubject, TrackedSubject.StaticDataReceivedHandle, TrackedSubject.FrameDataReceivedHandle);
				if (!SendPacketToServer(CreateRemoveSubjectPacket(TrackedSubject)))
				{
					UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StopTrackingSubject: Failed to send remove packet to server. Subject = (%s)"),
						*TrackedSubject.ToString());
				}
			}
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::StopTrackingSubject: Unable to find subject. SubjectHandle = %s"),
				*SubjectHandle.ToString());
		}
	}

	void FLiveLinkStreamingHelper::RemoveAllSubjects()
	{
		if (!IsEngineExitRequested())
		{
			if (ILiveLinkClient * LiveLinkClient = GetLiveLinkClient())
			{
				for (auto It = TrackedSubjects.CreateIterator(); It; ++It)
				{
					// Don't send packets at this point, because we're shutting the subsystem down and any
					// channels should have been closed already.
					const FLiveLinkTrackedSubject& TrackedSubject = It.Value();
					LiveLinkClient->UnregisterSubjectFramesHandle(TrackedSubject.LiveLinkSubject, TrackedSubject.StaticDataReceivedHandle, TrackedSubject.FrameDataReceivedHandle);
				}
			}

			TrackedSubjects.Empty();
		}
	}

	void FLiveLinkStreamingHelper::GetJoinInProgressPackets(TArray<TSharedRef<FLiveStreamAnimationPacket>>& JoinInProgressPackets)
	{
		JoinInProgressPackets.Reserve(JoinInProgressPackets.Num() + TrackedSubjects.Num());

		for (auto It = TrackedSubjects.CreateIterator(); It; ++It)
		{
			// We send these packets separately, in case the connection already had the subject registered
			// but the skeleton changed since they were connected.
			FLiveLinkTrackedSubject& TrackedSubject = It.Value();
			TSharedPtr<FLiveStreamAnimationPacket> AddOrUpdateSubjectPacket = CreateAddOrUpdateSubjectPacket(TrackedSubject);
			if (AddOrUpdateSubjectPacket.IsValid())
			{
				JoinInProgressPackets.Add(AddOrUpdateSubjectPacket.ToSharedRef());
			}
		}
	}

	void FLiveLinkStreamingHelper::ReceivedStaticData(
		FLiveLinkSubjectKey InSubjectKey,
		TSubclassOf<ULiveLinkRole> InSubjectRole,
		const FLiveLinkStaticDataStruct& InStaticData,
		const FLiveStreamAnimationHandle SubjectHandle)
	{
		if (FLiveLinkTrackedSubject* TrackedSubject = TrackedSubjects.Find(SubjectHandle))
		{
			bool bSentPacket = false;
			if (const FLiveLinkSkeletonStaticData* StaticData = InStaticData.Cast<const FLiveLinkSkeletonStaticData>())
			{
				TrackedSubject->LastKnownSkeleton = *StaticData;
				bSentPacket = SendPacketToServer(CreateAddOrUpdateSubjectPacket(*TrackedSubject));
			}

			if (!bSentPacket)
			{
				UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::ReceivedStaticData: Failed to send static data packet to server. Subject = (%s)"),
					*TrackedSubject->ToString());
			}
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::ReceivedStaticData: Failed to find registered subject. SubjectHandle = (%s)"),
				*SubjectHandle.ToString());
		}
	}

	void FLiveLinkStreamingHelper::ReceivedFrameData(
		FLiveLinkSubjectKey InSubjectKey,
		TSubclassOf<ULiveLinkRole> InSubjectRole,
		const FLiveLinkFrameDataStruct& InFrameData,
		const FLiveStreamAnimationHandle SubjectHandle)
	{
		if (const FLiveLinkTrackedSubject* TrackedSubject = TrackedSubjects.Find(SubjectHandle))
		{
			bool bSentPacket = false;
			if (const FLiveLinkAnimationFrameData * AnimationData = InFrameData.Cast<FLiveLinkAnimationFrameData>())
			{
				bSentPacket = SendPacketToServer(CreateAnimationFramePacket(*TrackedSubject, FLiveLinkAnimationFrameData(*AnimationData)));
			}

			if (!bSentPacket)
			{
				UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::ReceivedFrameData: Failed to send anim packet to server. Subject = (%s)"),
					*TrackedSubject->ToString());
			}
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveLinkStreamingHelper::ReceivedFrameData: Failed to find registered subject. SubjectHandle = (%s)"),
				*SubjectHandle.ToString());
		}
	}

	bool FLiveLinkStreamingHelper::SendPacketToServer(const TSharedPtr<FLiveStreamAnimationPacket>& PacketToSend)
	{
		if (PacketToSend.IsValid())
		{
			Subsystem.SendPacketToServer(PacketToSend.ToSharedRef());
			return true;
		}

		return false;
	}

	static TSharedPtr<FLiveStreamAnimationPacket> WrapLiveLinkPacket(
		const TUniquePtr<FLiveLinkPacket>& PacketToSend,
		const bool bReliable)
	{
		TSharedPtr<FLiveStreamAnimationPacket> AnimPacket;

		if (PacketToSend.IsValid())
		{
			AnimPacket = FLiveStreamAnimationPacket::CreateFromPacket(*PacketToSend);
			if (AnimPacket.IsValid())
			{
				AnimPacket->SetReliable(bReliable);
			}
		}

		return AnimPacket;
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveLinkStreamingHelper::CreateAddOrUpdateSubjectPacket(const FLiveLinkTrackedSubject& Subject)
	{
		return WrapLiveLinkPacket(
			FLiveLinkAddOrUpdateSubjectPacket::CreatePacket(
				Subject.SubjectHandle,
				FLiveLinkSkeletonStaticData(Subject.LastKnownSkeleton)),
			true);
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveLinkStreamingHelper::CreateRemoveSubjectPacket(const FLiveLinkTrackedSubject& Subject)
	{
		return WrapLiveLinkPacket(
			FLiveLinkRemoveSubjectPacket::CreatePacket(Subject.SubjectHandle),
			true);
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveLinkStreamingHelper::CreateAnimationFramePacket(const FLiveLinkTrackedSubject& Subject, FLiveLinkAnimationFrameData&& AnimationData)
	{
		return WrapLiveLinkPacket(
			FLiveLinkAnimationFramePacket::CreatePacket(
				Subject.SubjectHandle,
				FLiveStreamAnimationLiveLinkFrameData(MoveTemp(AnimationData), Subject.Options, Subject.TranslationHandle)),
			false);
	}

	void FLiveLinkStreamingHelper::OnRoleChanged(ELiveStreamAnimationRole NewRole)
	{
		if (ELiveStreamAnimationRole::Processor == NewRole)
		{
			StartProcessingPackets();
		}
		else
		{
			StopProcessingPackets();
		}
	}

	void FLiveLinkStreamingHelper::OnFrameTranslatorChanged()
	{
		if (FLiveStreamAnimationLiveLinkSource* LocalSource = LiveLinkSource.Get())
		{
			LocalSource->SetFrameTranslator(Subsystem.GetLiveLinkFrameTranslator());
		}
	}

	TWeakPtr<const FSkelMeshToLiveLinkSource> FLiveLinkStreamingHelper::GetOrCreateSkelMeshToLiveLinkSource()
	{
		if (ELiveStreamAnimationRole::Tracker == Subsystem.GetRole())
		{
			if (!SkelMeshToLiveLinkSource.IsValid())
			{
				if (ILiveLinkClient* LiveLinkClient = GetLiveLinkClient())
				{
					SkelMeshToLiveLinkSource = MakeShared<FSkelMeshToLiveLinkSource>();
					LiveLinkClient->AddSource(SkelMeshToLiveLinkSource);
				}
			}

			return SkelMeshToLiveLinkSource;
		}

		return nullptr;
	}
};

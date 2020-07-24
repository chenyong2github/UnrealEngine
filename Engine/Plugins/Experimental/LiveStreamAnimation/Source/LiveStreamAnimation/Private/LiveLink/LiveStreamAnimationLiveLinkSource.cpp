// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveStreamAnimationLiveLinkSource.h"
#include "LiveStreamAnimationLog.h"
#include "LiveLink/LiveLinkPacket.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "LiveLink/LiveStreamAnimationLiveLinkRole.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveLinkPresetTypes.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSubjectSettings.h"

namespace LiveStreamAnimation
{
	FLiveStreamAnimationLiveLinkSource::FLiveStreamAnimationLiveLinkSource(ULiveStreamAnimationLiveLinkFrameTranslator* InTranslator)
		: FrameTranslator(InTranslator)
	{
	}

	void FLiveStreamAnimationLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
	{
		LiveLinkClient = InClient;
		SourceGuid = InSourceGuid;
	}

	void FLiveStreamAnimationLiveLinkSource::Update()
	{
	}

	bool FLiveStreamAnimationLiveLinkSource::CanBeDisplayedInUI() const
	{
		return false;
	}

	bool FLiveStreamAnimationLiveLinkSource::IsSourceStillValid() const
	{
		// TODO: Maybe allow a way for users to test and see if we still are still connected to our server somehow?
		return true;
	}

	bool FLiveStreamAnimationLiveLinkSource::RequestSourceShutdown()
	{
		UE_LOG(LogLiveStreamAnimation, Log, TEXT("FLiveStreamAnimationLiveLinkSource::RequestSourceShutdown"));
		Reset();
		return true;
	}

	FText FLiveStreamAnimationLiveLinkSource::GetSourceType() const
	{
		return NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceType", "Live Stream Animation Source");
	}

	FText FLiveStreamAnimationLiveLinkSource::GetSourceMachineName() const
	{
		// TODO: Maybe allow a user provided name somehow?
		return NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceMachineNameNetworked", "Live Stream Animation Network");
	}

	FText FLiveStreamAnimationLiveLinkSource::GetSourceStatus() const
	{
		static FText ConnectedText = NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceState_Connected", "Connected");
		static FText DisconnectedText = NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceState_Disconnected", "Disconnected");

		return bIsConnectedToMesh ? ConnectedText : DisconnectedText;
	}

	void FLiveStreamAnimationLiveLinkSource::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(FrameTranslator);
	}

	void FLiveStreamAnimationLiveLinkSource::Reset()
	{
		SourceGuid = FGuid();
		LiveLinkClient = nullptr;
	}

	bool FLiveStreamAnimationLiveLinkSource::HandlePacket(FLiveLinkPacket&& InPacket)
	{
		if (LiveLinkClient)
		{
			switch (InPacket.GetPacketType())
			{
			case ELiveLinkPacketType::AddOrUpdateSubject:
				return HandleAddOrUpdateSubjectPacket(static_cast<FLiveLinkAddOrUpdateSubjectPacket&&>(MoveTemp(InPacket)));

			case ELiveLinkPacketType::RemoveSubject:
				return HandleRemoveSubjectPacket(static_cast<FLiveLinkRemoveSubjectPacket&&>(MoveTemp(InPacket)));

			case ELiveLinkPacketType::AnimationFrame:
				return HandleAnimationFramePacket(static_cast<FLiveLinkAnimationFramePacket&&>(MoveTemp(InPacket)));

			default:
				UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveStreamAnimationLiveLinkSource::HandlePacket: Invalid packet type %d"), static_cast<int32>(InPacket.GetPacketType()));
				return false;
			}
		}

		return false;
	}

	bool FLiveStreamAnimationLiveLinkSource::HandleAddOrUpdateSubjectPacket(FLiveLinkAddOrUpdateSubjectPacket&& InPacket)
	{
		const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();

		// If we already mapped this subject, don't do anything but warn.

		// TODO:	We might want to make this a remap / changing of skeleton data, but for
		//			now we'll just assume nothing's changed.
		//			We should also probably listen for removal events from LiveLink directly.

		if (const FLiveLinkSubjectKey* FoundKey = MappedSubjects.Find(Handle))
		{
			UE_LOG(LogLiveStreamAnimation, Warning,
				TEXT("FLiveStreamAnimationLiveLinkSource::HandleAddOrUpdateSubjectPacket: Found existing subject. Handle=%s, FoundSubject=%s"),
				*Handle.ToString(), *FoundKey->SubjectName.ToString());

			return true;
		}
		
		FLiveLinkSubjectKey NewKey;
		NewKey.Source = SourceGuid;
		NewKey.SubjectName = Handle.GetName();

		FLiveLinkSubjectPreset Presets;
		Presets.Key = NewKey;
		Presets.Role = ULiveStreamAnimationLiveLinkRole::StaticClass();
		Presets.bEnabled = true;

		if (FrameTranslator)
		{
			Presets.Settings = NewObject<ULiveLinkSubjectSettings>();
			Presets.Settings->Translators.Add(FrameTranslator);
		}

		if (!LiveLinkClient->CreateSubject(Presets))
		{
			UE_LOG(LogLiveStreamAnimation, Warning,
				TEXT("FLiveStreamAnimationLiveLinkSource::HandleAddOrUpdateSubjectPacket: Failed to create subject. Handle=%s"),
				*Handle.ToString());
			return false;
		}

		UE_LOG(LogLiveStreamAnimation, Log,
			TEXT("FLiveStreamAnimationLiveLinkSource::HandleRemoveSubjectPacket: Added subject to find subject. Handle=%s"),
			*Handle.ToString());

		FLiveLinkStaticDataStruct DataStruct;
		DataStruct.InitializeWith(&InPacket.GetStaticData());
		LiveLinkClient->PushSubjectStaticData_AnyThread(NewKey, Presets.Role, MoveTemp(DataStruct));
		MappedSubjects.Emplace(Handle, NewKey);
		return true;
	}

	bool FLiveStreamAnimationLiveLinkSource::HandleRemoveSubjectPacket(FLiveLinkRemoveSubjectPacket&& InPacket)
	{
		const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();
		FLiveLinkSubjectKey FoundKey;

		if (!MappedSubjects.RemoveAndCopyValue(Handle, FoundKey))
		{
			UE_LOG(LogLiveStreamAnimation, Warning,
				TEXT("FLiveStreamAnimationLiveLinkSource::HandleRemoveSubjectPacket: Failed to find subject. Handle=%s"),
				*Handle.ToString());
			return true;
		}

		UE_LOG(LogLiveStreamAnimation, Log,
			TEXT("FLiveStreamAnimationLiveLinkSource::HandleRemoveSubjectPacket: Failed to find subject. Handle=%s, Subject=%s"),
			*Handle.ToString(), *FoundKey.SubjectName.ToString());

		LiveLinkClient->RemoveSubject_AnyThread(FoundKey);
		return true;
	}

	bool FLiveStreamAnimationLiveLinkSource::HandleAnimationFramePacket(FLiveLinkAnimationFramePacket&& InPacket)
	{
		const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();
		if (const FLiveLinkSubjectKey* FoundKey = MappedSubjects.Find(Handle))
		{
			UE_LOG(LogLiveStreamAnimation, Verbose,
				TEXT("FLiveStreamAnimationLiveLinkSource::HandleAnimationFramePacket: Added animation frame. Handle=%s, Subject=%s"),
				*Handle.ToString(), *FoundKey->SubjectName.ToString());

			FLiveLinkFrameDataStruct Data;
			Data.InitializeWith(&InPacket.GetFrameData());
			LiveLinkClient->PushSubjectFrameData_AnyThread(*FoundKey, MoveTemp(Data));
			return true;
		}

		UE_LOG(LogLiveStreamAnimation, Verbose,
			TEXT("FLiveStreamAnimationLiveLinkSource::HandleAnimationFramePacket: Failed to find subject. Handle=%s"),
			*Handle.ToString());

		return false;
	}

	void FLiveStreamAnimationLiveLinkSource::SetFrameTranslator(ULiveStreamAnimationLiveLinkFrameTranslator* NewFrameTranslator)
	{
		FrameTranslator = NewFrameTranslator;

		// TODO: Update the individual Subjects.
		//			Updating the subjects should be a *very* rare occurrence though, as most
		//			of the time the translator will be set up in Configs or in Blueprints before
		//			we've received any data from the network.
	}
}
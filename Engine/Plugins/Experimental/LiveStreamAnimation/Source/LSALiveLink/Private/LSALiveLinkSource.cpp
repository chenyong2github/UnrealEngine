// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LSALiveLinkSource.h"
#include "LSALiveLinkLog.h"
#include "LSALiveLinkPacket.h"
#include "LSALiveLinkFrameTranslator.h"
#include "LSALiveLinkRole.h"

#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveLinkPresetTypes.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSubjectSettings.h"

FLSALiveLinkSource::FLSALiveLinkSource(ULSALiveLinkFrameTranslator* InTranslator)
	: FrameTranslator(InTranslator)
{
}

void FLSALiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	LiveLinkClient = InClient;
	SourceGuid = InSourceGuid;
}

void FLSALiveLinkSource::Update()
{
}

bool FLSALiveLinkSource::CanBeDisplayedInUI() const
{
	return false;
}

bool FLSALiveLinkSource::IsSourceStillValid() const
{
	// TODO: Maybe allow a way for users to test and see if we still are still connected to our server somehow?
	return true;
}

bool FLSALiveLinkSource::RequestSourceShutdown()
{
	UE_LOG(LogLSALiveLink, Log, TEXT("FLSALiveLinkSource::RequestSourceShutdown"));
	Reset();
	return true;
}

FText FLSALiveLinkSource::GetSourceType() const
{
	return NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceType", "Live Stream Animation Source");
}

FText FLSALiveLinkSource::GetSourceMachineName() const
{
	// TODO: Maybe allow a user provided name somehow?
	return NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceMachineNameNetworked", "Live Stream Animation Network");
}

FText FLSALiveLinkSource::GetSourceStatus() const
{
	static FText ConnectedText = NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceState_Connected", "Connected");
	static FText DisconnectedText = NSLOCTEXT("LiveStreamAnimation", "LiveLinkSourceState_Disconnected", "Disconnected");

	return bIsConnectedToMesh ? ConnectedText : DisconnectedText;
}

void FLSALiveLinkSource::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(FrameTranslator);
}

void FLSALiveLinkSource::Reset()
{
	SourceGuid = FGuid();
	LiveLinkClient = nullptr;
}

bool FLSALiveLinkSource::HandlePacket(FLSALiveLinkPacket&& InPacket)
{
	if (LiveLinkClient)
	{
		switch (InPacket.GetPacketType())
		{
		case ELSALiveLinkPacketType::AddOrUpdateSubject:
			return HandleAddOrUpdateSubjectPacket(static_cast<FLSALiveLinkAddOrUpdateSubjectPacket&&>(MoveTemp(InPacket)));

		case ELSALiveLinkPacketType::RemoveSubject:
			return HandleRemoveSubjectPacket(static_cast<FLSALiveLinkRemoveSubjectPacket&&>(MoveTemp(InPacket)));

		case ELSALiveLinkPacketType::AnimationFrame:
			return HandleAnimationFramePacket(static_cast<FLSALiveLinkAnimationFramePacket&&>(MoveTemp(InPacket)));

		default:
			UE_LOG(LogLSALiveLink, Warning, TEXT("FLSALiveLinkSource::HandlePacket: Invalid packet type %d"), static_cast<int32>(InPacket.GetPacketType()));
			return false;
		}
	}

	return false;
}

bool FLSALiveLinkSource::HandleAddOrUpdateSubjectPacket(FLSALiveLinkAddOrUpdateSubjectPacket&& InPacket)
{
	const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();

	// If we already mapped this subject, don't do anything but warn.

	// TODO:	We might want to make this a remap / changing of skeleton data, but for
	//			now we'll just assume nothing's changed.
	//			We should also probably listen for removal events from LiveLink directly.

	if (const FLiveLinkSubjectKey* FoundKey = MappedSubjects.Find(Handle))
	{
		UE_LOG(LogLSALiveLink, Warning,
			TEXT("FLSALiveLinkSource::HandleAddOrUpdateSubjectPacket: Found existing subject. Handle=%s, FoundSubject=%s"),
			*Handle.ToString(), *FoundKey->SubjectName.ToString());

		return true;
	}
		
	FLiveLinkSubjectKey NewKey;
	NewKey.Source = SourceGuid;
	NewKey.SubjectName = Handle.GetName();

	FLiveLinkSubjectPreset Presets;
	Presets.Key = NewKey;
	Presets.Role = ULSALiveLinkRole::StaticClass();
	Presets.bEnabled = true;

	if (FrameTranslator)
	{
		Presets.Settings = NewObject<ULiveLinkSubjectSettings>();
		Presets.Settings->Translators.Add(FrameTranslator);
	}

	if (!LiveLinkClient->CreateSubject(Presets))
	{
		UE_LOG(LogLSALiveLink, Warning,
			TEXT("FLSALiveLinkSource::HandleAddOrUpdateSubjectPacket: Failed to create subject. Handle=%s"),
			*Handle.ToString());
		return false;
	}

	UE_LOG(LogLSALiveLink, Log,
		TEXT("FLSALiveLinkSource::HandleRemoveSubjectPacket: Added subject to find subject. Handle=%s"),
		*Handle.ToString());

	FLiveLinkStaticDataStruct DataStruct;
	DataStruct.InitializeWith(&InPacket.GetStaticData());
	LiveLinkClient->PushSubjectStaticData_AnyThread(NewKey, Presets.Role, MoveTemp(DataStruct));
	MappedSubjects.Emplace(Handle, NewKey);
	return true;
}

bool FLSALiveLinkSource::HandleRemoveSubjectPacket(FLSALiveLinkRemoveSubjectPacket&& InPacket)
{
	const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();
	FLiveLinkSubjectKey FoundKey;

	if (!MappedSubjects.RemoveAndCopyValue(Handle, FoundKey))
	{
		UE_LOG(LogLSALiveLink, Warning,
			TEXT("FLSALiveLinkSource::HandleRemoveSubjectPacket: Failed to find subject. Handle=%s"),
			*Handle.ToString());
		return true;
	}

	UE_LOG(LogLSALiveLink, Log,
		TEXT("FLSALiveLinkSource::HandleRemoveSubjectPacket: Failed to find subject. Handle=%s, Subject=%s"),
		*Handle.ToString(), *FoundKey.SubjectName.ToString());

	LiveLinkClient->RemoveSubject_AnyThread(FoundKey);
	return true;
}

bool FLSALiveLinkSource::HandleAnimationFramePacket(FLSALiveLinkAnimationFramePacket&& InPacket)
{
	const FLiveStreamAnimationHandle Handle = InPacket.GetSubjectHandle();
	if (const FLiveLinkSubjectKey* FoundKey = MappedSubjects.Find(Handle))
	{
		UE_LOG(LogLSALiveLink, Verbose,
			TEXT("FLSALiveLinkSource::HandleAnimationFramePacket: Added animation frame. Handle=%s, Subject=%s"),
			*Handle.ToString(), *FoundKey->SubjectName.ToString());

		FLiveLinkFrameDataStruct Data;
		Data.InitializeWith(&InPacket.GetFrameData());
		LiveLinkClient->PushSubjectFrameData_AnyThread(*FoundKey, MoveTemp(Data));
		return true;
	}

	UE_LOG(LogLSALiveLink, Verbose,
		TEXT("FLSALiveLinkSource::HandleAnimationFramePacket: Failed to find subject. Handle=%s"),
		*Handle.ToString());

	return false;
}

void FLSALiveLinkSource::SetFrameTranslator(ULSALiveLinkFrameTranslator* NewFrameTranslator)
{
	FrameTranslator = NewFrameTranslator;

	// TODO: Update the individual Subjects.
	//			Updating the subjects should be a *very* rare occurrence though, as most
	//			of the time the translator will be set up in Configs or in Blueprints before
	//			we've received any data from the network.
}
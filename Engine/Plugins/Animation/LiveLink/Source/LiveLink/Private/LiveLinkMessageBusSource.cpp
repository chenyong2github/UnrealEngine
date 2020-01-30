// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkMessageBusSource.h"

#include "ILiveLinkClient.h"
#include "ILiveLinkModule.h"
#include "LiveLinkClient.h"
#include "LiveLinkHeartbeatEmitter.h"
#include "LiveLinkLog.h"
#include "LiveLinkMessageBusDiscoveryManager.h"
#include "LiveLinkMessages.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkSettings.h"
#include "LiveLinkTypes.h"

#include "MessageEndpointBuilder.h"
#include "Misc/App.h"

void FLiveLinkMessageBusSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;
	bIsValid = true;

	for (const TSubclassOf<ULiveLinkRole>& RoleClass : FLiveLinkRoleTrait::GetRoles())
	{
		RoleInstances.Add(RoleClass->GetDefaultObject<ULiveLinkRole>());
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	MessageEndpoint = FMessageEndpoint::Builder(TEXT("LiveLinkMessageBusSource"))
		.Handling<FLiveLinkSubjectDataMessage>(this, &FLiveLinkMessageBusSource::HandleSubjectData)
		.Handling<FLiveLinkSubjectFrameMessage>(this, &FLiveLinkMessageBusSource::HandleSubjectFrame)
		.Handling<FLiveLinkHeartbeatMessage>(this, &FLiveLinkMessageBusSource::HandleHeartbeat)
		.Handling<FLiveLinkClearSubject>(this, &FLiveLinkMessageBusSource::HandleClearSubject)
		.ReceivingOnAnyThread()
		.WithCatchall(this, &FLiveLinkMessageBusSource::InternalHandleMessage);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (ConnectionAddress.IsValid())
	{
		SendConnectMessage();
	}
	else
	{
		ILiveLinkModule::Get().GetMessageBusDiscoveryManager().AddDiscoveryMessageRequest();
		bIsValid = false;
	}

	UpdateConnectionLastActive();
}

void FLiveLinkMessageBusSource::Update()
{
	if (!ConnectionAddress.IsValid())
	{
		FLiveLinkMessageBusDiscoveryManager& DiscoveryManager = ILiveLinkModule::Get().GetMessageBusDiscoveryManager();
		for (const FProviderPollResultPtr Result : DiscoveryManager.GetDiscoveryResults())
		{
			if (Client->GetSourceType(SourceGuid).ToString() == Result->Name)
			{
				ConnectionAddress = Result->Address;
				SourceMachineName = FText::FromString(Result->MachineName);
				MachineTimeOffset = Result->MachineTimeOffset;
				DiscoveryManager.RemoveDiscoveryMessageRequest();
				SendConnectMessage();
				UpdateConnectionLastActive();
				break;
			}
		}
	}
	else
	{
		const double HeartbeatTimeout = GetDefault<ULiveLinkSettings>()->GetMessageBusHeartbeatTimeout();
		const double CurrentTime = FApp::GetCurrentTime();

		bIsValid = CurrentTime - ConnectionLastActive < HeartbeatTimeout;
		if (!bIsValid)
		{
			const double DeadSourceTimeout = GetDefault<ULiveLinkSettings>()->GetMessageBusTimeBeforeRemovingDeadSource();
			if (CurrentTime - ConnectionLastActive > DeadSourceTimeout)
			{
				if (RequestSourceShutdown())
				{
					Client->RemoveSource(SourceGuid);
				}
			}
		}
	}
}

void FLiveLinkMessageBusSource::InternalHandleMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UpdateConnectionLastActive();

	if (!Context->IsValid())
	{
		return;
	}

	UScriptStruct* MessageTypeInfo = Context->GetMessageTypeInfo().Get();
	if (MessageTypeInfo == nullptr)
	{
		return;
	}

	const bool bIsStaticData = MessageTypeInfo->IsChildOf(FLiveLinkBaseStaticData::StaticStruct());
	const bool bIsFrameData = MessageTypeInfo->IsChildOf(FLiveLinkBaseFrameData::StaticStruct());
	if (!bIsStaticData && !bIsFrameData)
	{
		return;
	}

	FName SubjectName = NAME_None;
	if (const FString* SubjectNamePtr = Context->GetAnnotations().Find(FLiveLinkMessageAnnotation::SubjectAnnotation))
	{
		SubjectName = *(*SubjectNamePtr);
	}
	if (SubjectName == NAME_None)
	{
		static const FName NAME_InvalidSubject = "LiveLinkMessageBusSource_InvalidSubject";
		FLiveLinkLog::ErrorOnce(NAME_InvalidSubject, FLiveLinkSubjectKey(SourceGuid, NAME_None), TEXT("No Subject Name was provided for connection '%s'"), *GetSourceMachineName().ToString());
		return;
	}

	// Find the role.
	TSubclassOf<ULiveLinkRole> SubjectRole;
	if (bIsStaticData)
	{
		// Check if it's in the Annotation first
		FName RoleName = NAME_None;
		if (const FString* RoleNamePtr = Context->GetAnnotations().Find(FLiveLinkMessageAnnotation::RoleAnnotation))
		{
			RoleName = *(*RoleNamePtr);
		}

		for (TWeakObjectPtr<ULiveLinkRole> WeakRole : RoleInstances)
		{
			if (ULiveLinkRole* Role = WeakRole.Get())
			{
				if (RoleName != NAME_None)
				{
					if (RoleName == Role->GetClass()->GetFName())
					{
						if (bIsStaticData && MessageTypeInfo->IsChildOf(Role->GetStaticDataStruct()))
						{
							SubjectRole = Role->GetClass();
							break;
						}
						if (bIsFrameData && MessageTypeInfo->IsChildOf(Role->GetFrameDataStruct()))
						{
							SubjectRole = Role->GetClass();
							break;
						}
					}
				}
				else
				{
					if (Role->GetStaticDataStruct() == MessageTypeInfo)
					{
						SubjectRole = Role->GetClass();
						break;
					}
				}
			}
		}

		if (SubjectRole.Get() == nullptr)
		{
			static const FName NAME_InvalidRole = "LiveLinkMessageBusSource_InvalidRole";
			FLiveLinkLog::ErrorOnce(NAME_InvalidRole, FLiveLinkSubjectKey(SourceGuid, SubjectName), TEXT("No Role was provided or found for subject '%s' with connection '%s'"), *SubjectName.ToString(), *GetSourceMachineName().ToString());
			return;
		}

	}

	const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);
	if (bIsStaticData)
	{
		check(MessageTypeInfo->IsChildOf(FLiveLinkBaseStaticData::StaticStruct()));

		FLiveLinkStaticDataStruct DataStruct(MessageTypeInfo);
		DataStruct.InitializeWith(MessageTypeInfo, reinterpret_cast<const FLiveLinkBaseStaticData*>(Context->GetMessage()));
		Client->PushSubjectStaticData_AnyThread(SubjectKey, SubjectRole, MoveTemp(DataStruct));
	}
	else
	{
		check(MessageTypeInfo->IsChildOf(FLiveLinkBaseFrameData::StaticStruct()));

		FLiveLinkFrameDataStruct DataStruct(MessageTypeInfo);
		const FLiveLinkBaseFrameData* Message = reinterpret_cast<const FLiveLinkBaseFrameData*>(Context->GetMessage());
		DataStruct.InitializeWith(MessageTypeInfo, Message);
		DataStruct.GetBaseData()->WorldTime = FLiveLinkWorldTime(Message->WorldTime.GetOffsettedTime(), MachineTimeOffset);
		Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(DataStruct));
	}
}

bool FLiveLinkMessageBusSource::IsSourceStillValid() const
{
	return ConnectionAddress.IsValid() && bIsValid;
}

FText FLiveLinkMessageBusSource::GetSourceStatus() const
{
	if (!ConnectionAddress.IsValid())
	{
		return NSLOCTEXT("LiveLinkMessageBusSource", "InvalidConnection", "Waiting for connection");
	}
	else if (IsSourceStillValid())
	{
		return NSLOCTEXT("LiveLinkMessageBusSource", "ActiveStatus", "Active");
	}
	return NSLOCTEXT("LiveLinkMessageBusSource", "TimeoutStatus", "Not responding");
}

void FLiveLinkMessageBusSource::HandleHeartbeat(const FLiveLinkHeartbeatMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UpdateConnectionLastActive();
}

void FLiveLinkMessageBusSource::HandleClearSubject(const FLiveLinkClearSubject& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UpdateConnectionLastActive();

	if (!Message.SubjectName.IsNone())
	{
		const FLiveLinkSubjectKey SubjectKey(SourceGuid, Message.SubjectName);
		Client->RemoveSubject_AnyThread(SubjectKey);
	}
}

FORCEINLINE void FLiveLinkMessageBusSource::UpdateConnectionLastActive()
{
	FScopeLock ConnectionTimeLock(&ConnectionLastActiveSection);

	ConnectionLastActive = FPlatformTime::Seconds();
}

void FLiveLinkMessageBusSource::SendConnectMessage()
{
	FLiveLinkConnectMessage* ConnectMessage = new FLiveLinkConnectMessage();
	ConnectMessage->LiveLinkVersion = 2;
	MessageEndpoint->Send(ConnectMessage, ConnectionAddress);
	FLiveLinkHeartbeatEmitter& HeartbeatEmitter = ILiveLinkModule::Get().GetHeartbeatEmitter();
	HeartbeatEmitter.StartHeartbeat(ConnectionAddress, MessageEndpoint);
	bIsValid = true;
}

bool FLiveLinkMessageBusSource::RequestSourceShutdown()
{
	FLiveLinkMessageBusDiscoveryManager& DiscoveryManager = ILiveLinkModule::Get().GetMessageBusDiscoveryManager();
	if (DiscoveryManager.IsRunning() && !ConnectionAddress.IsValid())
	{
		DiscoveryManager.RemoveDiscoveryMessageRequest();
	}

	FLiveLinkHeartbeatEmitter& HeartbeatEmitter = ILiveLinkModule::Get().GetHeartbeatEmitter();
	HeartbeatEmitter.StopHeartbeat(ConnectionAddress, MessageEndpoint);

	// Disable the Endpoint message handling since the message could keep it alive a bit.
	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Disable();
	}
	MessageEndpoint.Reset();

	return true;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FLiveLinkMessageBusSource::HandleSubjectData(const FLiveLinkSubjectDataMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UpdateConnectionLastActive();

	Client->PushSubjectSkeleton(SourceGuid, Message.SubjectName, Message.RefSkeleton);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FLiveLinkMessageBusSource::HandleSubjectFrame(const FLiveLinkSubjectFrameMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UpdateConnectionLastActive();

	FLiveLinkFrameData FrameData;
	FrameData.Transforms = Message.Transforms;
	FrameData.CurveElements = Message.Curves;
	FrameData.MetaData = Message.MetaData;
	FrameData.WorldTime = FLiveLinkWorldTime(Message.Time);
	Client->PushSubjectData(SourceGuid, Message.SubjectName, FrameData);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

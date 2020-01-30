// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkProvider.h"

#include "HAL/PlatformProcess.h"
#include "IMessageContext.h"
#include "LiveLinkMessages.h"
#include "LiveLinkTypes.h"

#include "Logging/LogMacros.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiveLinkMessageBus, Warning, All);

static const int32 LIVELINK_SupportedVersion = 2;


FName FLiveLinkMessageAnnotation::SubjectAnnotation = TEXT("SubjectName");
FName FLiveLinkMessageAnnotation::RoleAnnotation = TEXT("Role");


// Address that we have had a connection request from
struct FTrackedAddress
{
	FTrackedAddress(FMessageAddress InAddress)
		: Address(InAddress)
		, LastHeartbeatTime(FPlatformTime::Seconds())
	{}

	FMessageAddress Address;
	double			LastHeartbeatTime;
};


// Validate the supplied connection as still active
struct FConnectionValidator
{
	FConnectionValidator()
		: CutOffTime(FPlatformTime::Seconds() - CONNECTION_TIMEOUT)
	{}

	bool operator()(const FTrackedAddress& Connection) const { return Connection.LastHeartbeatTime >= CutOffTime; }

private:
	// How long we give connections before we decide they are dead
	static const double CONNECTION_TIMEOUT;

	// Oldest time that we still deem as active
	const double CutOffTime;
};

const double FConnectionValidator::CONNECTION_TIMEOUT = 10.f;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
// Subject that the application has told us about
struct FTrackedSubject
{
	// Ref skeleton to go with transform data
	FLiveLinkRefSkeleton RefSkeleton;

	// Bone transform data
	TArray<FTransform> Transforms;

	// Curve data
	TArray<FLiveLinkCurveElement> Curves;

	// MetaData for subject
	FLiveLinkMetaData MetaData;

	// Incrementing time (application time) for interpolation purposes
	double Time;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


// Static Subject data that the application has told us about
struct FTrackedStaticData
{
	FTrackedStaticData()
		: SubjectName(NAME_None)
	{}
	FTrackedStaticData(FName InSubjectName, TWeakObjectPtr<UClass> InRoleClass, FLiveLinkStaticDataStruct InStaticData)
		: SubjectName(InSubjectName), RoleClass(InRoleClass), StaticData(MoveTemp(InStaticData))
	{}
	FName SubjectName;
	TWeakObjectPtr<UClass> RoleClass;
	FLiveLinkStaticDataStruct StaticData;
	bool operator==(FName InSubjectName) const { return SubjectName == InSubjectName; }
};


// Frame Subject data that the application has told us about
struct FTrackedFrameData
{
	FTrackedFrameData()
		: SubjectName(NAME_None)
	{}
	FTrackedFrameData(FName InSubjectName, FLiveLinkFrameDataStruct InFrameData)
		: SubjectName(InSubjectName), FrameData(MoveTemp(InFrameData))
	{}
	FName SubjectName;
	FLiveLinkFrameDataStruct FrameData;
	bool operator==(FName InSubjectName) const { return SubjectName == InSubjectName; }
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FLiveLinkProvider : public ILiveLinkProvider
{
private:
	const FString ProviderName;
	const FString MachineName;

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** Lock to stop multiple threads accessing the CurrentPreset at the same time */
	mutable FCriticalSection CriticalSection;

	// Array of our current connections
	TArray<FTrackedAddress> ConnectedAddresses;

	// Cache of our current subject state
	TArray<FTrackedStaticData> StaticDatas;
	TArray<FTrackedFrameData> FrameDatas;
	TMap<FName, FTrackedSubject> Subjects;

	// Delegate to notify interested parties when the client sources have changed
	FLiveLinkProviderConnectionStatusChanged OnConnectionStatusChanged;
	
	//Message bus message handlers
	void HandlePingMessage(const FLiveLinkPingMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleConnectMessage(const FLiveLinkConnectMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleHeartbeat(const FLiveLinkHeartbeatMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	// End message bus message handlers

	// Validate our current connections
	void ValidateConnections()
	{
		FConnectionValidator Validator;

		const int32 RemovedConnections = ConnectedAddresses.RemoveAll([=](const FTrackedAddress& Address) { return !Validator(Address); });

		if (RemovedConnections > 0)
		{
			OnConnectionStatusChanged.Broadcast();
		}
	}

	// Get the cached data for the named subject
	FTrackedSubject& GetTrackedSubject(const FName& SubjectName)
	{
		return Subjects.FindOrAdd(SubjectName);
	}

	// Send hierarchy data for named subject
	void SendSubject(FName SubjectName, const FTrackedSubject& Subject)
	{
		FLiveLinkSubjectDataMessage* SubjectData = new FLiveLinkSubjectDataMessage;
		SubjectData->RefSkeleton = Subject.RefSkeleton;
		SubjectData->SubjectName = SubjectName;

		ValidateConnections();
		TArray<FMessageAddress> Addresses;
		Addresses.Reserve(ConnectedAddresses.Num());
		for (const FTrackedAddress& Address : ConnectedAddresses)
		{
			Addresses.Add(Address.Address);
		}

		MessageEndpoint->Send(SubjectData, Addresses);
	}

	// Send frame data for named subject
	void SendSubjectFrame(FName SubjectName, const FTrackedSubject& Subject)
	{
		FLiveLinkSubjectFrameMessage* SubjectFrame = new FLiveLinkSubjectFrameMessage;
		SubjectFrame->Transforms = Subject.Transforms;
		SubjectFrame->SubjectName = SubjectName;
		SubjectFrame->Curves = Subject.Curves;
		SubjectFrame->MetaData = Subject.MetaData;
		SubjectFrame->Time = Subject.Time;

		ValidateConnections();

		TArray<FMessageAddress> Addresses;
		Addresses.Reserve(ConnectedAddresses.Num());
		for (const FTrackedAddress& Address : ConnectedAddresses)
		{
			Addresses.Add(Address.Address);
		}

		MessageEndpoint->Send(SubjectFrame, Addresses);
	}

	// Get the cached data for the named subject
	FTrackedStaticData* GetLastSubjectStaticData(const FName& SubjectName)
	{
		return StaticDatas.FindByKey(SubjectName);
	}

	FTrackedFrameData* GetLastSubjectFrameData(const FName& SubjectName)
	{
		return FrameDatas.FindByKey(SubjectName);
	}

	void SetLastSubjectStaticData(FName SubjectName, TSubclassOf<ULiveLinkRole> Role, FLiveLinkStaticDataStruct&& StaticData)
	{
		FTrackedStaticData* Result = StaticDatas.FindByKey(SubjectName);
		if (Result)
		{
			Result->StaticData = MoveTemp(StaticData);
			Result->RoleClass = Role.Get();
		}
		else
		{
			StaticDatas.Emplace(SubjectName, Role.Get(), MoveTemp(StaticData));
		}
	}

	void SetLastSubjectFrameData(FName SubjectName, FLiveLinkFrameDataStruct&& FrameData)
	{
		FTrackedFrameData* Result = FrameDatas.FindByKey(SubjectName);
		if (Result)
		{
			Result->FrameData = MoveTemp(FrameData);
		}
		else
		{
			FrameDatas.Emplace(SubjectName, MoveTemp(FrameData));
		}
	}

	// Clear a existing track subject
	void ClearTrackedSubject(const FName& SubjectName)
	{
		Subjects.Remove(SubjectName);
		const int32 FrameIndex = FrameDatas.IndexOfByKey(SubjectName);
		if (FrameIndex != INDEX_NONE)
		{
			FrameDatas.RemoveAtSwap(FrameIndex);
		}
		const int32 StaticIndex = StaticDatas.IndexOfByKey(SubjectName);
		if (FrameIndex != INDEX_NONE)
		{
			StaticDatas.RemoveAtSwap(StaticIndex);
		}
	}

	void SendClearSubjectToConnections(FName SubjectName)
	{
		ValidateConnections();

		TArray<FMessageAddress> MessageAddresses;
		MessageAddresses.Reserve(ConnectedAddresses.Num());
		for (const FTrackedAddress& Address : ConnectedAddresses)
		{
			MessageAddresses.Add(Address.Address);
		}

		FLiveLinkClearSubject* ClearSubject = new FLiveLinkClearSubject(SubjectName);
		MessageEndpoint->Send(ClearSubject, EMessageFlags::Reliable, nullptr, MessageAddresses, FTimespan::Zero(), FDateTime::MaxValue());
	}

public:
	FLiveLinkProvider(const FString& InProviderName)
		: ProviderName(InProviderName)
		, MachineName(FPlatformProcess::ComputerName())
	{
		MessageEndpoint = FMessageEndpoint::Builder(*InProviderName)
			.ReceivingOnAnyThread()
			.Handling<FLiveLinkPingMessage>(this, &FLiveLinkProvider::HandlePingMessage)
			.Handling<FLiveLinkConnectMessage>(this, &FLiveLinkProvider::HandleConnectMessage)
			.Handling<FLiveLinkHeartbeatMessage>(this, &FLiveLinkProvider::HandleHeartbeat);

		if (MessageEndpoint.IsValid())
		{
			MessageEndpoint->Subscribe<FLiveLinkPingMessage>();
		}
	}

	virtual ~FLiveLinkProvider()
	{
		if (MessageEndpoint.IsValid())
		{
			// Disable the Endpoint message handling since the message could keep it alive a bit.
			MessageEndpoint->Disable();
			MessageEndpoint.Reset();
		}
	}

	virtual void UpdateSubject(const FName& SubjectName, const TArray<FName>& BoneNames, const TArray<int32>& BoneParents) override
	{
		FScopeLock Lock(&CriticalSection);

		FTrackedSubject& Subject = GetTrackedSubject(SubjectName);
		Subject.RefSkeleton.SetBoneNames(BoneNames);
		Subject.RefSkeleton.SetBoneParents(BoneParents);
		Subject.Transforms.Empty();

		SendSubject(SubjectName, Subject);
	}

	virtual bool UpdateSubjectStaticData(const FName SubjectName, TSubclassOf<ULiveLinkRole> Role, FLiveLinkStaticDataStruct&& StaticData) override
	{
		FScopeLock Lock(&CriticalSection);

		if (SubjectName == NAME_None || Role.Get() == nullptr)
		{
			return false;
		}

		if (Role->GetDefaultObject<ULiveLinkRole>()->GetStaticDataStruct() != StaticData.GetStruct())
		{
			return false;
		}

		if (GetLastSubjectStaticData(SubjectName) != nullptr)
		{
			ClearSubject(SubjectName);
		}

		ValidateConnections();

		if (ConnectedAddresses.Num() > 0)
		{
			TArray<FMessageAddress> Addresses;
			Addresses.Reserve(ConnectedAddresses.Num());
			for (const FTrackedAddress& Address : ConnectedAddresses)
			{
				Addresses.Add(Address.Address);
			}

			TMap<FName, FString> Annotations;
			Annotations.Add(FLiveLinkMessageAnnotation::SubjectAnnotation, SubjectName.ToString());
			Annotations.Add(FLiveLinkMessageAnnotation::RoleAnnotation, Role->GetName());

			MessageEndpoint->Send(StaticData.CloneData(), const_cast<UScriptStruct*>(StaticData.GetStruct()), EMessageFlags::Reliable, Annotations, nullptr, Addresses, FTimespan::Zero(), FDateTime::MaxValue());
		}

		SetLastSubjectStaticData(SubjectName, Role, MoveTemp(StaticData));

		return true;
	}

	virtual void ClearSubject(const FName& SubjectName)
	{
		FScopeLock Lock(&CriticalSection);

		RemoveSubject(SubjectName);
	}

	virtual void RemoveSubject(const FName SubjectName) override
	{
		FScopeLock Lock(&CriticalSection);

		ClearTrackedSubject(SubjectName);
		SendClearSubjectToConnections(SubjectName);
	}

	virtual void UpdateSubjectFrame(const FName& SubjectName, const TArray<FTransform>& BoneTransforms, const TArray<FLiveLinkCurveElement>& CurveData, double Time) override
	{
		FScopeLock Lock(&CriticalSection);

		FTrackedSubject& Subject = GetTrackedSubject(SubjectName);

		Subject.Transforms = BoneTransforms;
		Subject.Curves = CurveData;
		Subject.Time = Time;

		SendSubjectFrame(SubjectName, Subject);
	}

	virtual void UpdateSubjectFrame(const FName& SubjectName, const TArray<FTransform>& BoneTransforms, const TArray<FLiveLinkCurveElement>& CurveData,
		const FLiveLinkMetaData& MetaData, double Time) override
	{
		FScopeLock Lock(&CriticalSection);

		FTrackedSubject& Subject = GetTrackedSubject(SubjectName);

		Subject.Transforms = BoneTransforms;
		Subject.Curves = CurveData;
		Subject.MetaData = MetaData;
		Subject.Time = Time;

		SendSubjectFrame(SubjectName, Subject);
	}

	virtual bool UpdateSubjectFrameData(const FName SubjectName, FLiveLinkFrameDataStruct&& FrameData) override
	{
		FScopeLock Lock(&CriticalSection);

		if (SubjectName == NAME_None)
		{
			return false;
		}

		FTrackedStaticData* StaticData = GetLastSubjectStaticData(SubjectName);
		if (StaticData == nullptr)
		{
			return false;
		}

		UClass* RoleClass = StaticData->RoleClass.Get();
		if (RoleClass == nullptr)
		{
			return false;
		}

		if (RoleClass->GetDefaultObject<ULiveLinkRole>()->GetFrameDataStruct() != FrameData.GetStruct())
		{
			return false;
		}

		ValidateConnections();

		if (ConnectedAddresses.Num() > 0)
		{
			TArray<FMessageAddress> Addresses;
			Addresses.Reserve(ConnectedAddresses.Num());
			for (const FTrackedAddress& Address : ConnectedAddresses)
			{
				Addresses.Add(Address.Address);
			}

			TMap<FName, FString> Annotations;
			Annotations.Add(FLiveLinkMessageAnnotation::SubjectAnnotation, SubjectName.ToString());

			MessageEndpoint->Send(FrameData.CloneData(), const_cast<UScriptStruct*>(FrameData.GetStruct()), EMessageFlags::None, Annotations, nullptr, Addresses, FTimespan::Zero(), FDateTime::MaxValue());
		}

		SetLastSubjectFrameData(SubjectName, MoveTemp(FrameData));

		return true;
	}

	virtual bool HasConnection() const
	{
		FScopeLock Lock(&CriticalSection);

		FConnectionValidator Validator;

		for (const FTrackedAddress& Connection : ConnectedAddresses)
		{
			if (Validator(Connection))
			{
				return true;
			}
		}
		return false;
	}

	virtual FDelegateHandle RegisterConnStatusChangedHandle(const FLiveLinkProviderConnectionStatusChanged::FDelegate& ConnStatusChanged)
	{
		return OnConnectionStatusChanged.Add(ConnStatusChanged);
	}

	virtual void UnregisterConnStatusChangedHandle(FDelegateHandle Handle)
	{
		OnConnectionStatusChanged.Remove(Handle);
	}
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FLiveLinkProvider::HandlePingMessage(const FLiveLinkPingMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (Message.LiveLinkVersion < LIVELINK_SupportedVersion)
	{
		UE_LOG(LogLiveLinkMessageBus, Warning, TEXT("A unsupported version of LiveLink is trying to communicate. Requested version: '%d'. Supported version: '%d'."), Message.LiveLinkVersion, LIVELINK_SupportedVersion)
		return;
	}

	MessageEndpoint->Send(new FLiveLinkPongMessage(ProviderName, MachineName, Message.PollRequest), Context->GetSender());
}

void FLiveLinkProvider::HandleConnectMessage(const FLiveLinkConnectMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	FScopeLock Lock(&CriticalSection);

	if (Message.LiveLinkVersion < LIVELINK_SupportedVersion)
	{
		UE_LOG(LogLiveLinkMessageBus, Error, TEXT("A unsupported version of LiveLink is trying to connect. Requested version: '%d'. Supported version: '%d'."), Message.LiveLinkVersion, LIVELINK_SupportedVersion)
		return;
	}

	const FMessageAddress& ConnectionAddress = Context->GetSender();

	if (!ConnectedAddresses.ContainsByPredicate([=](const FTrackedAddress& Address) { return Address.Address == ConnectionAddress; }))
	{
		ConnectedAddresses.Add(FTrackedAddress(ConnectionAddress));

		// LiveLink version 1 path
		for (const auto& Subject : Subjects)
		{
			SendSubject(Subject.Key, Subject.Value);
			FPlatformProcess::Sleep(0.1); //HACK: Try to help these go in order, editor needs extra buffering support to make sure this isn't needed in future.
			SendSubjectFrame(Subject.Key, Subject.Value);
		}

		// LiveLink version 2 path
		TArray<FMessageAddress> MessageAddress;
		MessageAddress.Add(ConnectionAddress);

		TMap<FName, FString> Annotations;
		Annotations.Add(FLiveLinkMessageAnnotation::SubjectAnnotation, TEXT(""));
		Annotations.Add(FLiveLinkMessageAnnotation::RoleAnnotation, TEXT(""));

		for (const FTrackedStaticData& Data : StaticDatas)
		{
			UClass* RoleClass = Data.RoleClass.Get();
			Annotations.FindChecked(FLiveLinkMessageAnnotation::SubjectAnnotation) = Data.SubjectName.ToString();
			Annotations.FindChecked(FLiveLinkMessageAnnotation::RoleAnnotation) = RoleClass ? RoleClass->GetName() : TEXT("");
			MessageEndpoint->Send(Data.StaticData.CloneData(), const_cast<UScriptStruct*>(Data.StaticData.GetStruct()), EMessageFlags::Reliable, Annotations, nullptr, MessageAddress, FTimespan::Zero(), FDateTime::MaxValue());
		}

		FPlatformProcess::Sleep(0.1); //HACK: Try to help these go in order, editor needs extra buffering support to make sure this isn't needed in future.

		for (const FTrackedFrameData& Data : FrameDatas)
		{
			Annotations.FindChecked(FLiveLinkMessageAnnotation::SubjectAnnotation) = Data.SubjectName.ToString();
			MessageEndpoint->Send(Data.FrameData.CloneData(), const_cast<UScriptStruct*>(Data.FrameData.GetStruct()), EMessageFlags::None, Annotations, nullptr, MessageAddress, FTimespan::Zero(), FDateTime::MaxValue());
		}

		OnConnectionStatusChanged.Broadcast();
	}
}

void FLiveLinkProvider::HandleHeartbeat(const FLiveLinkHeartbeatMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	FScopeLock Lock(&CriticalSection);

	FTrackedAddress* TrackedAddress = ConnectedAddresses.FindByPredicate([=](const FTrackedAddress& ConAddress) { return ConAddress.Address == Context->GetSender(); });
	if (TrackedAddress)
	{
		TrackedAddress->LastHeartbeatTime = FPlatformTime::Seconds();

		// Respond so editor gets heartbeat too
		MessageEndpoint->Send(new FLiveLinkHeartbeatMessage(), Context->GetSender());
	}
}

TSharedPtr<ILiveLinkProvider> ILiveLinkProvider::CreateLiveLinkProvider(const FString& ProviderName)
{
	return MakeShareable(new FLiveLinkProvider(ProviderName));
}
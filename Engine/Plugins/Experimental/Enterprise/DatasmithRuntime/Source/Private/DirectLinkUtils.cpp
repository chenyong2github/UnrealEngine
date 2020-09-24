// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkUtils.h"

#include "DatasmithRuntime.h"
#include "DatasmithRuntimeBlueprintLibrary.h"

#include "DatasmithCore.h"
#include "DatasmithTranslatorModule.h"
#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MaterialSelectors/DatasmithRevitLiveMaterialSelector.h"

#include "HAL/CriticalSection.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

const TCHAR* EndPointName = TEXT("DatasmithRuntime");

namespace DatasmithRuntime
{
	class FDirectLinkProxyImpl : public FTickableGameObject, public DirectLink::IEndpointObserver
	{
	public:
		static TSharedRef<FDirectLinkProxyImpl> Get();

		virtual ~FDirectLinkProxyImpl();

		bool RegisterSceneProvider(const TCHAR* StreamName, TSharedPtr<FDestinationProxy> DestinationProxy);

		void UnregisterSceneProvider(TSharedPtr<FDestinationProxy> DestinationProxy);

		FString GetEndPointName();

		const TArray<FDatasmithRuntimeSourceInfo>& GetListOfSources() const;

		bool OpenConnection(const DirectLink::FSourceHandle& SourceId, const DirectLink::FDestinationHandle& DestinationId);

		void CloseConnection(const DirectLink::FSourceHandle& SourceId, const DirectLink::FDestinationHandle& DestinationId);

		DirectLink::FSourceHandle GetSourceHandleFromHash(uint32 SourceHash);

		uint32 GetSourceHandleHash(const DirectLink::FSourceHandle& SourceId);

		FString GetSourceName(const DirectLink::FSourceHandle& SourceId);

		DirectLink::FSourceHandle GetConnection(const DirectLink::FDestinationHandle& DestinationId);

		void OnStateChanged(const DirectLink::FRawInfo& RawInfo) override;

		void SetChangeNotifier(FDatasmithRuntimeChangeEvent* InNotifyChange)
		{
			NotifyChange = InNotifyChange;
		}

	protected:
		//~ Begin FTickableEditorObject interface
		virtual void Tick(float DeltaSeconds) override;

		virtual bool IsTickable() const override
		{
			return ReceiverEndpoint.IsValid() && bIsDirty;
		}

		virtual TStatId GetStatId() const override
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FDirectLinkProxyImpl, STATGROUP_Tickables);
		}
		//~ End FTickableEditorObject interface

	private:
		FDirectLinkProxyImpl() : bIsDirty(false), NotifyChange(nullptr) {}

	private:
		TUniquePtr<DirectLink::FEndpoint> ReceiverEndpoint;
		TSet<TSharedPtr<FDestinationProxy>> DestinationList;

		DirectLink::FRawInfo LastRawInfo;
		mutable FRWLock RawInfoCopyLock;
		FMD5Hash LastHash;

		TArray<FDatasmithRuntimeSourceInfo> LastSources;

		std::atomic_bool bIsDirty;
		FDatasmithRuntimeChangeEvent* NotifyChange;
	};
}

UDirectLinkProxy::UDirectLinkProxy()
{
	using namespace DatasmithRuntime;

	Impl = FDirectLinkProxyImpl::Get();
	Impl->SetChangeNotifier(&OnDirectLinkChange);
}

FString UDirectLinkProxy::GetEndPointName()
{
	return EndPointName;
}

TArray<FDatasmithRuntimeSourceInfo> UDirectLinkProxy::GetListOfSources()
{
	return Impl->GetListOfSources();
}

FString UDirectLinkProxy::GetDestinationName(ADatasmithRuntimeActor* DatasmithRuntimeActor)
{
	return DatasmithRuntimeActor ? DatasmithRuntimeActor->GetDestinationName() : FString();
}

bool UDirectLinkProxy::IsConnected(ADatasmithRuntimeActor* DatasmithRuntimeActor)
{
	return DatasmithRuntimeActor ? DatasmithRuntimeActor->IsConnected() : false;
}

FString UDirectLinkProxy::GetSourcename(ADatasmithRuntimeActor* DatasmithRuntimeActor)
{
	return DatasmithRuntimeActor ? DatasmithRuntimeActor->GetSourceName() : TEXT("None");
}

void UDirectLinkProxy::ConnectToSource(ADatasmithRuntimeActor* DatasmithRuntimeActor, int32 SourceIndex)
{
	if (DatasmithRuntimeActor)
	{
		const TArray<FDatasmithRuntimeSourceInfo>& SourcesList = Impl->GetListOfSources();

		if (SourcesList.IsValidIndex(SourceIndex))
		{
			DatasmithRuntimeActor->OpenConnection(SourcesList[SourceIndex].Hash);
		}
		else if (SourceIndex == INDEX_NONE)
		{
			DatasmithRuntimeActor->CloseConnection();
			DatasmithRuntimeActor->Reset();
		}
	}
}

namespace DatasmithRuntime
{
	TSharedRef<FDirectLinkProxyImpl> FDirectLinkProxyImpl::Get()
	{
		static TSharedRef<FDirectLinkProxyImpl> DirectLinkProxy(new FDirectLinkProxyImpl());

#if !NO_LOGGING
		LogDatasmith.SetVerbosity( ELogVerbosity::Error );
		//LogDirectLink.SetVerbosity( ELogVerbosity::Error );
		//LogDirectLinkIndexer.SetVerbosity( ELogVerbosity::Error );
		//LogDirectLinkNet.SetVerbosity( ELogVerbosity::Error );
#endif

		return DirectLinkProxy;
	}

	FDirectLinkProxyImpl::~FDirectLinkProxyImpl()
	{
		if (ReceiverEndpoint.IsValid())
		{
			ReceiverEndpoint->RemoveEndpointObserver(this);
			ReceiverEndpoint.Reset();
		}
	}

	bool FDirectLinkProxyImpl::RegisterSceneProvider(const TCHAR* StreamName, TSharedPtr<FDestinationProxy> DestinationProxy)
	{
		using namespace DirectLink;

		if (DestinationProxy.IsValid())
		{
			if (!ReceiverEndpoint.IsValid())
			{
				ReceiverEndpoint.Reset();
				bool bInit = true;
	#if !WITH_EDITOR
				bInit = (FModuleManager::Get().LoadModule(TEXT("Messaging")))
					&& (FModuleManager::Get().LoadModule(TEXT("Networking")))
					&& (FModuleManager::Get().LoadModule(TEXT("UdpMessaging")));
	#endif

				if (!bInit)
				{
					return false;
				}

				ReceiverEndpoint = MakeUnique<FEndpoint>(EndPointName);
				ReceiverEndpoint->AddEndpointObserver(this);
				ReceiverEndpoint->SetVerbose();
			}

			check(ReceiverEndpoint.IsValid());

			DestinationProxy->GetDestinationHandle() = ReceiverEndpoint->AddDestination(StreamName, DirectLink::EVisibility::Public, StaticCastSharedPtr<ISceneProvider>(DestinationProxy));

			if (DestinationProxy->GetDestinationHandle().IsValid())
			{
				DestinationList.Add(DestinationProxy);
				return true;
			}
		}

		return false;
	}

	void FDirectLinkProxyImpl::UnregisterSceneProvider(TSharedPtr<FDestinationProxy> DestinationProxy)
	{
		if (ReceiverEndpoint.IsValid() && DestinationList.Contains(DestinationProxy))
		{
			DestinationList.Remove(DestinationProxy);
			ReceiverEndpoint->RemoveDestination(DestinationProxy->GetDestinationHandle());
		}
	}

	bool FDirectLinkProxyImpl::OpenConnection(const DirectLink::FSourceHandle& SourceId, const DirectLink::FDestinationHandle& DestinationId)
	{
		using namespace DirectLink;

		if (ReceiverEndpoint.IsValid())
		{
			FEndpoint::EOpenStreamResult Result = ReceiverEndpoint->OpenStream(SourceId, DestinationId);

			return Result == FEndpoint::EOpenStreamResult::Opened || Result == FEndpoint::EOpenStreamResult::AlreadyOpened;
		}

		return false;
	}

	void FDirectLinkProxyImpl::CloseConnection(const DirectLink::FSourceHandle& SourceId, const DirectLink::FDestinationHandle& DestinationId)
	{
		if (ReceiverEndpoint.IsValid())
		{
			ReceiverEndpoint->CloseStream(SourceId, DestinationId);
		}

	}

	FString FDirectLinkProxyImpl::GetSourceName(const DirectLink::FSourceHandle& SourceId)
	{
		using namespace DirectLink;

		if (ReceiverEndpoint.IsValid() && SourceId.IsValid())
		{
			FRawInfo RawInfo = ReceiverEndpoint->GetRawInfoCopy();

			if (FRawInfo::FDataPointInfo* DataPointInfoPtr = RawInfo.DataPointsInfo.Find(SourceId))
			{
				return DataPointInfoPtr->Name;
			}
		}

		return FString();
	}

	uint32 ComputeSourcesHash(const DirectLink::FSourceHandle& SourceId, const FMessageAddress& MessageAddress)
	{
		return HashCombine(GetTypeHash(SourceId), GetTypeHash(MessageAddress));
	}

	uint32 FDirectLinkProxyImpl::GetSourceHandleHash(const DirectLink::FSourceHandle& SourceId)
	{
		using namespace DirectLink;

		if (ReceiverEndpoint.IsValid() && SourceId.IsValid())
		{
			FRawInfo RawInfo = ReceiverEndpoint->GetRawInfoCopy();

			if (FRawInfo::FDataPointInfo* DataPointInfoPtr = RawInfo.DataPointsInfo.Find(SourceId))
			{
				if (FRawInfo::FEndpointInfo* EndPointInfoPtr = RawInfo.EndpointsInfo.Find(DataPointInfoPtr->EndpointAddress))
				{
					return ComputeSourcesHash(SourceId, DataPointInfoPtr->EndpointAddress);
				}
			}
		}

		return 0xffffffff;
	}

	DirectLink::FSourceHandle FDirectLinkProxyImpl::GetSourceHandleFromHash(uint32 SourceHash)
	{
		using namespace DirectLink;

		if (ReceiverEndpoint.IsValid())
		{
			FRawInfo RawInfo = ReceiverEndpoint->GetRawInfoCopy();

			for (TPair<FGuid, FRawInfo::FDataPointInfo>& DataPointInfo : RawInfo.DataPointsInfo)
			{
				if (GetSourceHandleHash(DataPointInfo.Key) == SourceHash)
				{
					return DataPointInfo.Key;
				}
			}
		}

		return FSourceHandle();
	}

	FString FDirectLinkProxyImpl::GetEndPointName()
	{
		return ReceiverEndpoint.IsValid() ? EndPointName : TEXT("Invalid");
	}

	const TArray<FDatasmithRuntimeSourceInfo>& FDirectLinkProxyImpl::GetListOfSources() const
	{
		return LastSources;
	}

	DirectLink::FSourceHandle FDirectLinkProxyImpl::GetConnection(const DirectLink::FDestinationHandle& DestinationId)
	{
		using namespace DirectLink;

		if (ReceiverEndpoint.IsValid())
		{
			FRawInfo RawInfo = ReceiverEndpoint->GetRawInfoCopy();

			for (FRawInfo::FStreamInfo& StreamInfo : RawInfo.StreamsInfo)
			{
				if (/*StreamInfo.bIsActive && */StreamInfo.Destination == DestinationId)
				{
					return StreamInfo.Source;
				}
			}
		}

		return FSourceHandle();
	}

	FMD5Hash ComputeSourcesHash(const DirectLink::FRawInfo& RawInfo)
	{
		using namespace DirectLink;

		TArray<FGuid> Keys;
		RawInfo.DataPointsInfo.GenerateKeyArray(Keys);
		Keys.Sort();

		TArray<uint8> Buffer;
		FMemoryWriter Ar( Buffer );

		for (FGuid& Guid : Keys)
		{
			FRawInfo::FDataPointInfo& DataPointInfo = const_cast<FRawInfo::FDataPointInfo&>(RawInfo.DataPointsInfo[Guid]);
			if (!DataPointInfo.bIsSource)
			{
				continue;
			}

			Ar << Guid;
			Ar << DataPointInfo.Name;
			Ar << DataPointInfo.bIsOnThisEndpoint;

			ensure(RawInfo.EndpointsInfo.Contains(DataPointInfo.EndpointAddress));
			FRawInfo::FEndpointInfo& EndpointInfo = const_cast<FRawInfo::FEndpointInfo&>(RawInfo.EndpointsInfo[DataPointInfo.EndpointAddress]);

			Ar << EndpointInfo.ProcessId;
			Ar << EndpointInfo.ExecutableName;
		}

		FMD5 MD5;
		MD5.Update(Buffer.GetData(), Buffer.Num());

		FMD5Hash Hash;
		Hash.Set(MD5);

		return Hash;
	}

	void FDirectLinkProxyImpl::OnStateChanged(const DirectLink::FRawInfo& RawInfo)
	{
		using namespace DirectLink;

		FRWScopeLock _(RawInfoCopyLock, SLT_Write);

		FMD5Hash NewHash = ComputeSourcesHash(RawInfo);

		if (NewHash == LastHash)
		{
			return;
		}

		LastHash = NewHash;
		LastRawInfo = RawInfo;
		LastSources.Reset();

		for (TSharedPtr<FDestinationProxy>& DestinationProxy : DestinationList)
		{
			if (DestinationProxy->IsConnected() && !LastRawInfo.DataPointsInfo.Contains(DestinationProxy->GetConnectedSourceHandle()))
			{
				DestinationProxy->ResetConnection();
			}
		}

		for (const TPair<FGuid, FRawInfo::FDataPointInfo>& MapEntry : RawInfo.DataPointsInfo)
		{
			const FGuid& DataPointId = MapEntry.Key;
			const FRawInfo::FDataPointInfo& DataPointInfo = MapEntry.Value;

			if (DataPointInfo.bIsSource && DataPointInfo.EndpointAddress != RawInfo.ThisEndpointAddress)
			{
				ensure(RawInfo.EndpointsInfo.Contains(DataPointInfo.EndpointAddress));
				const FRawInfo::FEndpointInfo& EndPointInfo = RawInfo.EndpointsInfo[DataPointInfo.EndpointAddress];

				// #ueent_datasmithruntime: Skip remote end points
				if (!EndPointInfo.bIsLocal)
				{
					continue;
				}

				const FString& SourceName = DataPointInfo.Name;

				FString SourceLabel = SourceName + TEXT("-") + EndPointInfo.ExecutableName + TEXT("-") + FString::FromInt((int32)EndPointInfo.ProcessId);

				const uint32 SourceHash = ComputeSourcesHash(DataPointId, DataPointInfo.EndpointAddress);
				LastSources.Emplace(*SourceLabel, SourceHash);
			}
		}

		bIsDirty = NotifyChange != nullptr;
	}

	void FDirectLinkProxyImpl::Tick(float DeltaSeconds)
	{
		FRWScopeLock _(RawInfoCopyLock, SLT_Write);
		NotifyChange->Broadcast();
		bIsDirty = false;
	}

	FDestinationProxy::FDestinationProxy(FDatasmithSceneReceiver_ISceneChangeListener* InChangeListener)
		: ChangeListener(InChangeListener)
		, DirectLinkProxy(FDirectLinkProxyImpl::Get().Get())
	{
	}

	bool FDestinationProxy::OpenConnection(uint32 SourceHash)
	{
		return OpenConnection(DirectLinkProxy.GetSourceHandleFromHash(SourceHash));
	}

	void FDestinationProxy::CloseConnection()
	{
		if (ConnectedSource.IsValid() && Destination.IsValid())
		{
			DirectLinkProxy.CloseConnection(ConnectedSource, Destination);
			ResetConnection();
		}
	}

	FString FDestinationProxy::GetSourceName()
	{
		return ConnectedSource.IsValid() ? DirectLinkProxy.GetSourceName(ConnectedSource) : TEXT("None");
	}

	bool FDestinationProxy::RegisterDestination(const TCHAR* StreamName)
	{
		using namespace DirectLink;

		UnregisterDestination();

		DirectLinkProxy.RegisterSceneProvider(StreamName, this->AsShared() );

		return Destination.IsValid();
	}

	void FDestinationProxy::UnregisterDestination()
	{
		using namespace DirectLink;

		if (Destination.IsValid())
		{
			CloseConnection();

			DirectLinkProxy.UnregisterSceneProvider(this->AsShared());

			Destination = FDestinationHandle();
		}

		ConnectedSource = FSourceHandle();
	}

	bool FDestinationProxy::OpenConnection(const DirectLink::FSourceHandle& SourceId)
	{
		using namespace DirectLink;

		if (SourceId.IsValid())
		{
			if (Destination.IsValid())
			{
				if (ConnectedSource.IsValid() && SourceId != ConnectedSource)
				{
					DirectLinkProxy.CloseConnection(ConnectedSource, Destination);
					ConnectedSource = FSourceHandle();
					SceneReceiver.Reset();
				}

				if (ChangeListener)
				{
					SceneReceiver = MakeShared<FDatasmithSceneReceiver>();
					SceneReceiver->SetChangeListener(ChangeListener);
				}

				if (DirectLinkProxy.OpenConnection(SourceId, Destination))
				{
					ConnectedSource = SourceId;
				}
			}
		}

		return SourceId.IsValid() && SourceId == ConnectedSource;
	}
}

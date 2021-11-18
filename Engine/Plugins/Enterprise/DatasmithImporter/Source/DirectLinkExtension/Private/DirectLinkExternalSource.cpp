// Copyright Epic Games, Inc. All Rights Reserved.


#include "DirectLinkExternalSource.h"

#include "DirectLinkExtensionModule.h"
#include "IDirectLinkManager.h"

#include "Async/Async.h"
#include "DirectLinkEndpoint.h"
#include "DirectLinkDeltaConsumer.h"
#include "DirectLinkMisc.h"
#include "Misc/AsyncTaskNotification.h"

#define LOCTEXT_NAMESPACE "DirectLinkExternalSource"

namespace UE::DatasmithImporter
{
	/**
	 * Wrapper DirectLink SceneReceiver, used to notify the FExternalSource when the DatasmithScene is loaded.
	 */
	class FInternalDirectLinkSceneReceiverWrapper : public DirectLink::ISceneReceiver
	{
	public:
		FInternalDirectLinkSceneReceiverWrapper(const TSharedRef<DirectLink::ISceneReceiver>& InSceneReceiver, const TSharedRef<FDirectLinkExternalSource>& InDirectLinkExternalSource)
			: SceneReceiver(InSceneReceiver)
			, DirectLinkExternalSource(InDirectLinkExternalSource)
		{}

		virtual void FinalSnapshot(const DirectLink::FSceneSnapshot& SceneSnapshot) override
		{
			SceneReceiver->FinalSnapshot(SceneSnapshot);
			if (TSharedPtr<FDirectLinkExternalSource> PinnedExternalSource = DirectLinkExternalSource.Pin())
			{
				PinnedExternalSource->CachedHash = DirectLink::GenerateSceneSnapshotHash(SceneSnapshot);
				PinnedExternalSource->TriggerOnExternalSourceChanged();
			}
		}

	private:
		TSharedRef<DirectLink::ISceneReceiver> SceneReceiver;
		TWeakPtr<FDirectLinkExternalSource> DirectLinkExternalSource;
	};

	FDirectLinkExternalSource::~FDirectLinkExternalSource()
	{
		Invalidate();
	}

	FExternalSourceCapabilities FDirectLinkExternalSource::GetCapabilities() const
	{ 
		FExternalSourceCapabilities Capabilities;
		Capabilities.bSupportAsynchronousLoading = true;

		return Capabilities;
	}

	void FDirectLinkExternalSource::Initialize(const FString& InSourceName, const FGuid& InSourceHandle, const FGuid& InDestinationHandle)
	{
		SourceName = InSourceName;
		SourceHandle = InSourceHandle;
		DestinationHandle = InDestinationHandle;
	}

	bool FDirectLinkExternalSource::OpenStream()
	{
		if (SourceHandle.IsValid() && DestinationHandle.IsValid() && !bIsStreamOpen)
		{
			const DirectLink::FEndpoint::EOpenStreamResult Result = IDirectLinkExtensionModule::GetEndpoint().OpenStream(SourceHandle, DestinationHandle);

			bIsStreamOpen = Result == DirectLink::FEndpoint::EOpenStreamResult::Opened;
			return bIsStreamOpen;			
		}

		return false;
	}

	void FDirectLinkExternalSource::CloseStream()
	{
		if (SourceHandle.IsValid() && DestinationHandle.IsValid() && bIsStreamOpen)
		{
			IDirectLinkExtensionModule::GetEndpoint().CloseStream(SourceHandle, DestinationHandle);
			bIsStreamOpen = false;
		}
	}

	void FDirectLinkExternalSource::Invalidate()
	{
		if (IDirectLinkExtensionModule::IsAvailable())
		{
			IDirectLinkExtensionModule::GetEndpoint().RemoveDestination(DestinationHandle);
			CloseStream();
		}

		ClearOnExternalSourceLoadedDelegates();
		DestinationHandle.Invalidate();
	}

	TSharedPtr<DirectLink::ISceneReceiver> FDirectLinkExternalSource::GetSceneReceiver(const DirectLink::IConnectionRequestHandler::FSourceInformation& Source)
	{
		if (!InternalSceneReceiver)
		{
			if (TSharedPtr<DirectLink::ISceneReceiver> SceneReceiver = GetSceneReceiverInternal(Source))
			{
				InternalSceneReceiver = MakeShared<FInternalDirectLinkSceneReceiverWrapper>(SceneReceiver.ToSharedRef(), StaticCastSharedRef<FDirectLinkExternalSource>(AsShared()));
			}
		}

		return InternalSceneReceiver;
	}

	bool FDirectLinkExternalSource::StartAsyncLoad()
	{
		if (!IsAvailable())
		{
			return false;
		}
		
		if (!IsStreamOpen())
		{
			return OpenStream();
		}

		// No need to do anything, the stream is already opened and the scene will be loaded on the next DirectLink sync.
		return true;
	}
}
#undef LOCTEXT_NAMESPACE
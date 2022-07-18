// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSubsystem.h"
#include "ILiveLinkClient.h"
#include "VCamPixelStreamingLiveLink.h"
#include "VCamPixelStreamingSession.h"
#include "PixelStreamingVCamLog.h"

void UVCamPixelStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkSource = MakeShared<FPixelStreamingLiveLinkSource>();
		LiveLinkClient->AddSource(LiveLinkSource);
	}
}

void UVCamPixelStreamingSubsystem::Deinitialize()
{
	Super::Deinitialize();
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (LiveLinkSource && ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient->RemoveSource(LiveLinkSource);
	}
	LiveLinkSource.Reset();
}

UVCamPixelStreamingSubsystem* UVCamPixelStreamingSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UVCamPixelStreamingSubsystem>() : nullptr;
}

void UVCamPixelStreamingSubsystem::RegisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	if (!OutputProvider) return;
	
	ActiveOutputProviders.AddUnique(OutputProvider);
	if (LiveLinkSource)
	{
		LiveLinkSource->CreateSubject(OutputProvider->GetFName());
		LiveLinkSource->PushTransformForSubject(OutputProvider->GetFName(), FTransform::Identity);
	}
}

void UVCamPixelStreamingSubsystem::UnregisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	if (!OutputProvider) return;
	if (ActiveOutputProviders.Remove(OutputProvider) && LiveLinkSource)
	{
		LiveLinkSource->RemoveSubject(OutputProvider->GetFName());
	}
}

TSharedPtr<UE::PixelStreamingServers::IServer> UVCamPixelStreamingSubsystem::LaunchSignallingServer(int StreamerPort, int PlayerPort)
{
	bool bAlreadyLaunched = Server.IsValid() && Server->HasLaunched();
	if(bAlreadyLaunched)
	{
		return Server;
	}

	// Download Pixel Streaming servers/frontend if we want to use a browser to view Pixel Streaming output
	// but only attempt this is we haven't already started a download before.
	if(!DownloadProcess.IsValid())
	{
		DownloadProcess = UE::PixelStreamingServers::DownloadPixelStreamingServers(/*bSkipIfPresent*/ true);
		if(DownloadProcess.IsValid())
		{
			DownloadProcess->OnCompleted().BindLambda([this, StreamerPort, PlayerPort](int ExitCode){
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("Pixel Streaming frontend downloaded, restarting signalling server to use it."));
				StopSignallingServer();
				LaunchSignallingServer(StreamerPort, PlayerPort);
			});
		}
	}

	// Launch signalling server
	Server = UE::PixelStreamingServers::MakeSignallingServer();
	UE::PixelStreamingServers::FLaunchArgs LaunchArgs;
	LaunchArgs.bPollUntilReady = false;
	LaunchArgs.ProcessArgs = FString::Printf(TEXT("--StreamerPort=%d --HttpPort=%d"), StreamerPort, PlayerPort);
	Server->Launch(LaunchArgs);
	return Server;
}

void UVCamPixelStreamingSubsystem::StopSignallingServer()
{
	if(Server.IsValid())
	{
		Server->Stop();
		Server.Reset();
	}
}
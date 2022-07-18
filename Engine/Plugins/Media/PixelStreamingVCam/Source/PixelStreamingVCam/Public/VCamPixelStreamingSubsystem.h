// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "PixelStreamingServers.h"
#include "VCamPixelStreamingSubsystem.generated.h"

class FPixelStreamingLiveLinkSource;
class UVCamPixelStreamingSession;

UCLASS()
class PIXELSTREAMINGVCAM_API UVCamPixelStreamingSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Convenience function for accessing the subsystem
	static UVCamPixelStreamingSubsystem* Get();
	void RegisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider);
	void UnregisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider);

	TSharedPtr<UE::PixelStreamingServers::IServer> LaunchSignallingServer(int StreamerPort, int PlayerPort);
	void StopSignallingServer();
	
	// An associated Live Link Source shared by all output providers
	TSharedPtr<FPixelStreamingLiveLinkSource> LiveLinkSource;
private:
	// Keep track of which output providers are currently active
	UPROPERTY(Transient)
	TArray<UVCamPixelStreamingSession*> ActiveOutputProviders;

	// Signalling/webserver
	TSharedPtr<UE::PixelStreamingServers::IServer> Server;

	// Download process for PS web frontend files (if we want to view output in the browser)
	TSharedPtr<FMonitoredProcess> DownloadProcess;
};

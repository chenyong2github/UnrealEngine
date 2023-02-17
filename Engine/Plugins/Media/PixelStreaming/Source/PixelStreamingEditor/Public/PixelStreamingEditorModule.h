// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingModule.h"
#include "PixelStreamingServers.h"
#include "PixelStreamingEditorUtils.h"

namespace UE::EditorPixelStreaming
{
	class FPixelStreamingToolbar;
}

class PIXELSTREAMINGEDITOR_API FPixelStreamingEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void StartStreaming(UE::EditorPixelStreaming::EStreamTypes InStreamType);
	void StopStreaming();
	UE_DEPRECATED(5.2, "FindMessageHandler(...) has been moved from the PixelStreaming module to the PixelStreamingInput module")
	void SetStreamType(UE::EditorPixelStreaming::EStreamTypes InStreamType){};
	UE_DEPRECATED(5.2, "GetStreamType() has been deprecated. You can now instead find what is being streamed by doing Streamer->GetVideoInput()->ToString()")
	UE::EditorPixelStreaming::EStreamTypes GetStreamType() { return UE::EditorPixelStreaming::EStreamTypes::Editor; }

	void StartSignalling();
	void StopSignalling();
	TSharedPtr<UE::PixelStreamingServers::IServer> GetSignallingServer();

	void SetSignallingDomain(const FString& InSignallingDomain);
	FString GetSignallingDomain() { return SignallingDomain; };
	void SetStreamerPort(int32 InStreamerPort);
	int32 GetStreamerPort() { return StreamerPort; };
	void SetViewerPort(int32 InViewerPort);
	int32 GetViewerPort() { return ViewerPort; };

	static FPixelStreamingEditorModule* GetModule();

private:
	void InitEditorStreaming(IPixelStreamingModule& Module);
	bool ParseResolution(const TCHAR* InResolution, uint32& OutX, uint32& OutY);
	void MaybeResizeEditor(TSharedPtr<SWindow> RootWindow);
	FString GetSignallingServerURL();

	TSharedPtr<UE::EditorPixelStreaming::FPixelStreamingToolbar> Toolbar;
	static FPixelStreamingEditorModule* PixelStreamingEditorModule;
	// Signalling/webserver
	TSharedPtr<UE::PixelStreamingServers::IServer> SignallingServer;
	// Download process for PS web frontend files (if we want to view output in the browser)
	TSharedPtr<FMonitoredProcess> DownloadProcess;
	// The signalling server host: eg ws://127.0.0.1
	FString SignallingDomain;
	// The port the streamer will connect to. eg 8888
	int32 StreamerPort;
	// The port the streams can be viewed at on the browser. eg 80 or 8080
#if PLATFORM_LINUX
	int32 ViewerPort = 8080; // ports <1000 require superuser privileges on Linux
#else
	int32 ViewerPort = 80;
#endif
	// The streamer used by the PixelStreamingEditor module
	TSharedPtr<IPixelStreamingStreamer> EditorStreamer;

public:
	bool bUseExternalSignallingServer = false;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteSessionHost.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "FrameGrabber.h"
#include "Widgets/SViewport.h"
#include "BackChannel/Utils/BackChannelThreadedConnection.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCMessage.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "Engine/GameEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "RemoteSession.h"
#include "RemoteSessionModule.h"

#if WITH_EDITOR
	#include "Editor.h"
	#include "Editor/EditorEngine.h"
	#include "IAssetViewport.h"
#endif

namespace RemoteSessionEd
{
	static FAutoConsoleVariable SlateDragDistanceOverride(TEXT("RemoteSessionEd.SlateDragDistanceOverride"), 10.0f, TEXT("How many pixels you need to drag before a drag and drop operation starts in remote app"));
};


FRemoteSessionHost::FRemoteSessionHost(TArray<FRemoteSessionChannelInfo> InSupportedChannels)
	: SupportedChannels(InSupportedChannels)
	, HostTCPPort(0)
	, IsListenerConnected(false)
{
	SavedEditorDragTriggerDistance = FSlateApplication::Get().GetDragTriggerDistance();
}

FRemoteSessionHost::~FRemoteSessionHost()
{
	// close this manually to force the thread to stop before things start to be 
	// destroyed
	if (Listener.IsValid())
	{
		Listener->Close();
	}

	Close();
}

void FRemoteSessionHost::Close()
{
	FRemoteSessionRole::Close();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetDragTriggerDistance(SavedEditorDragTriggerDistance);
	}
}

void FRemoteSessionHost::SetScreenSharing(const bool bEnabled)
{
}


bool FRemoteSessionHost::StartListening(const uint16 InPort)
{
	if (Listener.IsValid())
	{
		return false;
	}

	if (IBackChannelTransport* Transport = IBackChannelTransport::Get())
	{
		Listener = Transport->CreateConnection(IBackChannelTransport::TCP);

		if (Listener->Listen(InPort) == false)
		{
			Listener = nullptr;
		}
		HostTCPPort = InPort;
	}

	return Listener.IsValid();
}

void FRemoteSessionHost::OnBindEndpoints()
{
	FRemoteSessionRole::OnBindEndpoints();
}

void FRemoteSessionHost::OnCreateChannels()
{
	FRemoteSessionRole::OnCreateChannels();
	
	ClearChannels();
	
	CreateChannels(SupportedChannels);

	IsListenerConnected = true;
	
	// now ask the client to start these channels
	FBackChannelOSCMessage Msg(GetChannelSelectionEndPoint());
	
	FRemoteSessionModule& RemoteSession = FModuleManager::GetModuleChecked<FRemoteSessionModule>("RemoteSession");
	const TArray<FRemoteSessionModule::FChannelRedirects>& Redirects = RemoteSession.GetChannelRedirects();

	// send these across as a name/mode pair
	for (const FRemoteSessionChannelInfo& Channel : SupportedChannels)
	{
		ERemoteSessionChannelMode ClientMode = (Channel.Mode == ERemoteSessionChannelMode::Write) ? ERemoteSessionChannelMode::Read : ERemoteSessionChannelMode::Write;

		// For old version of the app, is there a old name that is compatible with the data that we could used
		const FRemoteSessionModule::FChannelRedirects* FoundRedirect = Redirects.FindByPredicate(
			[&Channel](const FRemoteSessionModule::FChannelRedirects& InChannel)
			{
				return Channel.Type == InChannel.NewName;
			});
		if (FoundRedirect)
		{
			Msg.Write(FoundRedirect->OldName);
			Msg.Write((int32)ClientMode);
		}

		Msg.Write(Channel.Type);
		Msg.Write((int32)ClientMode);
	}
	
	OSCConnection->SendPacket(Msg);
}


void FRemoteSessionHost::Tick(float DeltaTime)
{
	// non-threaded listener
	if (IsConnected() == false)
	{
		if (Listener.IsValid() && IsListenerConnected)
		{
			Listener->Close();
			Listener = nullptr;

			//reset the host TCP socket
			StartListening(HostTCPPort);
			IsListenerConnected = false;
		}
        
        if (Listener.IsValid())
        {
            Listener->WaitForConnection(0, [this](TSharedRef<IBackChannelConnection> InConnection) {
                Close();
                CreateOSCConnection(InConnection);
                return true;
            });
        }
	}
	
	FRemoteSessionRole::Tick(DeltaTime);
}

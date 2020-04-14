// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteImportServer.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "RemoteImportMessages.h"
#include "RemoteImportLibrary.h"



DEFINE_LOG_CATEGORY_STATIC(LogRemoteImportServer, Log, All);



FRemoteImportServer::FRemoteImportServer()
{
	auto MessageEndpointBuilder = FMessageEndpoint::Builder(TEXT("RemoteImportServer"))
		.ReceivingOnThread(ENamedThreads::GameThread)
		.Handling<FPingMessage>(this, &FRemoteImportServer::OnPingMessage)
		.Handling<FImportFileRequest>(this, &FRemoteImportServer::OnImportCommandMessage)
	;
	MessageEndpoint = MessageEndpointBuilder.Build();

	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Subscribe<FPingMessage>();
		MessageEndpoint->Subscribe<FImportFileRequest>();
	}

	OnAnchorChangeDelegateHandle = URemoteImportLibrary::GetAnchorListChangeDelegate().AddRaw(
		this, &FRemoteImportServer::PublishAnchorList);
}

FRemoteImportServer::~FRemoteImportServer()
{
	URemoteImportLibrary::GetAnchorListChangeDelegate().Remove(OnAnchorChangeDelegateHandle);
	if (MessageEndpoint.IsValid())
	{
		// Disable the Endpoint message handling since the message could keep it alive a bit.
		MessageEndpoint->Disable();
		MessageEndpoint.Reset();
	}
}

FServerStateMessage* MakeAnchorListMessage()
{
	auto ServerStateMessage = new FServerStateMessage();
	ServerStateMessage->Anchors = URemoteImportLibrary::ListAnchors();
	return ServerStateMessage;
}

void FRemoteImportServer::OnPingMessage(const FPingMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UE_LOG(LogRemoteImportServer, Display, TEXT("OnPingMessage: %d from %s"), Message.Version, *Context->GetSender().ToString());
	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Send(new FPongMessage(FString::Printf(TEXT("Received %d"), Message.Version)), Context->GetSender());
		MessageEndpoint->Send(MakeAnchorListMessage(), Context->GetSender());
	}
}

void FRemoteImportServer::OnImportCommandMessage(const FImportFileRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	URemoteImportLibrary::ImportSource(Message.File, Message.Destination);

	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Send(new FImportFileResponse(&Message), Context->GetSender());
	}
}

void FRemoteImportServer::PublishAnchorList()
{
	// #ueent_todo: use a timer to avoid spam and batch multiple updates in one
	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Publish(MakeAnchorListMessage());
	}
}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPrestonMDRSource.h"

#include "ILiveLinkClient.h"

#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"

#include "Sockets.h"
#include "SocketSubsystem.h"

#define LOCTEXT_NAMESPACE "LiveLinkPrestonMDRSource"

DEFINE_LOG_CATEGORY_STATIC(LogPrestonMDRSource, Log, All);

FLiveLinkPrestonMDRSource::FLiveLinkPrestonMDRSource(FLiveLinkPrestonMDRConnectionSettings InConnectionSettings)
	: SocketSubsystem(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	, ConnectionSettings(MoveTemp(InConnectionSettings))
	, LastTimeDataReceived(0.0)
	, bIsConnectedToDevice(false)
	, bFailedToConnectToDevice(false)
{
	SourceMachineName = FText::Format(LOCTEXT("PrestonMDRMachineName", "{0}:{1}"), FText::FromString(ConnectionSettings.IPAddress), FText::AsNumber(ConnectionSettings.PortNumber, &FNumberFormattingOptions::DefaultNoGrouping()));
}

FLiveLinkPrestonMDRSource::~FLiveLinkPrestonMDRSource()
{
	RequestSourceShutdown();
}

void FLiveLinkPrestonMDRSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SubjectKey = FLiveLinkSubjectKey(InSourceGuid, ConnectionSettings.SubjectName);

	OpenConnection();

	MessageThread = MakeUnique<FPrestonMDRMessageThread>(Socket);

	MessageThread->OnFrameDataReady_AnyThread().BindRaw(this, &FLiveLinkPrestonMDRSource::OnFrameDataReady_AnyThread);
	MessageThread->OnStatusChanged_AnyThread().BindRaw(this, &FLiveLinkPrestonMDRSource::OnStatusChanged_AnyThread);
	MessageThread->OnConnectionLost_AnyThread().BindRaw(this, &FLiveLinkPrestonMDRSource::OnConnectionLost_AnyThread);
	MessageThread->OnConnectionFailed_AnyThread().BindRaw(this, &FLiveLinkPrestonMDRSource::OnConnectionFailed_AnyThread);

	MessageThread->Start();
}

bool FLiveLinkPrestonMDRSource::IsSourceStillValid() const
{
	if (Socket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
	{
		return false;
	}

	return true;
}

bool FLiveLinkPrestonMDRSource::RequestSourceShutdown()
{
	if (MessageThread)
	{
		MessageThread->Stop();
		MessageThread.Reset();
	}

	if (Socket)
	{
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
	}

	return true;
}

FText FLiveLinkPrestonMDRSource::GetSourceType() const
{
	return LOCTEXT("PrestonMDRSourceType", "PrestonMDR");
}

FText FLiveLinkPrestonMDRSource::GetSourceStatus() const
{
	if (bFailedToConnectToDevice == true)
	{
		return LOCTEXT("FailedConnectionStatus", "Failed to connect");
	}
	else if (bIsConnectedToDevice == false)
	{
		return LOCTEXT("NotConnectedStatus", "Waiting to connect...");
	}
	else if (FPlatformTime::Seconds() - LastTimeDataReceived > DataReceivedTimeout)
	{
		return LOCTEXT("WaitingForDataStatus", "Idle");
	}
	return LOCTEXT("ActiveStatus", "Active");
}

void FLiveLinkPrestonMDRSource::OpenConnection()
{
	check(!Socket);

	// Create an IPv4 TCP Socket
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("Preston MDR Socket"), FNetworkProtocolTypes::IPv4);
	Socket->SetNonBlocking(true);
	Socket->SetNoDelay(true);
	Socket->SetRecvErr(true);

	FIPv4Address IPAddr;
	if (FIPv4Address::Parse(ConnectionSettings.IPAddress, IPAddr) == false)
	{
		UE_LOG(LogPrestonMDRSource, Error, TEXT("Ill-formed IP Address"));
		return;
	}

	uint16 PortNumber = ConnectionSettings.PortNumber;
	
	FIPv4Endpoint Endpoint = FIPv4Endpoint(IPAddr, PortNumber);
	TSharedRef<FInternetAddr> Addr = Endpoint.ToInternetAddr();
	
	UE_LOG(LogPrestonMDRSource, VeryVerbose, TEXT("Connecting to the MDR server at %s:%d..."), *IPAddr.ToString(), PortNumber);

	if (!Socket->Connect(*Addr))
	{
		UE_LOG(LogPrestonMDRSource, Error, TEXT("Could not connect to the MDR server at %s:%d"), *IPAddr.ToString(), PortNumber);
		bFailedToConnectToDevice = true;
		return;
	}

	if (Socket->GetConnectionState() == ESocketConnectionState::SCS_ConnectionError)
	{
		UE_LOG(LogPrestonMDRSource, Error, TEXT("Could not connect to the MDR server at %s:%d"), *IPAddr.ToString(), PortNumber);
		bFailedToConnectToDevice = true;
		return;
	}
}

void FLiveLinkPrestonMDRSource::OnConnectionLost_AnyThread()
{
	UE_LOG(LogPrestonMDRSource, Error, TEXT("Connection to the MDR device was lost"));
	bIsConnectedToDevice = false;

	// There was an unrecoverable connection loss, so we need to destroy the current socket and open a new one
	if (Socket)
	{
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
	}

	// Attempt to re-open the connection to the MDR server
	OpenConnection();

	if (MessageThread)
	{
		MessageThread->SetSocket(Socket);
		MessageThread->SoftReset();
	}
}

void FLiveLinkPrestonMDRSource::OnConnectionFailed_AnyThread()
{
	UE_LOG(LogPrestonMDRSource, Error, TEXT("Could not connect to the MDR server at %s:%d"), *ConnectionSettings.IPAddress, ConnectionSettings.PortNumber);
	bIsConnectedToDevice = false;
	bFailedToConnectToDevice = true;

	if (MessageThread)
	{
		MessageThread->Stop();
	}
}

void FLiveLinkPrestonMDRSource::OnStatusChanged_AnyThread(FMDR3Status InStatus)
{
	LatestMDRStatus = InStatus;
	UpdateStaticData_AnyThread();
}

void FLiveLinkPrestonMDRSource::UpdateStaticData_AnyThread()
{
	FLiveLinkStaticDataStruct PrestonMDRStaticDataStruct(FLiveLinkCameraStaticData::StaticStruct());
	FLiveLinkCameraStaticData* PrestonMDRStaticData = PrestonMDRStaticDataStruct.Cast<FLiveLinkCameraStaticData>();

	PrestonMDRStaticData->bIsFocalLengthSupported = LatestMDRStatus.bIsZoomMotorSet;
	PrestonMDRStaticData->bIsApertureSupported = LatestMDRStatus.bIsIrisMotorSet;
	PrestonMDRStaticData->bIsFocusDistanceSupported = LatestMDRStatus.bIsFocusMotorSet;

	PrestonMDRStaticData->bIsFieldOfViewSupported = false;
	PrestonMDRStaticData->bIsAspectRatioSupported = false;
	PrestonMDRStaticData->bIsProjectionModeSupported = false;

	Client->PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkCameraRole::StaticClass(), MoveTemp(PrestonMDRStaticDataStruct));
}

void FLiveLinkPrestonMDRSource::OnFrameDataReady_AnyThread(FLensDataPacket InData)
{
	bIsConnectedToDevice = true;

	FLiveLinkFrameDataStruct LensFrameDataStruct(FLiveLinkCameraFrameData::StaticStruct());
	FLiveLinkCameraFrameData* LensFrameData = LensFrameDataStruct.Cast<FLiveLinkCameraFrameData>();

	LastTimeDataReceived = FPlatformTime::Seconds();
	LensFrameData->WorldTime = LastTimeDataReceived.load();
	LensFrameData->MetaData.SceneTime = InData.FrameTime;
	LensFrameData->FocusDistance = InData.Focus;
	LensFrameData->FocalLength = InData.Zoom;
	LensFrameData->Aperture = InData.Iris;

	Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(LensFrameDataStruct));
}

#undef LOCTEXT_NAMESPACE

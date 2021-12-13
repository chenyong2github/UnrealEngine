// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPrestonMDRSource.h"

#include "ILiveLinkClient.h"

#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "LiveLinkPrestonMDRRole.h"
#include "LiveLinkPrestonMDRTypes.h"

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

	MessageThread->OnFrameDataReady().BindRaw(this, &FLiveLinkPrestonMDRSource::OnFrameDataReady);
	MessageThread->OnStatusChanged().BindRaw(this, &FLiveLinkPrestonMDRSource::OnStatusChanged);
	MessageThread->OnConnectionLost().BindRaw(this, &FLiveLinkPrestonMDRSource::OnConnectionLost);
	MessageThread->OnConnectionFailed().BindRaw(this, &FLiveLinkPrestonMDRSource::OnConnectionFailed);

	MessageThread->Start();
}

void FLiveLinkPrestonMDRSource::InitializeSettings(ULiveLinkSourceSettings* Settings)
{
	ILiveLinkSource::InitializeSettings(Settings);

	if (ULiveLinkPrestonMDRSourceSettings* MDRSettings = Cast<ULiveLinkPrestonMDRSourceSettings>(Settings))
	{
		SavedSourceSettings = MDRSettings;

		if (MessageThread)
		{
			MessageThread->SetIncomingDataMode_GameThread(SavedSourceSettings->IncomingDataMode);
		}
	}
	else
	{
		UE_LOG(LogPrestonMDRSource, Warning, TEXT("Preston MDR Source coming from Preset is outdated. Consider recreating a Preston MDR Source. Configure it and resave as preset"));
	}
}

void FLiveLinkPrestonMDRSource::OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent)
{
	ILiveLinkSource::OnSettingsChanged(Settings, PropertyChangedEvent);

	const FProperty* const MemberProperty = PropertyChangedEvent.MemberProperty;
	const FProperty* const Property = PropertyChangedEvent.Property;
	if (Property && MemberProperty && (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive))
	{
		if (SavedSourceSettings != nullptr)
		{
			const FName PropertyName = Property->GetFName();
			const FName MemberPropertyName = MemberProperty->GetFName();

			if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkPrestonMDRSourceSettings, IncomingDataMode))
			{
				if (MessageThread)
				{
					MessageThread->SetIncomingDataMode_GameThread(SavedSourceSettings->IncomingDataMode);
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkPrestonMDRSourceSettings, FocusEncoderRange))
			{
				if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Min))
				{
					SavedSourceSettings->FocusEncoderRange.Min = FMath::Clamp(SavedSourceSettings->FocusEncoderRange.Min, (uint16)0, SavedSourceSettings->FocusEncoderRange.Max);
				}
				else if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Max))
				{
					SavedSourceSettings->FocusEncoderRange.Max = FMath::Clamp(SavedSourceSettings->FocusEncoderRange.Max, SavedSourceSettings->FocusEncoderRange.Min, (uint16)0xFFFF);
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkPrestonMDRSourceSettings, IrisEncoderRange))
			{
				if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Min))
				{
					SavedSourceSettings->IrisEncoderRange.Min = FMath::Clamp(SavedSourceSettings->IrisEncoderRange.Min, (uint16)0, SavedSourceSettings->IrisEncoderRange.Max);
				}
				else if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Max))
				{
					SavedSourceSettings->IrisEncoderRange.Max = FMath::Clamp(SavedSourceSettings->IrisEncoderRange.Max, SavedSourceSettings->IrisEncoderRange.Min, (uint16)0xFFFF);
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkPrestonMDRSourceSettings, ZoomEncoderRange))
			{
				if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Min))
				{
					SavedSourceSettings->ZoomEncoderRange.Min = FMath::Clamp(SavedSourceSettings->ZoomEncoderRange.Min, (uint16)0, SavedSourceSettings->ZoomEncoderRange.Max);
				}
				else if (PropertyName == GET_MEMBER_NAME_CHECKED(FEncoderRange, Max))
				{
					SavedSourceSettings->ZoomEncoderRange.Max = FMath::Clamp(SavedSourceSettings->ZoomEncoderRange.Max, SavedSourceSettings->ZoomEncoderRange.Min, (uint16)0xFFFF);
				}
			}
		}
		else
		{
			UE_LOG(LogPrestonMDRSource, Warning, TEXT("Preston MDR Source coming from Preset is outdated. Consider recreating a Preston MDR Source. Configure it and resave as preset"));
		}
	}
}

bool FLiveLinkPrestonMDRSource::IsSourceStillValid() const
{
	return bIsConnectedToDevice;
}

bool FLiveLinkPrestonMDRSource::RequestSourceShutdown()
{
	if (MessageThread)
	{
		MessageThread->Stop();
		MessageThread.Reset();
	}

	{
		FScopeLock Lock(&PrestonSourceCriticalSection);
		if (Socket)
		{
			SocketSubsystem->DestroySocket(Socket);
			Socket = nullptr;
		}
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

void FLiveLinkPrestonMDRSource::OnConnectionLost()
{
	UE_LOG(LogPrestonMDRSource, Error, TEXT("Connection to the MDR device was lost"));
	bIsConnectedToDevice = false;

	{
		FScopeLock Lock(&PrestonSourceCriticalSection);
		// There was an unrecoverable connection loss, so we need to destroy the current socket and open a new one
		if (Socket)
		{
			SocketSubsystem->DestroySocket(Socket);
			Socket = nullptr;
		}

		// Attempt to re-open the connection to the MDR server
		OpenConnection();
	}

	if (MessageThread && Socket)
	{
		MessageThread->SetSocket(Socket);
		MessageThread->SoftReset();
	}
}

void FLiveLinkPrestonMDRSource::OnConnectionFailed()
{
	UE_LOG(LogPrestonMDRSource, Error, TEXT("Could not connect to the MDR server at %s:%d"), *ConnectionSettings.IPAddress, ConnectionSettings.PortNumber);
	bIsConnectedToDevice = false;
	bFailedToConnectToDevice = true;

	if (MessageThread)
	{
		MessageThread->Stop();
	}
}

void FLiveLinkPrestonMDRSource::OnStatusChanged(FMDR3Status InStatus)
{
	LatestMDRStatus = InStatus;
	UpdateStaticData();
}

void FLiveLinkPrestonMDRSource::UpdateStaticData()
{
	FLiveLinkStaticDataStruct PrestonMDRStaticDataStruct(FLiveLinkPrestonMDRStaticData::StaticStruct());
	FLiveLinkPrestonMDRStaticData* PrestonMDRStaticData = PrestonMDRStaticDataStruct.Cast<FLiveLinkPrestonMDRStaticData>();

	PrestonMDRStaticData->bIsFocalLengthSupported = LatestMDRStatus.bIsZoomMotorSet;
	PrestonMDRStaticData->bIsApertureSupported = LatestMDRStatus.bIsIrisMotorSet;
	PrestonMDRStaticData->bIsFocusDistanceSupported = LatestMDRStatus.bIsFocusMotorSet;

	PrestonMDRStaticData->bIsFieldOfViewSupported = false;
	PrestonMDRStaticData->bIsAspectRatioSupported = false;
	PrestonMDRStaticData->bIsProjectionModeSupported = false;

	Client->PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkPrestonMDRRole::StaticClass(), MoveTemp(PrestonMDRStaticDataStruct));
}

void FLiveLinkPrestonMDRSource::OnFrameDataReady(FLensDataPacket InData)
{
	bIsConnectedToDevice = true;

	FLiveLinkFrameDataStruct LensFrameDataStruct(FLiveLinkPrestonMDRFrameData::StaticStruct());
	FLiveLinkPrestonMDRFrameData* LensFrameData = LensFrameDataStruct.Cast<FLiveLinkPrestonMDRFrameData>();

	LastTimeDataReceived = FPlatformTime::Seconds();
	LensFrameData->WorldTime = LastTimeDataReceived.load();
	LensFrameData->MetaData.SceneTime = InData.FrameTime;

	if (SavedSourceSettings->IncomingDataMode == EFIZDataMode::CalibratedData)
	{
		LensFrameData->RawFocusEncoderValue = 0;
		LensFrameData->RawIrisEncoderValue = 0;
		LensFrameData->RawZoomEncoderValue = 0;

		LensFrameData->FocusDistance = InData.Focus;
		LensFrameData->Aperture = InData.Iris;
		LensFrameData->FocalLength = InData.Zoom;
	}
	else
	{
		LensFrameData->RawFocusEncoderValue = InData.Focus;
		LensFrameData->RawIrisEncoderValue = InData.Iris;
		LensFrameData->RawZoomEncoderValue = InData.Zoom;

		const uint16 FocusDelta = SavedSourceSettings->FocusEncoderRange.Max - SavedSourceSettings->FocusEncoderRange.Min;
		LensFrameData->FocusDistance = (FocusDelta != 0) ? FMath::Clamp((InData.Focus - SavedSourceSettings->FocusEncoderRange.Min) / (float)FocusDelta, 0.0f, 1.0f) : 0.0f;

		const uint16 IrisDelta = SavedSourceSettings->IrisEncoderRange.Max - SavedSourceSettings->IrisEncoderRange.Min;
		LensFrameData->Aperture = (IrisDelta != 0) ? FMath::Clamp((InData.Iris - SavedSourceSettings->IrisEncoderRange.Min) / (float)IrisDelta, 0.0f, 1.0f) : 0.0f;

		const uint16 ZoomDelta = SavedSourceSettings->ZoomEncoderRange.Max - SavedSourceSettings->ZoomEncoderRange.Min;
		LensFrameData->FocalLength = (ZoomDelta != 0) ? FMath::Clamp((InData.Zoom - SavedSourceSettings->ZoomEncoderRange.Min) / (float)ZoomDelta, 0.0f, 1.0f) : 0.0f;
	}

	Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(LensFrameDataStruct));
}

#undef LOCTEXT_NAMESPACE

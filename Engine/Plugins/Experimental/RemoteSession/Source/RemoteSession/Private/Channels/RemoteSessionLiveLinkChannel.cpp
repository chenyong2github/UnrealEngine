// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/RemoteSessionLiveLinkChannel.h"
#include "RemoteSession.h"
#include "Framework/Application/SlateApplication.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCMessage.h"

#include "MessageHandler/Messages.h"

#include "Engine/Engine.h"
#include "RemoteSession.h"
#include "Async/Async.h"


class FRemoteSessionLiveLinkChannelFactoryWorker : public IRemoteSessionChannelFactoryWorker
{
public:
	TSharedPtr<IRemoteSessionChannel> Construct(ERemoteSessionChannelMode InMode, TBackChannelSharedPtr<IBackChannelConnection> InConnection) const
	{
		return MakeShared<FRemoteSessionLiveLinkChannel>(InMode, InConnection);
	}

};

REGISTER_CHANNEL_FACTORY(FRemoteSessionLiveLinkChannel, FRemoteSessionLiveLinkChannelFactoryWorker, ERemoteSessionChannelMode::Read);

#define MESSAGE_ADDRESS TEXT("/RS.LiveLink")

FRemoteSessionLiveLinkChannel::FRemoteSessionLiveLinkChannel(ERemoteSessionChannelMode InRole, TSharedPtr<IBackChannelConnection, ESPMode::ThreadSafe> InConnection)
	: IRemoteSessionChannel(InRole, InConnection)
	, Connection(InConnection)
	, Role(InRole)
{
	//Init();
}


FRemoteSessionLiveLinkChannel::~FRemoteSessionLiveLinkChannel()
{
	if (Role == ERemoteSessionChannelMode::Read)
	{
		// Remove the callback so it doesn't call back on an invalid this
		Connection->RemoveRouteDelegate(MESSAGE_ADDRESS, MessageCallbackHandle);
	}
}


void FRemoteSessionLiveLinkChannel::Tick(const float InDeltaTime)
{
	// Inbound data gets handled as callbacks
	if (Role == ERemoteSessionChannelMode::Write)
	{
		//SendXRTracking();
	}
}

void FRemoteSessionLiveLinkChannel::SendLiveLinkInfo()
{
	if (Connection.IsValid())
    {
        /*if (XRSystem.IsValid() && XRSystem->IsTracking(IXRTrackingSystem::HMDDeviceId))
        {
            FVector Location;
            FQuat Orientation;
            if (XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, Orientation, Location))
            {
                FRotator Rotation(Orientation);

                TwoParamMsg<FVector, FRotator> MsgParam(Location, Rotation);
				TBackChannelSharedPtr<IBackChannelPacket> Msg = MakeShared<FBackChannelOSCMessage, ESPMode::ThreadSafe>(MESSAGE_ADDRESS);
                Msg->Write(TEXT("XRData"), MsgParam.AsData());

                Connection->SendPacket(Msg);
                
                UE_LOG(LogRemoteSession, VeryVerbose, TEXT("Sent Rotation (%.02f,%.02f,%.02f)"), Rotation.Pitch,Rotation.Yaw,Rotation.Roll);
            }
            else
            {
                 UE_LOG(LogRemoteSession, Warning, TEXT("Failed to get XRPose"));
            }
        }
        else
        {
            UE_LOG(LogRemoteSession, Warning, TEXT("XR Tracking not available to send"));
        }*/
    }
}

void FRemoteSessionLiveLinkChannel::ReceiveLiveLinkInfo(IBackChannelPacket& Message)
{
	TUniquePtr<TArray<uint8>> DataCopy = MakeUnique<TArray<uint8>>();
   // TWeakPtr<IXRTrackingSystem, ESPMode::ThreadSafe> TaskXRSystem = ProxyXRSystem;

	Message.Read(TEXT("XRData"), *DataCopy);

    AsyncTask(ENamedThreads::GameThread, [DataCopy=MoveTemp(DataCopy)]
	{
		/*TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> TaskXRSystemPinned = TaskXRSystem.Pin();
		if (TaskXRSystemPinned)
		{
			FMemoryReader Ar(*DataCopy);
			TwoParamMsg<FVector, FRotator> MsgParam(Ar);

			UE_LOG(LogRemoteSession, VeryVerbose, TEXT("Received Rotation (%.02f,%.02f,%.02f)"), MsgParam.Param2.Pitch, MsgParam.Param2.Yaw, MsgParam.Param2.Roll);

// 			FTransform NewTransform(MsgParam.Param2, MsgParam.Param1);
// 						TaskXRSystemPinned->UpdateTrackingToWorldTransform(NewTransform);
		}*/
	});
}


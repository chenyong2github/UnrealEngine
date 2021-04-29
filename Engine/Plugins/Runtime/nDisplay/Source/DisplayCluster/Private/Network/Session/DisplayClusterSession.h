// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Session/IDisplayClusterSession.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Transport/DisplayClusterSocketOperations.h"
#include "Network/Transport/DisplayClusterSocketOperationsHelper.h"
#include "Network/Packet/IDisplayClusterPacket.h"

#include "Misc/DisplayClusterConstants.h"
#include "Misc/DisplayClusterLog.h"

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "GenericPlatform/GenericPlatformAffinity.h"

class IDisplayClusterSessionStatusListener;


/**
 * Base server socket session class
 */
template <typename TPacketType, bool bIsBidirectional, bool bExitOnCommError>
class FDisplayClusterSession
	: public    IDisplayClusterSession
	, public    FRunnable
	, protected FDisplayClusterSocketOperations
	, protected FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>
{
public:
	FDisplayClusterSession(
			FSocket* Socket,
			IDisplayClusterSessionStatusListener* InStatusListener,
			IDisplayClusterSessionPacketHandler<TPacketType, bIsBidirectional>* InPacketHandler,
			uint64 InSessionId,
			const FString& InName = FString("DisplayClusterSession"),
			EThreadPriority InThreadPriority = EThreadPriority::TPri_Normal)

		: FDisplayClusterSocketOperations(Socket, DisplayClusterConstants::net::PacketBufferSize, InName)
		, FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>(*this, InName)
		, Name(InName)
		, SessionId(InSessionId)
		, StatusListener(InStatusListener)
		, PacketHandler(InPacketHandler)
		, ThreadPriority(InThreadPriority)
	{
		static_assert(std::is_base_of<IDisplayClusterPacket, TPacketType>::value, "TPacketType is not derived from IDisplayClusterPacket");

		check(InStatusListener);
		check(InPacketHandler);
	}

	virtual ~FDisplayClusterSession()
	{
		Stop();
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSession
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual FString GetName() const override final
	{
		return Name;
	}

	virtual uint64 GetSessionId() const override final
	{
		return SessionId;
	}

	virtual bool StartSession() override
	{
		StatusListener->NotifySessionOpen(SessionId);

		ThreadObj.Reset(FRunnableThread::Create(this, *(Name + FString("_thread")), 1024 * 1024, ThreadPriority, FPlatformAffinity::GetMainGameMask()));
		ensure(ThreadObj);

		UE_LOG(LogDisplayClusterNetwork, Log, TEXT("Session %s started"), *GetName());

		return true;
	}
	
	virtual void StopSession() override
	{
		Stop();
	}

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRunnable
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual uint32 Run() override
	{
		UE_LOG(LogDisplayClusterNetwork, Log, TEXT("Session thread %s has started"), *GetName());

		// Using TLS dramatically speeds up clusters with large numbers of nodes
		FMemory::SetupTLSCachesOnCurrentThread();

		while (FDisplayClusterSocketOperations::IsOpen())
		{
			// Receive a packet
			TSharedPtr<TPacketType> Request = FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>::ReceivePacket();
			if (!Request)
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Session %s: couldn't receive a request packet"), *GetName());
				break;
			}

			// Processs the request
			typename IDisplayClusterSessionPacketHandler<TPacketType, bIsBidirectional>::ReturnType Response = GetPacketHandler()->ProcessPacket(Request);
			
			// Send a response (or not, it depends on the connection type)
			const bool bResult = HandleSendResponse(Response);
			if (!bResult)
			{
				UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Session %s: couldn't send a response packet"), *GetName());
				break;
			}

			UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("A packet has been processed"), *GetName());
		}

		GetStatusListener()->NotifySessionClose(GetSessionId());

		UE_LOG(LogDisplayClusterNetwork, Log, TEXT("Session thread %s has finished"), *GetName());
		return 0;
	}

	virtual void Stop() override
	{
		GetSocket()->Close();
		ThreadObj->WaitForCompletion();
	}

protected:
	IDisplayClusterSessionStatusListener* GetStatusListener() const
	{
		return StatusListener;
	}

	IDisplayClusterSessionPacketHandler<TPacketType, bIsBidirectional>* GetPacketHandler() const
	{
		return PacketHandler;
	}

private:
	template<typename TResponseType>
	bool HandleSendResponse(const TResponseType& Response)
	{
		unimplemented();
		return false;
	}

	template<>
	bool HandleSendResponse<typename IDisplayClusterSessionPacketHandler<TPacketType, false>::ReturnType>(const typename IDisplayClusterSessionPacketHandler<TPacketType, false>::ReturnType& Response)
	{
		// Nothing to do, no responses for unidirectional services
		return true;
	}

	template<>
	bool HandleSendResponse<typename IDisplayClusterSessionPacketHandler<TPacketType, true>::ReturnType>(const typename IDisplayClusterSessionPacketHandler<TPacketType, true>::ReturnType& Response)
	{
		if (Response.IsValid())
		{
			return FDisplayClusterSocketOperationsHelper<TPacketType, bExitOnCommError>::SendPacket(Response);
		}

		return false;
	}

private:
	// Session name
	const FString Name;
	// Session ID
	const uint64 SessionId;

	// Session status listener
	IDisplayClusterSessionStatusListener* StatusListener = nullptr;
	// Session packets processor
	IDisplayClusterSessionPacketHandler<TPacketType, bIsBidirectional>* PacketHandler = nullptr;

	// Working thread priority
	const EThreadPriority ThreadPriority;

	// Session working thread
	TUniquePtr<FRunnableThread> ThreadObj;
};

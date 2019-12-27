// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Network/Service/DisplayClusterService.h"
#include "Network/Protocol/IPDisplayClusterClusterEventsProtocol.h"
#include "Network/DisplayClusterMessage.h"
#include "Dom/JsonObject.h"

/**
 * Cluster events server
 */
class FDisplayClusterClusterEventsService
	: public  FDisplayClusterService
	, private IPDisplayClusterClusterEventsProtocol
{
public:
	FDisplayClusterClusterEventsService(const FString& InAddr, const int32 InPort);
	virtual ~FDisplayClusterClusterEventsService();

public:
	enum class EDisplayClusterJsonError : uint8
	{
		Ok = 0,
		NotSupported = 1,
		MissedMandatoryFields = 2,
		UnknownError = 255
	};

public:
	virtual bool Start() override;
	virtual void Shutdown() override;

protected:
	virtual TSharedPtr<FDisplayClusterSessionBase> CreateSession(FSocket* InSocket, const FIPv4Endpoint& InEP) override;
	virtual bool IsConnectionAllowed(FSocket* InSocket, const FIPv4Endpoint& InEP)
	{ return true; }

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionListener
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void NotifySessionOpen(FDisplayClusterSessionBase* InSession) override;
	virtual void NotifySessionClose(FDisplayClusterSessionBase* InSession) override;
	virtual TSharedPtr<FJsonObject> ProcessJson(const TSharedPtr<FJsonObject>& Request) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterEventsProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event) override;

private:
	FDisplayClusterClusterEvent BuildClusterEventFromJson(const FString& EventName, const FString& EventType, const FString& EventCategory, const TSharedPtr<FJsonObject>& JsonMessage) const;

	TSharedPtr<FJsonObject> MakeResponseErrorMissedMandatoryFields();
	TSharedPtr<FJsonObject> MakeResponseErrorUnknown();
	TSharedPtr<FJsonObject> MakeResponseOk();
};

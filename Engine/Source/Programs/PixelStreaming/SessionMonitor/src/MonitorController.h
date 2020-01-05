// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

// Forward declarations
class FMonitor;

class FMonitorController
{
public:
	FMonitorController(FMonitor& Monitor);
	virtual ~FMonitorController();

protected:
	FMonitor& Monitor;
};

/**
 * Rest API implementation
 */
class FRestAPIMonitorController : public FMonitorController
{
public:
	FRestAPIMonitorController(FMonitor& Monitor, const std::string& ListenAddress, bool bServeEvents);
	~FRestAPIMonitorController();

private:

	struct FEvent
	{
		std::string Name;
		std::string Data;
	};

	// Forward declaration
	struct FEventListener;

	web::http::http_response handleCmd(const web::json::value& Data);

	void HandleGET(web::http::http_request Msg);
	void HandlePOST(web::http::http_request Msg);
	void HandleDEL(web::http::http_request Msg);
	void HandlePUT(web::http::http_request Msg);
	void HandleOPTIONS(web::http::http_request Msg);

	std::unique_ptr<web::http::experimental::listener::http_listener> Listener;
	std::unique_ptr<FEventListener> PendingEvents;
};

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

// Forward declarations
struct FAppConfig;

/// Forward declarations
class FMonitor;
class FSpawner;

//! 
// Manages a monitored App's lifetime
//
class FAppSession : public std::enable_shared_from_this<FAppSession>
{
public:

	FAppSession(FMonitor& Outer, const FAppConfig* Cfg);
	~FAppSession();

	bool Launch(boost::asio::ip::tcp::acceptor& MonitorAcceptor);
	void StartTimeoutDetection();
	void Shutdown();

	const FAppConfig* GetCfg() const
	{
		return Cfg;
	}

	enum class EState
	{
		None,
		Running,
		Frozen,
		ShuttingDown,
		Finished,
	};

	enum class EExitReason
	{
		None,
		RequestedShutdown,
		KilledAfterFreeze,
		Unexpected,
	};

	EExitReason GetExitReason() const
	{
		return ExitReason;
	}

private:

	const std::string& GetAppName() const;


	template<typename F>
	void SendMsg(const char* Type, const char* PayloadName, int Payload, F&& func);
	void StartMsgRead();
	void CheckHeartbeatDeadline(const boost::system::error_code& Ec);
	void CheckShutdownDeadline(const boost::system::error_code& Ec);

	void HandleMsg(const std::string& MsgStr);
	void OnProcessExit(int ExitCode);

	FMonitor& Outer;
	int AppCounter;
	const FAppConfig* Cfg;
	int TimeoutMs = 0;
	boost::asio::ip::tcp::socket MonitorSock;
	std::unique_ptr<FSpawner> Spawner;

	boost::asio::deadline_timer HeartbeatDeadline;
	boost::asio::deadline_timer ShutdownDeadline;

	boost::asio::streambuf RecvBuffer;
	bool bReadInProgress;

	EState State = EState::None;
	EExitReason ExitReason = EExitReason::None;
};


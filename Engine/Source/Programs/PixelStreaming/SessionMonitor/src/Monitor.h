// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

// Forward declarations
class FAppSession;
struct FAppConfig;

class IMonitorEventListener
{
public:
	virtual void OnStart() = 0;
	virtual void OnStartFailed() = 0;
	virtual void OnStop() = 0;
	virtual void OnAppCrashed(const FAppConfig* Cfg) = 0;
	virtual void OnAppFroze(const FAppConfig* Cfg) = 0;
	virtual void OnSessionTimeout() = 0;
};

//
// Puts together the entire data
//
class FMonitor
{
public:
	class FDummyMonitorEventListener : public IMonitorEventListener
	{
	public:
		void OnStart() override {}
		void OnStartFailed() override {}
		void OnStop() override {}
		void OnAppCrashed(const FAppConfig* Cfg) override { }
		void OnAppFroze(const FAppConfig* Cfg) override { }
		void OnSessionTimeout() override { }
	};

	FMonitor(boost::asio::io_service& Service, std::vector<FAppConfig> Cfg);
	~FMonitor();

	void Start();
	void Stop(bool bShutdownMonitor);

	boost::asio::io_context& GetIOContext()
	{
		return IOContext;
	}

	uint16_t GetMonitoringPort() const
	{
		return AppAcceptor->local_endpoint().port();
	}

	void SetEventListener(IMonitorEventListener* EventListener);
private:

	void StartImpl();
	void StopImpl(bool bShutdownMonitor);

	friend class FAppSession;

	/**
	 * Called from the FAppSession destructor
	 */
	void ForgetSession(FAppSession* App);

	void DoAppCrashAction(FAppSession* App);

	boost::asio::io_context& IOContext;
	std::unique_ptr<boost::asio::io_context::work> DummyWork; // Used to keep Service busy even if no apps are active
	std::vector<FAppConfig> Cfg;
	std::vector<FAppSession*> Sessions;
	bool bRestartingAll = false;

	enum class EState
	{
		None,
		Starting,
		Running,
		Stopping
	};
	EState State = EState::None;

	// Acceptor for the monitored apps
	// The apps connect to this acceptor, to exchange data with the SessionMonitor
	std::shared_ptr<boost::asio::ip::tcp::acceptor> AppAcceptor;

	IMonitorEventListener* EventListener;

};


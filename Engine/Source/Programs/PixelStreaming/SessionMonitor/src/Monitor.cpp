// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"
#include "Monitor.h"
#include "Logging.h"
#include "Algorithm.h"
#include "Spawner.h"
#include "StringUtils.h"
#include "AppSession.h"
#include "Config.h"
#include "ScopeGuard.h"

FMonitor::FMonitor(boost::asio::io_context& IOContext, std::vector<FAppConfig> Cfg)
	: IOContext(IOContext)
	, Cfg(std::move(Cfg))
{
	static FDummyMonitorEventListener DummyListener;
	EventListener = &DummyListener;
	DummyWork = std::make_unique<boost::asio::io_context::work>(IOContext);
}

FMonitor::~FMonitor()
{
	verify(Sessions.size() == 0);
}

void FMonitor::StartImpl()
{
	CHECK_MAINTHREAD();
	bRestartingAll = false;

	auto StartFailedGuard = ScopeGuard([&]
	{
		if (State == EState::Starting)
		{
			State = EState::None;
		}
		EventListener->OnStartFailed();
	});

	if (State!=EState::None)
	{
		EG_LOG(LogDefault, Warning, "Can't start, because there is already a session running");
		return;
	}
	verify(Sessions.size() == 0);
	State = EState::Starting;

	// Start acceptor if required
	if (!AppAcceptor)
	{
		// NOTE: Specifying port as 0, will let the OS pick an ephemeral port
		boost::asio::ip::tcp::endpoint Endpoint(boost::asio::ip::tcp::v4(), 0);
		auto TmpAcceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(IOContext);
		TmpAcceptor->open(Endpoint.protocol());
		boost::system::error_code Ec;
		TmpAcceptor->bind(Endpoint, Ec);
		if (Ec)
		{
			EG_LOG(LogDefault, Error, "Error binding acceptor. Reason=%s", Ec.message().c_str());
			return;
		}

		TmpAcceptor->listen(1, Ec);
		if (Ec)
		{
			EG_LOG(LogDefault, Error, "Error calling listen on the acceptor. Reason=%s", Ec.message().c_str());
			return;
		}

		TmpAcceptor->non_blocking(true, Ec);
		if (Ec)
		{
			EG_LOG(LogDefault, Error, "Could not set Acceptor to non-blocking. Reason=%s", Ec.message().c_str());
			return;
		}

		AppAcceptor = TmpAcceptor;
		EG_LOG(LogDefault, Log, "Using port %d for communicating with child apps", (int)GetMonitoringPort());
	}

	//
	// Start all applications
	//
	// Launching and starting timeout detection is done in separate steps, so we
	// can launch all apps and not worry about one launched app timing out because we are still launching
	// another one that takes a long time to launch by design.
	//
	std::vector<std::shared_ptr<FAppSession>> All;
	for (FAppConfig& AppCfg : Cfg)
	{
		auto Session = std::make_shared<FAppSession>(*this, &AppCfg);
		bool Res = Session->Launch(*AppAcceptor);
		if (!Res)
		{
			for (FAppSession* Session : Sessions)
			{
				Session->Shutdown();
			}
			return;
		}
		All.push_back(Session); // Keeping a strong reference, while we launch all the other apps
		Sessions.push_back(Session.get());
	}
	//
	// Now that all are launched, initiate the timeout detection
	for (FAppSession* Session : Sessions)
	{
		Session->StartTimeoutDetection();
	}

	StartFailedGuard.Dismiss();
	EG_LOG(LogDefault, Log, "Raising event START.");
	EventListener->OnStart();
	State = EState::Running;
}

void FMonitor::Start()
{
	boost::asio::post(IOContext, [this]()
	{
		StartImpl();
	});
}

void FMonitor::StopImpl(bool bShutdownMonitor)
{
	CHECK_MAINTHREAD();
	if (State==EState::None)
	{
		EG_LOG(LogDefault, Warning, "Can't initiate stop, since there is no session running");
	}
	else if (State==EState::Starting)
	{
		EG_LOG(LogDefault, Warning, "Can't initiate stop when starting");
	}
	else if (State == EState::Running)
	{
		EG_LOG(LogDefault, Log, "Initiating stop");
		State = EState::Stopping;
		for (FAppSession* Session : Sessions)
		{
			Session->Shutdown();
		}
	}
	else if (State == EState::Stopping)
	{
		EG_LOG(LogDefault, Warning, "Can't initiate stop, since it's stopping already");
	}

	if (bShutdownMonitor)
	{
		DummyWork.reset();
	}
}

void FMonitor::Stop(bool bShutdownMonitor)
{
	boost::asio::post(IOContext, [this, bShutdownMonitor]()
	{
		StopImpl(bShutdownMonitor);
	});
}

void FMonitor::ForgetSession(FAppSession* App)
{
	CHECK_MAINTHREAD();
	Erase(Sessions, App);

	bool doAppCrashAction = false;

	switch (App->GetExitReason())
	{
	case FAppSession::EExitReason::None:
	case FAppSession::EExitReason::RequestedShutdown:
		// Nothing to do
		break;
	case FAppSession::EExitReason::KilledAfterFreeze:
		EG_LOG(LogDefault, Log, "Raising event APPFROZE.");
		EventListener->OnAppFroze(App->GetCfg());
		doAppCrashAction = true;
		break;
	case FAppSession::EExitReason::Unexpected:
		EG_LOG(LogDefault, Log, "Raising event APPCRASHED.");
		EventListener->OnAppCrashed(App->GetCfg());
		doAppCrashAction = true;
		break;
	default:
		// Nothing to do
		verify(0);
	}

	if (doAppCrashAction && State == EState::Running)
	{
		DoAppCrashAction(App);
	}

	if ((Sessions.size() == 0) && ((State == EState::Running) || (State == EState::Stopping)))
	{
		EG_LOG(LogDefault, Log, "All apps terminated. Raising event STOP.");
		EventListener->OnStop();
		State = EState::None;
		if (bRestartingAll)
		{
			StartImpl();
		}
	}
}

void FMonitor::SetEventListener(IMonitorEventListener* EventListener_)
{
	CHECK_MAINTHREAD();
	EventListener = EventListener_;
}

void FMonitor::DoAppCrashAction(FAppSession* App)
{
	CHECK_MAINTHREAD();

	switch (App->GetCfg()->OnCrashAction)
	{
	case EAppCrashAction::None:
		EG_LOG(LogDefault, Log, "No app oncrash action to perform");
		break;
	case EAppCrashAction::StopSession:
		EG_LOG(LogDefault, Log, "Performing 'StopSession' oncrash action");
		if (State != EState::Stopping)
		{
			StopImpl(false);
		}
		break;
	case EAppCrashAction::RestartApp:
	{
		EG_LOG(LogDefault, Log, "Performing 'RestartApp' oncrash action");
		auto Session = std::make_shared<FAppSession>(*this, App->GetCfg());
		bool res = Session->Launch(*AppAcceptor);
		if (!res)
		{
			EG_LOG(LogDefault, Error, "App restart failed. Shutting down session...");
			if (State != EState::Stopping)
			{
				StopImpl(false);
			}
		}
		Sessions.push_back(Session.get());
		Session->StartTimeoutDetection();
	}
	break;
	case EAppCrashAction::RestartSession:
		EG_LOG(LogDefault, Log, "Performing 'RestartSession' oncrash action");
		if (!bRestartingAll)
		{
			bRestartingAll = true;
			if (State != EState::Stopping)
			{
				StopImpl(false);
			}
		}
		break;
	}
}

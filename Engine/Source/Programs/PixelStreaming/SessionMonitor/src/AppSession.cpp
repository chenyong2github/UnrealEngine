// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"

#include "AppSession.h"
#include "Monitor.h"
#include "Spawner.h"
#include "Logging.h"
#include "Utils.h"
#include "Config.h"
#include "StringUtils.h"

FAppSession::FAppSession(FMonitor& Outer_, const FAppConfig* Cfg_)
	: Outer(Outer_)
	, Cfg(Cfg_)
	, MonitorSock(Outer.GetIOContext())
	, HeartbeatDeadline(Outer.GetIOContext())
	, ShutdownDeadline(Outer.GetIOContext())
	, bReadInProgress(false)
{
	CHECK_MAINTHREAD();
	static int Counter = 1;
	AppCounter = Counter++;
	APPLOG(Log, "Creating FAppSession (AppCounter=%d)", AppCounter);
	TimeoutMs = Cfg->InitialTimeoutMs;
}

FAppSession::~FAppSession()
{
	CHECK_MAINTHREAD();
	APPLOG(Log, "Destroying FAppSession (AppCounter=%d)", AppCounter);
	Outer.ForgetSession(this);
}

void FAppSession::StartMsgRead()
{
	if (bReadInProgress)
	{
		APPLOG(Fatal, "Attempted to initiate a read operation when there is already one ongoing.", AppCounter);
		return;
	}

	bReadInProgress = true;

	HeartbeatDeadline.expires_from_now(boost::posix_time::milliseconds(TimeoutMs));
	HeartbeatDeadline.async_wait([this_ = shared_from_this(), this](const boost::system::error_code& Ec)
	{
		CheckHeartbeatDeadline(Ec);
	});

	boost::asio::async_read_until(
		MonitorSock, RecvBuffer, '\0',
		[this_(this->shared_from_this()), this](const boost::system::error_code& Ec, size_t Transfered)
	{
		// NOTE:
		// - Asio documentation states 'Transfered' is the number of bytes in the dynamic buffer's sequence get area
		// up to and including the delimiter.
		// - After a successful async_read_until operation, the dynamic buffer sequence may contain additional data beyond
		// the delimiter. An application will typically leave that data in the dynamic buffer sequence for a subsequent
		// async_read_until operation to examine.
		bReadInProgress = false;

		// For simplicity, any kind of error causes the app to be killed if it's not in shutdown mode already
		// When in shutdown mode, it's not necessary to kill it here, because the ShutdownDeadline timer will do it
		if (Ec)
		{
			if (State != EState::ShuttingDown)
			{
				APPLOG(Error, "Failed to receive message. Killing app. Reason=%s", Ec.message().c_str());
				State = EState::Frozen;
				Spawner = nullptr;
			}
		}
		else
		{
			std::string MsgStr(Transfered, 0);
			std::istream In(&RecvBuffer);
			In.read(&MsgStr[0], Transfered);
			// Remove any nonvisible characters at the end, like \n, \t, etc.
			while (MsgStr.back() < 32)
			{
				MsgStr.pop_back();
			}

			HandleMsg(MsgStr);

			StartMsgRead();
		}
	});

}

void FAppSession::HandleMsg(const std::string& MsgStr)
{
	APPLOG(Verbose, "Received MSG '%s'", MsgStr.c_str());

	try
	{
		web::json::value Msg = web::json::value::parse(Widen(MsgStr));
		std::string Type = Narrow(Msg.at(L"type").as_string());

		if (Type == "heartbeat")
		{
			// nothing to do
		}
		else if (Type == "change_heartbeat")
		{
			TimeoutMs = Msg.at(L"timeoutms").as_integer();
		}
	}
	catch (std::exception& e)
	{
		EG_LOG(LogDefault, Error, "Exception while handling message: %s", e.what());
	}

}

const std::string& FAppSession::GetAppName() const
{
	return Cfg->Name;
}

template<typename F>
void FAppSession::SendMsg(const char* Type, const char* PayloadName, int Payload, F&& Func)
{
	web::json::value Msg;
	Msg[L"type"] = web::json::value(Widen(Type));
	if (PayloadName)
	{
		Msg[Widen(PayloadName).c_str()] = Payload;
	}

	// Keep a shared pointer of the string to send, so it survives the async operation
	auto MsgStr = std::make_shared<std::string>(Narrow(Msg.serialize()));
	// Send a null terminated string
	boost::asio::async_write(
		MonitorSock, boost::asio::buffer(MsgStr->c_str(), MsgStr->size()+1),
		[this_(this->shared_from_this()), MsgStr, Func(std::forward<F>(Func))](const boost::system::error_code& Ec, size_t Transfered)
	{
		Func(Ec, Transfered);
	});
}

bool FAppSession::Launch(boost::asio::ip::tcp::acceptor& MonitorAcceptor)
{
	if (State != EState::None)
	{
		APPLOG(Error, "Can't launch when in state %d", (int)State);
		return false;
	}

	verify(Spawner == nullptr);
	Spawner = std::make_unique<FSpawner>(Cfg, Outer.GetMonitoringPort());
	bool Res = Spawner->Launch([this_(shared_from_this()), this](int ExitCode)
	{
		Outer.GetIOContext().post(
			[this_(shared_from_this()), this, ExitCode]()
		{
			OnProcessExit(ExitCode);
		});
	});
	
	if (!Res)
	{
		Spawner = nullptr;
		return false;
	}

	if (Cfg->bMonitored)
	{
		boost::system::error_code Ec;

		// Try to accept without blocking, so we can timeout
		std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
		do 
		{
			MonitorAcceptor.accept(MonitorSock, Ec);
			std::chrono::steady_clock::time_point NowTime = std::chrono::steady_clock::now();
			long long Ms = std::chrono::duration_cast<std::chrono::milliseconds>(NowTime - StartTime).count();
			if (Ec)
			{
				if (Ms > Cfg->InitialTimeoutMs)
				{
					APPLOG(Error, "Timeout trying to connec to to child app. Killing app.'");
					Spawner = nullptr;
					return false;
				}
				else if  (Ec.value()==boost::asio::error::would_block)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
				}
				else
				{
					APPLOG(Error, "Could not connect to child app. Killing app. Reason=%s", Ec.message().c_str());
					Spawner = nullptr;
					return false;
				}
			}
			else
			{
				break;
			}
		} while (true);

		APPLOG(Log, "Connected to %s", AddrToString(MonitorSock.remote_endpoint()).c_str());
	}
	else
	{
		APPLOG(Log, "App set to unmonitored mode (doesn't call back to SessionMonitor)");
	}

	State = EState::Running;
	return true;
}

void FAppSession::CheckHeartbeatDeadline(const boost::system::error_code& Ec)
{
	APPLOG(VeryVerbose, "%s: State=%d, Ec=%s", __FUNCTION__, int(State), Ec.message().c_str());

	if (State != EState::Running || Ec.value()==boost::asio::error::operation_aborted)
	{
		return;
	}

	// Check if the deadline has passed.
	// We compare the deadline against the current time, since a new asynchronous operation
	// may have moved the deadline before this callback was called
	if (HeartbeatDeadline.expires_at() <= boost::asio::deadline_timer::traits_type::now())
	{
		APPLOG(Error, "Heartbeat timeout. Killing app.");
		State = EState::Frozen;
		Spawner = nullptr;

		// Set deadline to infinite, so that if this callback is called again,
		// it will take no action unless a new deadline was set
		HeartbeatDeadline.expires_at(boost::posix_time::pos_infin);
	}
}

void FAppSession::CheckShutdownDeadline(const boost::system::error_code& Ec)
{
	APPLOG(VeryVerbose, "%s: State=%d, Ec=%s", __FUNCTION__, int(State), Ec.message().c_str());
	if (State == EState::Finished || Ec.value()==boost::asio::error::operation_aborted)
	{
		return;
	}

	APPLOG(Error, "Failed to cleanly shutdown within the allowed time. Forcibly killing the process");
	State = EState::Frozen;
	Spawner = nullptr;
	return;
}

void FAppSession::StartTimeoutDetection()
{
	CHECK_MAINTHREAD();
	if (Cfg->bMonitored)
	{
		StartMsgRead();
	}
}

void FAppSession::Shutdown()
{
	CHECK_MAINTHREAD();
	State = EState::ShuttingDown;

	if (Cfg->bMonitored)
	{
		APPLOG(Log, "App is being monitored, so trying a clean shutdown.")
		ShutdownDeadline.expires_from_now(boost::posix_time::milliseconds(Cfg->ShutdownTimeoutMs));
		ShutdownDeadline.async_wait([this_ = shared_from_this(), this](const boost::system::error_code& Ec)
		{
			CheckShutdownDeadline(Ec);
		});

		SendMsg("exit", nullptr, 0, [this](const boost::system::error_code& Ec, size_t Transfered)
		{
		});
	}
	else
	{
		APPLOG(Log, "App is in unmonitored mode, so no clean shutdown available. Forcibly killing the process");
		Spawner = nullptr;
	}

}

void FAppSession::OnProcessExit(int ExitCode)
{
	CHECK_MAINTHREAD();

	switch (State)
	{
	case EState::None:
		// Failed to launch. Nothing to do
		break;
	case EState::Running:
		APPLOG(Warning, "Process ended with code %d, without shutdown request", ExitCode);
		ExitReason = EExitReason::Unexpected;
		break;
	case EState::Frozen:
		APPLOG(Log, "Process ended with code %d, after being killed due to freeze detection or misbehaving", ExitCode);
		ExitReason = EExitReason::KilledAfterFreeze;
		break;
	case EState::ShuttingDown:
		APPLOG(Log, "Process ended with code %d, after a shutdown request", ExitCode);
		ExitReason = EExitReason::RequestedShutdown;
		break;
	default:
		verify(0);
	}

	State = EState::Finished;
	ShutdownDeadline.cancel();
	HeartbeatDeadline.cancel();
}

// Copyright Epic Games, Inc. All Rights Reserved.


/*
Links with useful information:

Best practice for return codes with rest apis
https://www.restapitutorial.com/httpstatuscodes.html

*/

#include "SessionMonitorPCH.h"
#include "MonitorController.h"
#include "StringUtils.h"
#include "Logging.h"
#include "Monitor.h"
#include "Config.h"


struct FRestAPIMonitorController::FEventListener : public IMonitorEventListener
{
	void OnStart() override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"started", ""});
	}
	void OnStartFailed() override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"startfailed", ""});
	}
	void OnStop() override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"stopped", ""});
	}
	void OnAppCrashed(const FAppConfig* Cfg) override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"appcrashed", Cfg->Name});
	}
	void OnAppFroze(const FAppConfig* Cfg) override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"appfroze", Cfg->Name});
	}
	void OnSessionTimeout() override
	{
		std::unique_lock<std::mutex> lk(Mtx);
		Events.push_back({"sessiontimeout", ""});
	}
	std::mutex Mtx;
	std::vector<FEvent> Events;
};

FMonitorController::FMonitorController(FMonitor& Monitor_)
	: Monitor(Monitor_)
{
}

FMonitorController::~FMonitorController()
{
}

//
// Rest API implementation
//
//
// Examples of http servers done with cpprestsdk:
// https://blogs.msdn.microsoft.com/christophep/2017/07/01/write-your-own-rest-web-server-using-c-using-cpp-rest-sdk-casablanca/
//
FRestAPIMonitorController::FRestAPIMonitorController(FMonitor& Monitor, const std::string& ListenAddress, bool bServeEvents)
	: FMonitorController(Monitor)
{
	web::uri_builder Uri(Widen(ListenAddress));

	auto Addr = Uri.to_uri().to_string();

	if (bServeEvents)
	{
		PendingEvents = std::make_unique<FRestAPIMonitorController::FEventListener>();
		Monitor.SetEventListener(PendingEvents.get());
	}

	Listener = std::make_unique<web::http::experimental::listener::http_listener>(Addr);

	// #TODO : Maybe add support for GET,PUT,DEL ?
	Listener->support(web::http::methods::GET, std::bind(&FRestAPIMonitorController::HandleGET, this, std::placeholders::_1));
	Listener->support(web::http::methods::POST, std::bind(&FRestAPIMonitorController::HandlePOST, this, std::placeholders::_1));
	Listener->support(web::http::methods::DEL, std::bind(&FRestAPIMonitorController::HandleDEL, this, std::placeholders::_1));
	Listener->support(web::http::methods::PUT, std::bind(&FRestAPIMonitorController::HandlePUT, this, std::placeholders::_1));

	Listener->support(web::http::methods::OPTIONS, std::bind(&FRestAPIMonitorController::HandleOPTIONS, this, std::placeholders::_1));

	Listener->open().wait();
}

FRestAPIMonitorController::~FRestAPIMonitorController()
{
	Listener->close().wait();
}

static web::http::http_response CreateReply(web::http::status_code Code, const web::json::value& Reply = web::json::value::object())
{
	web::http::http_response Res(Code);
	Res.headers().add(U("Access-Control-Allow-Origin"), U("*"));
	Res.headers().add(U("Access-Control-Allow-Methods"), U("POST, OPTIONS"));
	Res.headers().add(U("Access-Control-Allow-Headers"), U("Content-Type"));
	Res.set_body(Reply);
	return Res;
}

static web::http::http_response CreateBadRequestReply()
{
	return CreateReply(web::http::status_codes::BadRequest);
}

void FRestAPIMonitorController::HandleGET(web::http::http_request Msg)
{
	EG_LOG(LogDefault, Log, "GET received: %s", Narrow(Msg.to_string()).c_str());
	Msg.reply(CreateReply(web::http::status_codes::ServiceUnavailable));
}

void FRestAPIMonitorController::HandlePOST(web::http::http_request Msg)
{
	EG_LOG(LogDefault, Log, "POST received: %s", Narrow(Msg.to_string()).c_str());
	try
	{
		web::json::value Data = Msg.extract_json(true).get();
		web::http::http_response reply = handleCmd(Data);
		Msg.reply(std::move(reply));
	}
	catch (std::exception& e)
	{
		EG_LOG(LogDefault, Error, "Exception: %s", e.what());
		Msg.reply(CreateReply(web::http::status_codes::InternalError));
	}
}

void FRestAPIMonitorController::HandleDEL(web::http::http_request Msg)
{
	EG_LOG(LogDefault, Log, "DEL received: %s", Narrow(Msg.to_string()).c_str());
	Msg.reply(CreateReply(web::http::status_codes::ServiceUnavailable));
}

void FRestAPIMonitorController::HandlePUT(web::http::http_request Msg)
{
	EG_LOG(LogDefault, Log, "PUT received: %s", Narrow(Msg.to_string()).c_str());
	Msg.reply(CreateReply(web::http::status_codes::ServiceUnavailable));
}

void FRestAPIMonitorController::HandleOPTIONS(web::http::http_request Msg)
{
	EG_LOG(LogDefault, Log, "OPTIONS received: %s", Narrow(Msg.to_string()).c_str());

	web::http::http_response Res(web::http::status_codes::OK);
    Res.headers().add(U("Allow"), U("POST, OPTIONS"));
	Res.headers().add(U("Access-Control-Allow-Origin"), U("*"));
	Res.headers().add(U("Access-Control-Allow-Methods"), U("POST, OPTIONS"));
	Res.headers().add(U("Access-Control-Allow-Headers"), U("Content-Type"));
	Msg.reply(Res);
}

static bool JsonGetString(const web::json::object& Obj, const wchar_t* Field, std::string& Dst)
{
	try
	{
		Dst = Narrow(Obj.at(Field).as_string());
		return true;
	}
	catch (web::json::json_exception&)
	{
		return false;
	}
}

static bool JsonGetObject(const web::json::object& Obj, const wchar_t* Field, const web::json::object** Dst)
{
	try
	{
		*Dst = &(Obj.at(Field).as_object());
		return true;
	}
	catch (web::json::json_exception&)
	{
		return false;
	}
}

web::http::http_response FRestAPIMonitorController::handleCmd(const web::json::value& Data)
{
	if (!Data.is_object())
	{
		return CreateBadRequestReply();
	}
	const web::json::object& Obj = Data.as_object();

	std::string Cmd;
	const web::json::object* Params;
	if (!(JsonGetString(Obj, L"cmd", Cmd) && JsonGetObject(Obj, L"params", &Params)))
	{
		return CreateBadRequestReply();
	}


	web::json::value Body = web::json::value::object();
	Body[L"reply"] = web::json::value::object();

	if (Cmd == "start")
	{
		Monitor.Start();
	}
	else if (Cmd == "stop")
	{
		Monitor.Stop(false);
	}
	else if (Cmd == "getevents")
	{
		// Nothing to do
	}

	//
	// Add all pending events to all commands
	//
	std::vector<FEvent> Events;
	{
		std::unique_lock<std::mutex>(PendingEvents->Mtx);
		std::swap(Events, PendingEvents->Events);
	}
	std::vector<web::json::value> JsonEvents;
	for (FEvent& E : Events)
	{
		web::json::value Obj = web::json::value::object();
		Obj[L"name"] = web::json::value::string(Widen(E.Name));
		Obj[L"data"] = web::json::value::string(Widen(E.Data));
		JsonEvents.push_back(std::move(Obj));
	}
	Body[L"events"] = web::json::value::array(std::move(JsonEvents));

	return CreateReply(web::http::status_codes::OK, Body);
}

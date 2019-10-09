// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"
#include "Config.h"
#include "Logging.h"
#include "StringUtils.h"
#include "Utils.h"


template<typename ENUM>
ENUM GetJsonEnum(const web::json::value& JSon, const wchar_t* Name, const std::vector<std::string>& Choices, ENUM Default)
{
	if (!JSon.has_field(Name))
	{
		return Default;
	}

	std::string Val = Narrow(JSon.at(Name).as_string());
	for (size_t i = 0; i < Choices.size(); i++)
	{
		if (CiEquals(Choices[i], Val))
		{
			return static_cast<ENUM>(i);
		}
	}

	std::string Options;
	for (const std::string& C : Choices)
	{
		if (Options.size())
			Options += ", ";
		Options += "'" + C + "'";
	}
	EG_LOG(LogDefault, Fatal, "'%s' is not a valid value for field '%s'. Options are (%s)", Val.c_str(), Narrow(Name).c_str(), Options.c_str());
	return Default;
}

int GetJsonInteger(const web::json::value& Json, const wchar_t* Name, int Default)
{
	if (!Json.has_field(Name))
	{
		return Default;
	}

	return Json.at(Name).as_integer();
}

bool GetJsonBool(const web::json::value& Json, const wchar_t* Name, bool Default)
{
	if (!Json.has_field(Name))
	{
		return Default;
	}

	return Json.at(Name).as_bool();
}

std::string GetJsonString(const web::json::value& Json, const wchar_t* Name, const char* Default)
{
	if (!Json.has_field(Name))
	{
		return Default;
	}

	return Narrow(Json.at(Name).as_string());
}

std::vector<FAppConfig> ReadConfig(const std::string& ConfigFilename)
{
	std::string FinalFilename;
	if (!FullPath(FinalFilename, ConfigFilename))
	{
		EG_LOG(LogDefault, Fatal, "Failed to open config file '%s'", ConfigFilename.c_str());
		return {};
	}

	EG_LOG(LogDefault, Log, "Reading config file '%s'", FinalFilename.c_str());

	std::ifstream In;
	In.open(FinalFilename);
	if (!In.is_open())
	{
		EG_LOG(LogDefault, Fatal, "Failed to open config file '%s'", ConfigFilename.c_str());
		return {};
	}

	std::vector<FAppConfig> Res;
	web::json::value Json = web::json::value::parse(In);

	try
	{
		web::json::array Apps = Json.at(L"apps").as_array();
		for (web::json::value& App : Apps)
		{
			FAppConfig Cfg;
			Cfg.Name = Narrow(App.at(L"name").as_string());
			Cfg.Exe = Narrow(App.at(L"executable").as_string());
			// If its a relative path, then convert to full path
			if (Cfg.Exe[0] == '.' || Cfg.Exe[0] == '\\' || Cfg.Exe[0] == '/')
			{
				verify(FullPath(Cfg.Exe, Cfg.Exe, RootDir));
			}
			Cfg.Params = GetJsonString(App, L"parameters", "");

			//
			// Working directory defaults to the executable's folder
			//
			std::pair<std::string, std::string> FolderAndFile = GetFolderAndFile(Cfg.Exe);
			Cfg.WorkingDirectory = GetJsonString(App, L"working_directory", FolderAndFile.first.c_str());
			if (Cfg.WorkingDirectory.size())
			{
				verify(FullPath(Cfg.WorkingDirectory, Cfg.WorkingDirectory, RootDir));
			}

			Cfg.InitialTimeoutMs = GetJsonInteger(App, L"initial_timeout", Cfg.InitialTimeoutMs);
			Cfg.ShutdownTimeoutMs = GetJsonInteger(App, L"shutdown_timeout", Cfg.ShutdownTimeoutMs);
			Cfg.OnCrashAction = GetJsonEnum(App, L"oncrash", { "None", "StopSession", "RestartApp", "RestartSession"}, Cfg.OnCrashAction);
			Cfg.bMonitored = GetJsonBool(App, L"monitored", Cfg.bMonitored);
			Cfg.ParameterPrefix = GetJsonString(App, L"parameter_prefix", Cfg.ParameterPrefix.c_str());
			Res.push_back(Cfg);
		}
	}
	catch (web::json::json_exception& e)
	{
		EG_LOG(LogDefault, Fatal, "Error loading config: %s", e.what());
		return {};
	}

	return Res;
}




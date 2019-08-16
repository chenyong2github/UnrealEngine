// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_Environment.h"
#include "LC_Logging.h"
#include "Windows/WindowsHWrapper.h"


namespace
{
	static const DWORD MAX_ENVIRONMENT_VARIABLE_SIZE = 1024u;
}


void environment::RemoveVariable(const wchar_t* variable)
{
	const BOOL result = ::SetEnvironmentVariableW(variable, NULL);
	if (result == 0)
	{
		LC_LOG_DEV("Could not remove environment variable %S (Error: %d)", variable, ::GetLastError());
	}
}


void environment::SetVariable(const wchar_t* variable, const wchar_t* value)
{
	const BOOL result = ::SetEnvironmentVariableW(variable, value);
	if (result == 0)
	{
		LC_LOG_DEV("Could not set environment variable %S to value %S (Error: %d)", variable, value, ::GetLastError());
	}
}


std::wstring environment::GetVariable(const wchar_t* variable, const wchar_t* defaultValue)
{
	std::wstring value;
	value.resize(MAX_ENVIRONMENT_VARIABLE_SIZE);

	const DWORD result = ::GetEnvironmentVariableW(variable, &value[0], MAX_ENVIRONMENT_VARIABLE_SIZE);
	if (result == 0)
	{
		// if a default value was provided, return that instead
		if (defaultValue)
		{
			return std::wstring(defaultValue);
		}
		else
		{
			return std::wstring();
		}
	}

	return value;
}

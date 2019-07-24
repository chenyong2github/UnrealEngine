// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include <string>

namespace environment
{
	// Removes a variable from the environment of the calling process.
	void RemoveVariable(const wchar_t* variable);

	// Sets a variable in the environment of the calling process.
	void SetVariable(const wchar_t* variable, const wchar_t* value);

	// Gets a variable from the environment of the calling process.
	std::wstring GetVariable(const wchar_t* variable, const wchar_t* defaultValue);
}

// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once
//#pragma warning(disable:4668)
//#pragma warning(disable:4005)  

#include <Windows.h>

//#pragma warning(default:4005)
//#pragma warning(default:4668)

namespace WindowsMixedReality
{
	class HoloLensBuildLib
	{
	public:
		bool PackageProject(const wchar_t* StreamPath, bool PathIsActuallyPackage, const wchar_t* Params, UINT*& OutProcessId);
	};
}


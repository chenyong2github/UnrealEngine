// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "comdef.h"

#include <conio.h>
#include <objbase.h>
#include <comutil.h>
#include <stdio.h>
#include <string>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// convert Windows style enums in Linux, i.e. BMDPixelFormat to _BMDPixelFormat
#define ENUM(x) x

struct IDeckLinkIterator;
struct IDeckLinkVideoConversion;
struct IDeckLink;

namespace BlackmagicPlatform
{
	template<class T>
	std::string GetName(T* InObject)
	{
		if (InObject)
		{
			BSTR str;
			std::string stdStr;

			if (SUCCEEDED(InObject->GetName(&str)))
			{
				_bstr_t myBStr(str);
				stdStr = myBStr;
				SysFreeString(str);
			}

			return stdStr;
		}
		return std::string();
	}

	bool InitializeAPI();
	void ReleaseAPI();

	IDeckLinkIterator* CreateDeckLinkIterator();
	void DestroyDeckLinkIterator(IDeckLinkIterator*);

	IDeckLinkVideoConversion* CreateDeckLinkVideoConversion();
	void DestroyDeckLinkVideoConversion(IDeckLinkVideoConversion*);

	void SetThreadPriority_TimeCritical(std::thread& InThread);

	bool GetDisplayName(IDeckLink* Device, TCHAR* OutDisplayName, int32_t Size); 

	void* Allocate(uint32_t BufferSize);
	bool Free(void* Address, uint32_t BufferSize);
}

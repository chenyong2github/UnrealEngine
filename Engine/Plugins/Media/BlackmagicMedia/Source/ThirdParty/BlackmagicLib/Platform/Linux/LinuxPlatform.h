// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <codecvt>
#include <locale>
#include <thread>


// provide string the way Unreal Engine expects
#define TCHAR char16_t

// convert Windows style enums in Linux, i.e. BMDPixelFormat to _BMDPixelFormat
#define ENUM(x) _ ## x

// simulate Windows TEXT macro
#define TEXT(x) u##x

// simulate Windows BOOL type
typedef bool BOOL;
#define FALSE false
#define TRUE true

struct IDeckLinkIterator;
struct IDeckLinkVideoConversion;

namespace BlackmagicPlatform
{
	template<class T>
	std::string GetName(T* InObject)
	{
		if (InObject)
		{
			const char* Str;
			InObject->GetName(&Str);
			std::string StandardStr(Str);
			free((void *)Str);
			return StandardStr;
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

	void* Allocate(uint32_t Size);
	bool Free(void* Address, uint32_t Size);
}

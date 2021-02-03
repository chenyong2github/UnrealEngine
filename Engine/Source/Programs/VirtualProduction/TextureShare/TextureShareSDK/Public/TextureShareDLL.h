// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef TEXTURE_SHARE_SDK_DLL
#define TEXTURE_SHARE_SDK_API __declspec(dllexport)
#else
#define TEXTURE_SHARE_SDK_API __declspec(dllimport)
#endif

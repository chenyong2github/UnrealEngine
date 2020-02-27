// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if !defined(USE_BCRYPT)
	#define USE_BCRYPT 0
#endif

#if USE_BCRYPT
	#include "EncryptionContextBCrypt.h"
#elif PLATFORM_SWITCH
	#include "EncryptionContextSwitch.h"
#else
	#include "EncryptionContextOpenSSL.h"
#endif
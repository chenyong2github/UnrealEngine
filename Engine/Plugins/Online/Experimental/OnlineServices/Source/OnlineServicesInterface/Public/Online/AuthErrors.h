// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineError.h"

 // RobC mod- whole file
namespace UE::Online::Errors {

inline FOnlineError InvalidCredentials() { return FOnlineError(TEXT("invalid_credentials")); }

/* UE::Online::Errors */ }

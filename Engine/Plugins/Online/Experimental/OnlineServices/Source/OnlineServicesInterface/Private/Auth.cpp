// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Auth.h"

namespace UE::Online {

const TCHAR* LexToString(ELoginStatus Status)
{
	switch (Status)
	{
	case ELoginStatus::UsingLocalProfile:	return TEXT("UsingLocalProfile");
	case ELoginStatus::LoggedIn:			return TEXT("LoggedIn");
	default:								checkNoEntry(); // Intentional fallthrough
	case ELoginStatus::NotLoggedIn:			return TEXT("NotLoggedIn");
	}
}

void LexFromString(ELoginStatus& OutStatus, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("LoggedIn")) == 0)
	{
		OutStatus = ELoginStatus::LoggedIn;
	}
	else if (FCString::Stricmp(InStr, TEXT("UsingLocalProfile")) == 0)
	{
		OutStatus = ELoginStatus::UsingLocalProfile;
	}
	else if (FCString::Stricmp(InStr, TEXT("NotLoggedIn")) == 0)
	{
		OutStatus = ELoginStatus::NotLoggedIn;
	}
	else
	{
		checkNoEntry();
		OutStatus = ELoginStatus::NotLoggedIn;
	}
}

/* UE::Online */ }

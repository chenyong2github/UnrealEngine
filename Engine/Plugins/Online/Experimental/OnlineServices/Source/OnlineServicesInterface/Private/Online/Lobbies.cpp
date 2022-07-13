// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Lobbies.h"

namespace UE::Online {

const TCHAR* LexToString(ELobbyJoinPolicy Policy)
{
	switch (Policy)
	{
	case ELobbyJoinPolicy::PublicAdvertised:	return TEXT("PublicAdvertised");
	case ELobbyJoinPolicy::PublicNotAdvertised:	return TEXT("PublicNotAdvertised");
	default:									checkNoEntry(); // Intentional fallthrough
	case ELobbyJoinPolicy::InvitationOnly:		return TEXT("InvitationOnly");
	}
}

void LexFromString(ELobbyJoinPolicy& OutPolicy, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("PublicAdvertised")) == 0)
	{
		OutPolicy = ELobbyJoinPolicy::PublicAdvertised;
	}
	else if (FCString::Stricmp(InStr, TEXT("PublicNotAdvertised")) == 0)
	{
		OutPolicy = ELobbyJoinPolicy::PublicNotAdvertised;
	}
	else if (FCString::Stricmp(InStr, TEXT("InvitationOnly")) == 0)
	{
		OutPolicy = ELobbyJoinPolicy::InvitationOnly;
	}
	else
	{
		checkNoEntry();
		OutPolicy = ELobbyJoinPolicy::InvitationOnly;
	}
}

const TCHAR* LexToString(ELobbyComparisonOp Comparison)
{
	switch (Comparison)
	{
	case ELobbyComparisonOp::Equals:			return TEXT("Equals");
	case ELobbyComparisonOp::NotEquals:			return TEXT("NotEquals");
	case ELobbyComparisonOp::GreaterThan:		return TEXT("GreaterThan");
	case ELobbyComparisonOp::GreaterThanEquals:	return TEXT("GreaterThanEquals");
	case ELobbyComparisonOp::LessThan:			return TEXT("LessThan");
	case ELobbyComparisonOp::LessThanEquals:	return TEXT("LessThanEquals");
	case ELobbyComparisonOp::Near:				return TEXT("Near");
	case ELobbyComparisonOp::In:				return TEXT("In");
	default:									checkNoEntry(); // Intentional fallthrough
	case ELobbyComparisonOp::NotIn:				return TEXT("NotIn");
	}
}

void LexFromString(ELobbyComparisonOp& OutComparison, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Equals")) == 0)
	{
		OutComparison = ELobbyComparisonOp::Equals;
	}
	else if (FCString::Stricmp(InStr, TEXT("NotEquals")) == 0)
	{
		OutComparison = ELobbyComparisonOp::NotEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("GreaterThan")) == 0)
	{
		OutComparison = ELobbyComparisonOp::GreaterThan;
	}
	else if (FCString::Stricmp(InStr, TEXT("GreaterThanEquals")) == 0)
	{
		OutComparison = ELobbyComparisonOp::GreaterThanEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("LessThan")) == 0)
	{
		OutComparison = ELobbyComparisonOp::LessThan;
	}
	else if (FCString::Stricmp(InStr, TEXT("LessThanEquals")) == 0)
	{
		OutComparison = ELobbyComparisonOp::LessThanEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("Near")) == 0)
	{
		OutComparison = ELobbyComparisonOp::Near;
	}
	else if (FCString::Stricmp(InStr, TEXT("In")) == 0)
	{
		OutComparison = ELobbyComparisonOp::In;
	}
	else
	{
		checkNoEntry();
		OutComparison = ELobbyComparisonOp::In;
	}
}

const TCHAR* LexToString(EUILobbyJoinRequestedSource UILobbyJoinRequestedSource)
{
	switch (UILobbyJoinRequestedSource)
	{
	case EUILobbyJoinRequestedSource::FromInvitation:	return TEXT("FromInvitation");
	default:											checkNoEntry(); // Intentional fallthrough
	case EUILobbyJoinRequestedSource::Unspecified:		return TEXT("Unspecified");
	}
}

void LexFromString(EUILobbyJoinRequestedSource& OutUILobbyJoinRequestedSource, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("FromInvitation")) == 0)
	{
		OutUILobbyJoinRequestedSource = EUILobbyJoinRequestedSource::FromInvitation;
	}
	else if (FCString::Stricmp(InStr, TEXT("Unspecified")) == 0)
	{
		OutUILobbyJoinRequestedSource = EUILobbyJoinRequestedSource::Unspecified;
	}
	else
	{
		checkNoEntry();
		OutUILobbyJoinRequestedSource = EUILobbyJoinRequestedSource::Unspecified;
	}
}

void SortLobbies(const TArray<FFindLobbySearchFilter>& Filters, TArray<TSharedRef<const FLobby>>& Lobbies)
{
	// todo
}

/* UE::Online */ }

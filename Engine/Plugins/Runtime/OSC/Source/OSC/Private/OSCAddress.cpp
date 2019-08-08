// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCAddress.h"

// TODO: Finish implementing full pattern matching instead of just substring parsing
#define USE_FULL_OSC_PATTERN_MATCH 0


FOSCAddress::FOSCAddress()
	: bIsValidPattern(false)
	, bIsValidPath(false)
	, Hash(GetTypeHash(GetFullPath()))
{
}

FOSCAddress::FOSCAddress(const FString& InValue)
	: bIsValidPattern(false)
	, bIsValidPath(false)
{
	bIsValidPath = IsValidPath(InValue, false);
	InValue.ParseIntoArray(Containers, *OSC::PathSeparator, true);

	if (Containers.Num() > 0)
	{
		Method = Containers.Pop();
	}

	Hash = GetTypeHash(InValue);
	bIsValidPattern = IsValidPattern(Containers, Method);
}

// TODO: Pattern should check for validity of search strings.
// ex. '[' doesn't come before closing ']', etc.  This is essentially
// here as a stub.
bool FOSCAddress::IsValidPattern(const TArray<FString>& InContainers, const FString& InMethod)
{
#if USE_FULL_OSC_PATTERN_MATCH
	for (const FString& Container : InContainers)
	{
		for (const TCHAR& Char : Container.GetCharArray())
		{
			check(Char != OSC::PathSeparator[0]);

			if (OSC::PatternChars.Contains(Char))
			{
				return true;
			}
		}
	}
	return false;
#else // USE_FULL_OSC_PATTERN_MATCH
	return true;
#endif // !USE_FULL_OSC_PATTERN_MATCH
}

bool FOSCAddress::IsValidPath(const FString& Path, bool bInvalidateSeparator)
{
	for (const TCHAR& Char : Path.GetCharArray())
	{
		if (!bInvalidateSeparator)
		{
			if (Char == OSC::PathSeparator[0])
			{
				continue;
			}
		}

		if (OSC::InvalidChars.Contains(Char))
		{
			return false;
		}

		if (OSC::PatternChars.Contains(Char))
		{
			return false;
		}
	}

	return true;
}

bool FOSCAddress::Matches(const FOSCAddress& Address) const
{
	if (IsValidPattern())
	{
#if USE_FULL_OSC_PATTERN_MATCH
		// TODO: Implement
		return false;
#else // USE_FULL_OSC_PATTERN_MATCH
		return Address.GetFullPath().Contains(GetFullPath(), ESearchCase::CaseSensitive);
#endif // !USE_FULL_OSC_PATTERN_MATCH
	}
	else
	{
		return IsValidPath() && Address.IsValidPath() && Address.GetFullPath() == GetFullPath();
	}
}

bool FOSCAddress::IsValidPattern() const
{
	return bIsValidPattern;
}

bool FOSCAddress::IsValidPath() const
{
	return bIsValidPath;
}

void FOSCAddress::PushContainer(const FString& Container)
{
	const bool bInvalidateSeparator = true;
	if (!IsValidPath(Container, bInvalidateSeparator))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to push container on OSCAddress. "
			"Cannot contain invalid or reserved OSC pattern identifiers."));
		return;
	}

	Containers.Push(Container);
	Hash = GetTypeHash(GetFullPath());
}

FString FOSCAddress::PopContainer()
{
	if (Containers.Num() > 0)
	{
		FString Popped = Containers.Pop(false);
		Hash = GetTypeHash(GetFullPath());
	}

	return FString();
}

void FOSCAddress::ClearContainers(const FString& Container)
{
	Containers.Reset();
	Hash = GetTypeHash(GetFullPath());
}

const FString& FOSCAddress::GetMethod() const
{
	return Method;
}

void FOSCAddress::SetMethod(const FString& InMethod)
{
	const bool bInvalidateSeparator = true;
	if (!IsValidPath(Method, bInvalidateSeparator))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to set method on OSCAddress. "
			"Cannot contain invalid or reserved OSC pattern identifiers."));
		return;
	}

	Method = InMethod;
	Hash = GetTypeHash(GetFullPath());
}

FString FOSCAddress::GetContainerPath() const
{
	return OSC::PathSeparator + FString::Join(Containers, *OSC::PathSeparator);
}

FString FOSCAddress::GetContainer(int32 Index) const
{
	if (Index >= 0 && Index < Containers.Num())
	{
		return Containers[Index];
	}

	return FString();
}

void FOSCAddress::GetContainers(TArray<FString>& OutContainers) const
{
	OutContainers = Containers;
}

FString FOSCAddress::GetFullPath() const
{
	if (Containers.Num() == 0)
	{
		return OSC::PathSeparator + Method;
	}

	return GetContainerPath() + OSC::PathSeparator + Method;
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCAddress.h"

#include "Audio/AudioAddressPattern.h"


namespace OSC
{
	const FString BundleTag = TEXT("#bundle");
	const FString PathSeparator = TEXT("/");
} // namespace OSC


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
	InValue.ParseIntoArray(Containers, *OSC::PathSeparator, true);
	if (Containers.Num() > 0)
	{
		Method = Containers.Pop();
	}

	CacheAggregates();
}

void FOSCAddress::CacheAggregates()
{
	const bool bInvalidateSeparator = false;
	const FString FullPath = GetFullPath();

	Hash = GetTypeHash(FullPath);
	bIsValidPath = FAudioAddressPattern::IsValidPath(FullPath, bInvalidateSeparator);
	bIsValidPattern = FAudioAddressPattern::IsValidPattern(Containers, Method);
}

bool FOSCAddress::Matches(const FOSCAddress& InAddress) const
{
	if (IsValidPattern() && InAddress.IsValidPath())
	{
		if (Containers.Num() != InAddress.Containers.Num())
		{
			return false;
		}

		if (!FAudioAddressPattern::PartsMatch(Method, InAddress.Method))
		{
			return false;
		}

		for (int32 i = 0; i < Containers.Num(); ++i)
		{
			if (!FAudioAddressPattern::PartsMatch(Containers[i], InAddress.Containers[i]))
			{
				return false;
			}
		}

		return true; 
	}

	return false;
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
	if (Container.Contains(OSC::PathSeparator))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to push container on OSCAddress. "
			"Cannot contain OSC path separator '%s'."), *OSC::PathSeparator);
		return;
	}

	Containers.Push(Container);
	CacheAggregates();
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
	CacheAggregates();
}

const FString& FOSCAddress::GetMethod() const
{
	return Method;
}

void FOSCAddress::SetMethod(const FString& InMethod)
{
	if (InMethod.IsEmpty())
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to set OSCAddress method. "
			"'InMethod' cannot be empty string."));
		return;
	}

	if (InMethod.Contains(OSC::PathSeparator))
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to set OSCAddress method. "
			"Cannot contain OSC path separator '%s'."), *OSC::PathSeparator);
		return;
	}

	Method = InMethod;
	CacheAggregates();
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

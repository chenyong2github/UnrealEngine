// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "OSCLog.h"

#include "OSCAddress.generated.h"


namespace OSC
{
	const FString		BundleTag		= TEXT("#bundle");
	const FString		PathSeparator	= TEXT("/");
	const TArray<TCHAR>	InvalidChars	= { ' ', '#', };
	const TArray<TCHAR>	PatternChars	= { ',', '*', '/', '?', '[', ']', '{', '}' };
} // namespace OSC

USTRUCT(BlueprintType)
struct OSC_API FOSCAddress
{
	GENERATED_USTRUCT_BODY()

private:
	UPROPERTY(Transient)
	TArray<FString> Containers;

	UPROPERTY(Transient)
	FString Method;

	bool bIsValidPattern;
	bool bIsValidPath;
	uint32 Hash;

	static bool IsValidPattern(const TArray<FString>& InContainers, const FString& InMethod);
	static bool IsValidPath(const FString& Path, bool bInvalidateSeparator);

public:
	uint32 GetHash() const;

	bool IsValidPattern() const;
	bool IsValidPath() const;
	bool Matches(const FOSCAddress& Address) const;

	void PushContainer(const FString& Container);

	void ClearContainers(const FString& Container);
	void SetMethod(const FString& InMethod);
	FString PopContainer();

	FString GetContainerPath() const;
	FString GetContainer(int32 Index) const;
	void GetContainers(TArray<FString>& OutContainers) const;
	FString GetFullPath() const;
	const FString& GetMethodName() const;

	FOSCAddress();
	FOSCAddress(const FString& InValue);
};

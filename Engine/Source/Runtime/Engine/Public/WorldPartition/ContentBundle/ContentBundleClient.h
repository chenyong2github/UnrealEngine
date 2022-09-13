// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtr.h"

class UContentBundleDescriptor;

UENUM()
enum class EContentBundleClientState
{
	Unregistered,
	Registered,
	ContentInjectionRequested,
	ContentInjected,
	ContentRemovalRequested,
	
	// Failed state
	RegistrationFailed,
	ContentInjectionFailed,
};

class ENGINE_API FContentBundleClient
{
	friend class UContentBundleEngineSubsystem;

public:
	static TSharedPtr<FContentBundleClient> CreateClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName);

	FContentBundleClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName);
	virtual ~FContentBundleClient() = default;

	const UContentBundleDescriptor* GetDescriptor() const { return ContentBundleDescriptor.Get(); }

	void RequestContentInjection();
	void RequestRemoveContent();
	
	void RequestUnregister();

	EContentBundleClientState GetState() const { return ContentInjectionState; }

	FString const& GetDisplayName() const { return DisplayName; }

private:
	TWeakObjectPtr<const UContentBundleDescriptor> ContentBundleDescriptor;

	FString DisplayName;

	EContentBundleClientState ContentInjectionState;
};
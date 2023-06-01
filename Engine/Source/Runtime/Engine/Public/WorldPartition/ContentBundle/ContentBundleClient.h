// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtr.h"

#include "WorldPartition/ContentBundle/ContentBundleStatus.h"

#include "ContentBundleClient.generated.h"

class UContentBundleDescriptor;
class UWorld;

UENUM()
enum class EContentBundleClientState
{
	Unregistered,
	Registered,
	ContentInjectionRequested,
	ContentRemovalRequested,
	
	// Failed state
	RegistrationFailed,
};

UENUM()
enum class EWorldContentState
{
	NoContent,
	ContentBundleInjected
};

class ENGINE_API FContentBundleClient
{
	friend class UContentBundleEngineSubsystem;
	friend class FContentBundleBase;

public:
	static TSharedPtr<FContentBundleClient> CreateClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName);

	FContentBundleClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName);
	virtual ~FContentBundleClient() = default;

	const UContentBundleDescriptor* GetDescriptor() const { return ContentBundleDescriptor.Get(); }

	void RequestContentInjection();
	void RequestRemoveContent();
	
	void RequestUnregister();

	EContentBundleClientState GetState() const { return State; }

	FString const& GetDisplayName() const { return DisplayName; }

	virtual bool ShouldInjectContent(UWorld* World) const;
	virtual bool ShouldRemoveContent(UWorld* World) const;

protected:
	virtual void DoOnContentRegisteredInWorld(UWorld* InjectedWorld) {};
	virtual void DoOnContentInjectedInWorld(EContentBundleStatus InjectionStatus, UWorld* InjectedWorld) {};
	virtual void DoOnContentRemovedFromWorld(UWorld* InjectedWorld) {};

	virtual void DoOnClientToUnregister() {};

private:
	bool HasContentToRemove() const;

	void OnContentRegisteredInWorld(EContentBundleStatus ContentBundleStatus, UWorld* World);
	void OnContentInjectedInWorld(EContentBundleStatus InjectionStatus, UWorld* InjectedWorld);
	void OnContentRemovedFromWorld(EContentBundleStatus RemovalStatus, UWorld* InjectedWorld);

	void SetState(EContentBundleClientState State);
	void SetWorldContentState(UWorld* World, EWorldContentState State);
	EWorldContentState GetWorldContentState(UWorld* World) const;

	TWeakObjectPtr<const UContentBundleDescriptor> ContentBundleDescriptor;
	
	TMap<TWeakObjectPtr<UWorld>, EWorldContentState> WorldContentStates;

	FString DisplayName;

	EContentBundleClientState State;
};
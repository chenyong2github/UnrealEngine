// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundleClient.h"

#include "WorldPartition/ContentBundle/ContentBundleEngineSubsystem.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "Engine/Engine.h"

TSharedPtr<FContentBundleClient> FContentBundleClient::CreateClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName)
{
	return GEngine->GetEngineSubsystem<UContentBundleEngineSubsystem>()->RegisterContentBundle(InContentBundleDescriptor, InDisplayName);
}

FContentBundleClient::FContentBundleClient(const UContentBundleDescriptor* InContentBundleDescriptor, FString const& InDisplayName)
	:ContentBundleDescriptor(InContentBundleDescriptor),
#if WITH_EDITOR
	DisplayName(InDisplayName),
#endif
	ContentInjectionState(EContentBundleClientState::Unregistered)
{

}

void FContentBundleClient::RequestContentInjection()
{
	GEngine->GetEngineSubsystem<UContentBundleEngineSubsystem>()->RequestContentInjection(*this);
	ContentInjectionState = EContentBundleClientState::ContentInjectionRequested;
}

void FContentBundleClient::RequestRemoveContent()
{
	ContentInjectionState = EContentBundleClientState::ContentRemovalRequested;
	GEngine->GetEngineSubsystem<UContentBundleEngineSubsystem>()->RequestContentRemoval(*this);
}

void FContentBundleClient::RequestUnregister()
{
	ContentInjectionState = EContentBundleClientState::ContentRemovalRequested;
	GEngine->GetEngineSubsystem<UContentBundleEngineSubsystem>()->UnregisterContentBundle(*this);
}
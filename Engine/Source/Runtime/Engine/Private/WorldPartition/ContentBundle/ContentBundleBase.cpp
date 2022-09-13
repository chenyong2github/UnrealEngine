// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundleBase.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "WorldPartition/ContentBundle/ContentBundleClient.h"
#include "WorldPartition/ContentBundle/ContentBundleLog.h"
#include "Engine/World.h"

FContentBundleBase::FContentBundleBase(TSharedPtr<FContentBundleClient>& InClient, UWorld* InWorld)
	: Client(InClient),
	InjectedWorld(InWorld),
	Descriptor(InClient->GetDescriptor()),
	Status(EContentBundleStatus::Unknown)
{

}

FContentBundleBase::~FContentBundleBase()
{
	check(GetStatus() == EContentBundleStatus::Unknown);
}

void FContentBundleBase::Initialize()
{
	check(GetStatus() == EContentBundleStatus::Unknown);

	DoInitialize();

	check(GetStatus() == EContentBundleStatus::Registered);
}

void FContentBundleBase::Uninitialize()
{
	check(GetStatus() != EContentBundleStatus::Unknown);

	if (GetStatus() == EContentBundleStatus::ReadyToInject || GetStatus() == EContentBundleStatus::ContentInjected)
	{
		RemoveContent();
	}

	DoUninitialize();

	check(GetStatus() == EContentBundleStatus::Unknown);
}

void FContentBundleBase::InjectContent()
{
	check(GetStatus() == EContentBundleStatus::Registered);

	DoInjectContent();

	check(GetStatus() == EContentBundleStatus::ReadyToInject || GetStatus() == EContentBundleStatus::ContentInjected || GetStatus() == EContentBundleStatus::FailedToInject);
}

void FContentBundleBase::RemoveContent()
{
	check(GetStatus() == EContentBundleStatus::ReadyToInject || GetStatus() == EContentBundleStatus::ContentInjected || GetStatus() == EContentBundleStatus::FailedToInject);

	if (GetStatus() == EContentBundleStatus::ContentInjected || GetStatus() == EContentBundleStatus::ReadyToInject)
	{
		DoRemoveContent();
	}

	check(GetStatus() == EContentBundleStatus::Registered);
}

const TWeakPtr<FContentBundleClient>& FContentBundleBase::GetClient() const
{
	return Client;
}

const UContentBundleDescriptor* FContentBundleBase::GetDescriptor() const
{
	return Descriptor;
}

UWorld* FContentBundleBase::GetInjectedWorld() const
{
	return InjectedWorld.Get();
}

const FString& FContentBundleBase::GetDisplayName() const
{
	return GetDescriptor()->GetDisplayName();
}

void FContentBundleBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Descriptor);
}

void FContentBundleBase::SetStatus(EContentBundleStatus NewStatus)
{
	check(NewStatus != Status);

	UE_LOG(LogContentBundle, Log, TEXT("[CB: %s] State changing from %s to %s"), *GetDescriptor()->GetDisplayName(), *UEnum::GetDisplayValueAsText(Status).ToString(), *UEnum::GetDisplayValueAsText(NewStatus).ToString());
	Status = NewStatus;
}
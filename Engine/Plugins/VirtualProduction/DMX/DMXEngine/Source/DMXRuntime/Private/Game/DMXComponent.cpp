// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DMXComponent.h"

#include "DMXRuntimeLog.h"
#include "DMXStats.h"
#include "Library/DMXEntityFixturePatch.h"


UDMXComponent::UDMXComponent()
	: bReceiveDMXFromPatch(true)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false;
}

void UDMXComponent::OnRegister()
{
	Super::OnRegister();

	if (bTickInEditor && bReceiveDMXFromPatch)
	{
		SetReceiveDMXFromPatch(true);
	}
}

void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bReceiveDMXFromPatch)
	{
		SetReceiveDMXFromPatch(true);
	}
}

void UDMXComponent::DestroyComponent(bool bPromoteChildren)
{
	Super::DestroyComponent(bPromoteChildren);

	if (IsValid(CachedFixturePatch))
	{
		CachedFixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
	}
}

#if WITH_EDITOR
void UDMXComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXComponent, FixturePatchRef))
	{
		if (IsValid(CachedFixturePatch))
		{
			CachedFixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
		}

		CachedFixturePatch = FixturePatchRef.GetFixturePatch();

		if (bTickInEditor || GIsPlayInEditorWorld)
		{
			UDMXEntityFixturePatch* FixturePatch = GetFixturePatch();
		}
	}
}
#endif // WITH_EDITOR

void UDMXComponent::OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute)
{
	OnFixturePatchReceived.Broadcast(FixturePatch, NormalizedValuePerAttribute);
}

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	FixturePatchRef.SetEntity(InFixturePatch);

	SetReceiveDMXFromPatch(bReceiveDMXFromPatch);
}

void UDMXComponent::SetReceiveDMXFromPatch(bool bReceive)
{
	bReceiveDMXFromPatch = bReceive;

	if (IsValid(CachedFixturePatch))
	{
		CachedFixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
	}

	CachedFixturePatch = GetFixturePatch();
	if (bReceiveDMXFromPatch && IsValid(CachedFixturePatch))
	{
		CachedFixturePatch->OnFixturePatchReceivedDMX.AddDynamic(this, &UDMXComponent::OnFixturePatchReceivedDMX);
	}
}

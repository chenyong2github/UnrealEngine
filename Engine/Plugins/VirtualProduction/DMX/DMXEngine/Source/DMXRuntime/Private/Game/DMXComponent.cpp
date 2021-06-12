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

UDMXEntityFixturePatch* UDMXComponent::GetFixturePatch() const
{
	return FixturePatchRef.GetFixturePatch();
}

void UDMXComponent::SetFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	UDMXEntityFixturePatch* PreviousFixturePatch = FixturePatchRef.GetFixturePatch();

	if (InFixturePatch != PreviousFixturePatch)
	{
		// Remove the old receive binding
		if (IsValid(PreviousFixturePatch))
		{
			PreviousFixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
		}

		FixturePatchRef.SetEntity(InFixturePatch);
		SetupReceiveDMXBinding();
	}
}

void UDMXComponent::SetReceiveDMXFromPatch(bool bReceive)
{
	bReceiveDMXFromPatch = bReceive;

	SetupReceiveDMXBinding();
}

void UDMXComponent::OnFixturePatchReceivedDMX(UDMXEntityFixturePatch* FixturePatch, const FDMXNormalizedAttributeValueMap& NormalizedValuePerAttribute)
{
	OnFixturePatchReceived.Broadcast(FixturePatch, NormalizedValuePerAttribute);
}

void UDMXComponent::SetupReceiveDMXBinding()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	if (IsValid(FixturePatch))
	{
		if (bReceiveDMXFromPatch && !FixturePatch->OnFixturePatchReceivedDMX.Contains(this, GET_FUNCTION_NAME_CHECKED(UDMXComponent, OnFixturePatchReceivedDMX)))
		{
			// Enable receive DMX
			FixturePatch->OnFixturePatchReceivedDMX.AddDynamic(this, &UDMXComponent::OnFixturePatchReceivedDMX);
		}
		else if (!bReceiveDMXFromPatch && FixturePatch->OnFixturePatchReceivedDMX.Contains(this, GET_FUNCTION_NAME_CHECKED(UDMXComponent, OnFixturePatchReceivedDMX)))
		{
			// Disable receive DMX
			FixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
		}
	}
}

void UDMXComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UDMXEntityFixturePatch* FixturePatch = GetFixturePatch())
	{
		FDMXNormalizedAttributeValueMap NormalizeAttributeValues;
		FixturePatch->GetNormalizedAttributesValues(NormalizeAttributeValues);

		if (NormalizeAttributeValues.Map.Num() > 0)
		{
			OnFixturePatchReceived.Broadcast(FixturePatch, NormalizeAttributeValues);
		}
	}

	SetupReceiveDMXBinding();
}

void UDMXComponent::DestroyComponent(bool bPromoteChildren)
{
	Super::DestroyComponent(bPromoteChildren);

	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	if (IsValid(FixturePatch))
	{
		FixturePatch->OnFixturePatchReceivedDMX.RemoveAll(this);
	}
}

#if WITH_EDITOR
void UDMXComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXComponent, FixturePatchRef))
	{
		SetFixturePatch(FixturePatchRef.GetFixturePatch());
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXComponent, bReceiveDMXFromPatch))
	{
		SetupReceiveDMXBinding();
	}
}
#endif // WITH_EDITOR


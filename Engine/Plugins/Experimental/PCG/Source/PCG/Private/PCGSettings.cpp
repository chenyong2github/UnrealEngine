// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSettings.h"
#include "PCGNode.h"

FPCGElementPtr UPCGSettings::GetElement() const
{
	if (!CachedElement)
	{
		CacheLock.Lock();

		if (!CachedElement)
		{
			CachedElement = CreateElement();
		}

		CacheLock.Unlock();
	}

	return CachedElement;
}

UPCGNode* UPCGSettings::CreateNode() const
{
	return NewObject<UPCGNode>();
}

FPCGElementPtr UPCGTrivialSettings::CreateElement() const
{
	return MakeShared<FPCGTrivialElement>();
}

bool FPCGTrivialElement::Execute(FPCGContextPtr Context) const
{
	// Pass-through
	Context->OutputData = Context->InputData;
	return true;
}

#if WITH_EDITOR

void UPCGSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnSettingsChangedDelegate.Broadcast(this);
}

#endif // WITH_EDITOR
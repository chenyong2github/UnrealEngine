// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGContext.h"
#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "GameFramework/Actor.h"

FString FPCGContext::GetTaskName() const
{
	if (Node)
	{
		const FName NodeName = ((Node->NodeTitle != NAME_None) ? Node->NodeTitle : Node->GetFName());

		const UPCGSettings* Settings = GetInputSettings<UPCGSettings>();
		const FName NodeAdditionalName = Settings ? Settings->AdditionalTaskName() : NAME_None;

		if (NodeAdditionalName == NAME_None || NodeAdditionalName == NodeName)
		{
			return NodeName.ToString();
		}
		else
		{
			return FString::Printf(TEXT("%s (%s)"), *NodeName.ToString(), *NodeAdditionalName.ToString());
		}
	}
	else
	{
		return TEXT("Anonymous task");
	}
}

FString FPCGContext::GetComponentName() const
{
	return SourceComponent.IsValid() && SourceComponent->GetOwner() ? SourceComponent->GetOwner()->GetFName().ToString() : TEXT("Non-PCG Component");
}

bool FPCGContext::ShouldStop() const
{
	return FPlatformTime::Seconds() > EndTime;
}

bool FPCGContext::IsOutputConnectedOrInspecting(FName PinLabel) const
{
	const bool bOutputConnected = Node && Node->IsOutputPinConnected(PinLabel);
#if WITH_EDITOR
	return bOutputConnected || (!PCGHelpers::IsRuntimeOrPIE() && SourceComponent.IsValid() && SourceComponent->IsInspecting());
#else
	return bOutputConnected;
#endif // WITH_EDITOR
}

const UPCGSettingsInterface* FPCGContext::GetInputSettingsInterface() const
{
	if (Node)
	{
		return InputData.GetSettingsInterface(Node->GetSettingsInterface());
	}
	else
	{
		return InputData.GetSettingsInterface();
	}
}
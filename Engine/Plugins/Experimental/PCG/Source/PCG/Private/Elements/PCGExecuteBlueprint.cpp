// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGExecuteBlueprint.h"

#if WITH_EDITOR
#include "Engine/World.h"
#endif

UWorld* UPCGBlueprintElement::GetWorld() const
{
#if WITH_EDITOR
	return GWorld;
#else
	return nullptr;
#endif
}

#if WITH_EDITOR
void UPCGBlueprintSettings::PostLoad()
{
	Super::PostLoad();

	if (BlueprintElement_DEPRECATED && !BlueprintElementType)
	{
		BlueprintElementType = BlueprintElement_DEPRECATED;
		BlueprintElement_DEPRECATED = nullptr;
	}

	if (!BlueprintElementInstance)
	{
		RefreshBlueprintElement();
	}
}

void UPCGBlueprintSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!BlueprintElementInstance || BlueprintElementInstance->GetClass() != BlueprintElementType)
	{
		RefreshBlueprintElement();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGBlueprintSettings::SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType)
{
	if (!BlueprintElementInstance || InElementType != BlueprintElementType)
	{
		BlueprintElementType = InElementType;
		RefreshBlueprintElement();
	}
}

void UPCGBlueprintSettings::RefreshBlueprintElement()
{
	if (BlueprintElementType)
	{
		BlueprintElementInstance = NewObject<UPCGBlueprintElement>(this, BlueprintElementType);
	}
	else
	{
		BlueprintElementInstance = nullptr;
	}	
}

FPCGElementPtr UPCGBlueprintSettings::CreateElement() const
{
	return MakeShared<FPCGExecuteBlueprintElement>();
}

bool FPCGExecuteBlueprintElement::Execute(FPCGContextPtr Context) const
{
	const UPCGBlueprintSettings* Settings = Context->GetInputSettings<UPCGBlueprintSettings>();

	if (Settings && Settings->BlueprintElementInstance)
	{
		Settings->BlueprintElementInstance->Execute(Context->InputData, Context->OutputData);
	}
	else
	{
		// Nothing to do but forward data
		Context->OutputData = Context->InputData;
	}
	
	return true;
}
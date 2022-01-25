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
void UPCGBlueprintElement::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnBlueprintChangedDelegate.Broadcast(this);
}
#endif // WITH_EDITOR

void UPCGBlueprintSettings::SetupBlueprintEvent()
{
#if WITH_EDITOR
	if (BlueprintElementType)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintElementType->ClassGeneratedBy))
		{
			Blueprint->OnChanged().AddUObject(this, &UPCGBlueprintSettings::OnBlueprintChanged);
		}
	}
#endif
}

void UPCGBlueprintSettings::TeardownBlueprintEvent()
{
#if WITH_EDITOR
	if (BlueprintElementType)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintElementType->ClassGeneratedBy))
		{
			Blueprint->OnChanged().RemoveAll(this);
		}
	}
#endif
}

void UPCGBlueprintSettings::SetupBlueprintElementEvent()
{
#if WITH_EDITOR
	if (BlueprintElementInstance)
	{
		BlueprintElementInstance->OnBlueprintChangedDelegate.AddUObject(this, &UPCGBlueprintSettings::OnBlueprintElementChanged);
	}
#endif
}

void UPCGBlueprintSettings::TeardownBlueprintElementEvent()
{
#if WITH_EDITOR
	if (BlueprintElementInstance)
	{
		BlueprintElementInstance->OnBlueprintChangedDelegate.RemoveAll(this);
	}
#endif
}

void UPCGBlueprintSettings::PostLoad()
{
	Super::PostLoad();

	if (BlueprintElement_DEPRECATED && !BlueprintElementType)
	{
		BlueprintElementType = BlueprintElement_DEPRECATED;
		BlueprintElement_DEPRECATED = nullptr;
	}

	SetupBlueprintEvent();

	if (!BlueprintElementInstance)
	{
		RefreshBlueprintElement();
	}
	else
	{
		SetupBlueprintElementEvent();
	}
}

void UPCGBlueprintSettings::BeginDestroy()
{
	TeardownBlueprintElementEvent();
	TeardownBlueprintEvent();

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGBlueprintSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!BlueprintElementInstance || BlueprintElementInstance->GetClass() != BlueprintElementType)
	{
		RefreshBlueprintElement();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGBlueprintSettings::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	// When the blueprint changes, the element gets recreated, so we must rewire it here.
	TeardownBlueprintElementEvent();
	SetupBlueprintElementEvent();

	OnSettingsChangedDelegate.Broadcast(this);
}

void UPCGBlueprintSettings::OnBlueprintElementChanged(UPCGBlueprintElement* InElement)
{
	OnSettingsChangedDelegate.Broadcast(this);
}
#endif

void UPCGBlueprintSettings::SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType)
{
	if (!BlueprintElementInstance || InElementType != BlueprintElementType)
	{
		if (InElementType != BlueprintElementType)
		{
			TeardownBlueprintEvent();
			BlueprintElementType = InElementType;
			SetupBlueprintEvent();
		}
		
		RefreshBlueprintElement();
	}
}

void UPCGBlueprintSettings::RefreshBlueprintElement()
{
	TeardownBlueprintElementEvent();

	if (BlueprintElementType)
	{
		BlueprintElementInstance = NewObject<UPCGBlueprintElement>(this, BlueprintElementType);
		SetupBlueprintElementEvent();
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
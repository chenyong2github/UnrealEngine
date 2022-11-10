// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputDeviceProperties.h"
#include "UnrealEngine.h"	// For GEngine
#include "Engine/Engine.h"	// For FWorldContext
#include "Application/SlateApplicationBase.h"
#include "Framework/Application/IInputProcessor.h"
#include "GenericPlatform/GenericPlatformApplicationMisc.h"	// For FInputDeviceScope
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Framework/Application/SlateApplication.h"			// For RegisterInputPreProcessor

DEFINE_LOG_CATEGORY(LogInputDeviceProperties);

////////////////////////////////////////////////////////
// FInputDeviceSubsystemProcessor

/** An input processor for detecting changes to input devices based on the current FInputDeviceScope stack */
class FInputDeviceSubsystemProcessor : public IInputProcessor
{
	friend class UInputDeviceSubsystem;
	
	void UpdateLatestDevice(const FInputDeviceId InDeviceId)
	{
		if (UInputDeviceSubsystem* SubSystem = UInputDeviceSubsystem::Get())
		{
			if (const FInputDeviceScope* Scope = FInputDeviceScope::GetCurrent())
			{
				SubSystem->SetMostRecentlyUsedHardwareDevice(InDeviceId, { Scope->InputDeviceName, Scope->HardwareDeviceIdentifier });
			}	
		}
	}
	
public:

	// Required by IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override { }
	
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& InEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& InEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}

	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& InEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}
	
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InEvent, const FPointerEvent* InGestureEvent) override
	{
		UpdateLatestDevice(InEvent.GetInputDeviceId());
		return false;
	}
};

////////////////////////////////////////////////////////
// UInputDeviceSubsystem

FSetDevicePropertyParams::FSetDevicePropertyParams()
	: UserId(FSlateApplicationBase::SlateAppPrimaryPlatformUser)
{

}

UInputDeviceSubsystem* UInputDeviceSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UInputDeviceSubsystem>() : nullptr;
}

void UInputDeviceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// We have to have a valid slate app to run this subsystem
	check(FSlateApplication::IsInitialized());
	
	InputPreprocessor = MakeShared<FInputDeviceSubsystemProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(InputPreprocessor, 0);
}

void UInputDeviceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputPreprocessor);
	}
	InputPreprocessor.Reset();
}

bool UInputDeviceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// No slate app means we can't process any input
	if (!FSlateApplication::IsInitialized() ||
		// Commandlets and servers have no use for this subsystem
		IsRunningCommandlet() ||
		IsRunningDedicatedServer())
	{
		return false;
	}
	
	return Super::ShouldCreateSubsystem(Outer);
}

UWorld* UInputDeviceSubsystem::GetTickableGameObjectWorld() const
{
	// Use the default world by default...
	UWorld* World = GetWorld();

	// ...but if we don't have one (i.e. we are in the editor and not PIE'ing)
	// then we need to get the editor world. This will let us preview
	// device properties without needing to actually PIE every time
	if (!World && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* ThisWorld = Context.World();
			if (!ThisWorld)
			{
				continue;
			}
			// Prefer new PIE window worlds
			else if (Context.WorldType == EWorldType::PIE)
			{
				World = ThisWorld;
				break;
			}
			// Fallback to the editor world, which is still valid for previewing device properties
			else if (Context.WorldType == EWorldType::Editor)
			{
				World = ThisWorld;
			}
		}
	}

	return World;
}

ETickableTickType UInputDeviceSubsystem::GetTickableTickType() const
{
	return ETickableTickType::Conditional;
}

bool UInputDeviceSubsystem::IsAllowedToTick() const
{
	// Only tick when there are active device properties
	return !ActiveProperties.IsEmpty();
}

bool UInputDeviceSubsystem::IsTickableInEditor() const
{
	// We want to tick in editor to allow previewing of device properties
	return true;
}

TStatId UInputDeviceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UInputDeviceSubsystem, STATGROUP_Tickables);
}

void UInputDeviceSubsystem::Tick(float DeltaTime)
{
	for (int32 Index = ActiveProperties.Num() - 1; Index >= 0; --Index)
	{
		if (!ActiveProperties[Index].Property)
		{
			// Something has gone wrong if we get here... maybe the Property has been GC'd ?
			// This is really just an emergency handling case
			ensure(false);
			continue;
		}

		// Increase the evaluated time of this property
		ActiveProperties[Index].EvaluatedDuration += DeltaTime;

		// If the property has run past it's duration, reset it and remove it from our active properties
		if (ActiveProperties[Index].EvaluatedDuration > ActiveProperties[Index].Property->GetDuration())
		{
			// Evaluation complete, mark this one for removal
			if (ActiveProperties[Index].bResetUponCompletion)
			{
				ActiveProperties[Index].Property->ResetDeviceProperty(ActiveProperties[Index].PlatformUser);
			}			
			ActiveProperties.RemoveAtSwap(Index);
		}
		// Otherwise, we can evaluate and apply it as normal
		else
		{
			ActiveProperties[Index].Property->EvaluateDeviceProperty(ActiveProperties[Index].PlatformUser, DeltaTime, ActiveProperties[Index].EvaluatedDuration);
			ActiveProperties[Index].Property->ApplyDeviceProperty(ActiveProperties[Index].PlatformUser);
		}
	}
}

void UInputDeviceSubsystem::SetDeviceProperty(const FSetDevicePropertyParams& Params)
{
	if (!Params.DevicePropertyClass)
	{
		UE_LOG(LogInputDeviceProperties, Error, TEXT("Invalid DevicePropertyClass passed into SetDeviceProperty! Nothing will happen."));
		return;
	}
	
	FActiveDeviceProperty ActiveProp = {};

	// Spawn an instance of this device property
	ActiveProp.Property = NewObject<UInputDeviceProperty>(/* Outer = */ this, /* Class */ Params.DevicePropertyClass);
	ensure(ActiveProp.Property);

	ActiveProp.PlatformUser = Params.UserId;
	ActiveProp.bResetUponCompletion = Params.bResetUponCompletion;
	
	ActiveProperties.Emplace(ActiveProp);
}

int32 UInputDeviceSubsystem::RemoveDeviceProperty(const FPlatformUserId UserId, TSubclassOf<UInputDeviceProperty> DevicePropertyClass)
{
	int32 NumRemoved = 0;

	if (DevicePropertyClass)
	{
		// Remove all active properties that are of the same class type
		for (int32 Index = ActiveProperties.Num() - 1; Index >= 0; --Index)
		{
			if (ActiveProperties[Index].PlatformUser == UserId && ActiveProperties[Index].Property && ActiveProperties[Index].Property->GetClass() == DevicePropertyClass)
			{
				ActiveProperties[Index].Property->ResetDeviceProperty(ActiveProperties[Index].PlatformUser);
				ActiveProperties.RemoveAtSwap(Index);
				++NumRemoved;
			}
		}
	}	
	else
	{
		UE_LOG(LogInputDeviceProperties, Error, TEXT("Invalid DevicePropertyClass passed into RemoveDeviceProperty! Nothing will happen."));
	}

	return NumRemoved;
}

FHardwareDeviceIdentifier UInputDeviceSubsystem::GetMostRecentlyUsedHardwareDevice(const FPlatformUserId InUserId) const
{
	if (const FHardwareDeviceIdentifier* FoundDevice = LatestInputDeviceIdentifiers.Find(InUserId))
	{
		return *FoundDevice;
	}
	
	UE_LOG(LogInputDeviceProperties, Warning, TEXT("Could not determine the hardware device last used by platform user %d"), InUserId.GetInternalId());
	return FHardwareDeviceIdentifier::Invalid;
}

void UInputDeviceSubsystem::SetMostRecentlyUsedHardwareDevice(const FInputDeviceId InDeviceId, const FHardwareDeviceIdentifier& InHardwareId)
{
	FPlatformUserId OwningUserId = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(InDeviceId); 

	LatestInputDeviceIdentifiers.Add(OwningUserId, InHardwareId);

	if (OnInputHardwareDeviceChanged.IsBound())
	{
		OnInputHardwareDeviceChanged.Broadcast(OwningUserId);
	}

	if (OnInputHardwareDeviceChangedNative.IsBound())
	{
		OnInputHardwareDeviceChangedNative.Broadcast(OwningUserId);
	}
}
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
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"	// For FApp::GetDeltaTime()

#include UE_INLINE_GENERATED_CPP_BY_NAME(InputDeviceSubsystem)

#if WITH_EDITOR
#include "Editor.h"		// For PIE delegates
#endif

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
				// TODO: Refactor FInputDeviceScope to use FName's instead of a FString for HardwareDeviceIdentifier
				SubSystem->SetMostRecentlyUsedHardwareDevice(InDeviceId, { Scope->InputDeviceName, FName(*Scope->HardwareDeviceIdentifier) });
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
// FInputDevicePropertyHandle

FInputDevicePropertyHandle FInputDevicePropertyHandle::InvalidHandle(0);

FInputDevicePropertyHandle::FInputDevicePropertyHandle()
	: InternalId(0)
{

}

FInputDevicePropertyHandle::FInputDevicePropertyHandle(uint32 InInternalID)
	: InternalId(InInternalID)
{

}

bool FInputDevicePropertyHandle::operator==(const FInputDevicePropertyHandle& Other) const
{
	return InternalId == Other.InternalId;
}

bool FInputDevicePropertyHandle::operator!=(const FInputDevicePropertyHandle& Other) const
{
	return InternalId != Other.InternalId;
}

uint32 GetTypeHash(const FInputDevicePropertyHandle& InHandle)
{
	return GetTypeHash(InHandle.InternalId);
}

FString FInputDevicePropertyHandle::ToString() const
{
	return IsValid() ? FString::FromInt(InternalId) : TEXT("Invalid");
}

bool FInputDevicePropertyHandle::IsValid() const
{
	return InternalId != FInputDevicePropertyHandle::InvalidHandle.InternalId;
}

FInputDevicePropertyHandle FInputDevicePropertyHandle::AcquireValidHandle()
{
	// 0 is the "Invalid" index for these handles. Start them at 1
	static uint32 GHandleIndex = 1;

	return FInputDevicePropertyHandle(++GHandleIndex);
}

////////////////////////////////////////////////////////
// FSetDevicePropertyParams

FSetDevicePropertyParams::FSetDevicePropertyParams()
	: UserId(FSlateApplicationBase::SlateAppPrimaryPlatformUser)
{

}

FSetDevicePropertyParams::FSetDevicePropertyParams(TSubclassOf<UInputDeviceProperty> InPropertyClass, const FPlatformUserId InUserId, const bool bInRemoveAfterEvaluationTime /* = true */)
	: DevicePropertyClass(InPropertyClass)
	, UserId(InUserId)
	, bRemoveAfterEvaluationTime(bInRemoveAfterEvaluationTime)
{
	
}

////////////////////////////////////////////////////////
// UInputDeviceSubsystem

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

#if WITH_EDITOR
	FEditorDelegates::PreBeginPIE.AddUObject(this, &UInputDeviceSubsystem::OnPrePIEStarted);
	FEditorDelegates::PausePIE.AddUObject(this, &UInputDeviceSubsystem::OnPIEPaused);
	FEditorDelegates::ResumePIE.AddUObject(this, &UInputDeviceSubsystem::OnPIEResumed);
	FEditorDelegates::EndPIE.AddUObject(this, &UInputDeviceSubsystem::OnPIEStopped);
#endif	// WITH_EDITOR
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
#if WITH_EDITOR
	// If we are PIE'ing, then check if PIE is paused
	if (GEditor && (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld))
	{
		return bIsPIEPlaying && !ActiveProperties.IsEmpty();
	}	
#endif

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

void UInputDeviceSubsystem::Tick(float InDeltaTime)
{
	// If a property doesn't want to be affected by time dilation then we can use this instead
	const double NonDialatedDeltaTime = FApp::GetDeltaTime();

	const UWorld* World = GetTickableGameObjectWorld();
	const bool bIsGamePaused = World ? World->IsPaused() : false;

	for (int32 Index = ActiveProperties.Num() - 1; Index >= 0; --Index)
	{
		if (!ActiveProperties[Index].Property)
		{
			// Something has gone wrong if we get here... maybe the Property has been GC'd ?
			// This is really just an emergency handling case
			ensure(false);
			continue;
		}

		// If the game is paused, only play effects that have explicitly specified that they should be 
		// played while paused.
		if (bIsGamePaused && !ActiveProperties[Index].bPlayWhilePaused)
		{
			continue;
		}

		const double DeltaTime = ActiveProperties[Index].bIgnoreTimeDilation ? NonDialatedDeltaTime : static_cast<double>(InDeltaTime);
		
		// Increase the evaluated time of this property
		ActiveProperties[Index].EvaluatedDuration += DeltaTime;

		// If the property has run past it's duration, reset it and remove it from our active properties
		// Only do this if it is marked as 'bRemoveAfterEvaluationTime' so that you can keep device properties set
		// without having to worry about duration. 
		if (ActiveProperties[Index].bRemoveAfterEvaluationTime && ActiveProperties[Index].EvaluatedDuration > ActiveProperties[Index].Property->GetDuration())
		{
			ActiveProperties[Index].Property->ResetDeviceProperty(ActiveProperties[Index].PlatformUser);
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

APlayerController* UInputDeviceSubsystem::GetPlayerControllerFromPlatformUser(const FPlatformUserId UserId)
{
	if (UserId.IsValid())
	{
		if (const UInputDeviceSubsystem* System = UInputDeviceSubsystem::Get())
		{
			if (const UWorld* World = System->GetTickableGameObjectWorld())
			{
				for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
				{
					APlayerController* PlayerController = Iterator->Get();
					const FPlatformUserId PlayerControllerUserID = PlayerController ? PlayerController->GetPlatformUserId() : PLATFORMUSERID_NONE;
					if (PlayerControllerUserID.IsValid() && PlayerControllerUserID == UserId)
					{
						return PlayerController;
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogInputDeviceProperties, Warning, TEXT("Attempting to find the player controller for an invalid Platform"));
	}

	return nullptr;
}

APlayerController* UInputDeviceSubsystem::GetPlayerControllerFromInputDevice(const FInputDeviceId DeviceId)
{
	return GetPlayerControllerFromPlatformUser(IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceId));
}

FInputDevicePropertyHandle UInputDeviceSubsystem::SetDeviceProperty(const FSetDevicePropertyParams& Params)
{
	if (!Params.DevicePropertyClass)
	{
		UE_LOG(LogInputDeviceProperties, Error, TEXT("Invalid DevicePropertyClass passed into SetDeviceProperty! Nothing will happen."));
		return FInputDevicePropertyHandle::InvalidHandle;
	}
		
	FInputDevicePropertyHandle OutHandle = FInputDevicePropertyHandle::AcquireValidHandle();

	if (ensureAlwaysMsgf(OutHandle.IsValid(), TEXT("Unable to acquire a valid input device property handle!")))
	{
		FActiveDeviceProperty ActiveProp = {};

		// Spawn an instance of this device property		
		ActiveProp.Property = NewObject<UInputDeviceProperty>(/* Outer = */ this, /* Class */ Params.DevicePropertyClass);
		ensure(ActiveProp.Property);

		ActiveProp.PlatformUser = Params.UserId;
		ActiveProp.PropertyHandle = OutHandle;
		ActiveProp.bRemoveAfterEvaluationTime = Params.bRemoveAfterEvaluationTime;	
		ActiveProp.bIgnoreTimeDilation = Params.bIgnoreTimeDilation;
		ActiveProp.bPlayWhilePaused = Params.bPlayWhilePaused;

		ActiveProperties.Add(ActiveProp);
	}

	return OutHandle;
}

TObjectPtr<UInputDeviceProperty> UInputDeviceSubsystem::GetActiveDeviceProperty(const FInputDevicePropertyHandle Handle)
{
	for (const FActiveDeviceProperty& ActiveProp : ActiveProperties)
	{
		if (ActiveProp.PropertyHandle == Handle)
		{
			return ActiveProp.Property;
		}
	}
	return nullptr;
}

int32 UInputDeviceSubsystem::RemoveDevicePropertiesOfClass(const FPlatformUserId UserId, TSubclassOf<UInputDeviceProperty> DevicePropertyClass)
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

int32 UInputDeviceSubsystem::RemoveDevicePropertyByHandle(const FInputDevicePropertyHandle HandleToRemove, const bool bResetOnRemoval /*= true*/)
{
	return RemoveDevicePropertyHandles({ HandleToRemove }, bResetOnRemoval);
}

int32 UInputDeviceSubsystem::RemoveDevicePropertyHandles(const TSet<FInputDevicePropertyHandle>& HandlesToRemove, const bool bResetOnRemoval /*= true*/)
{
	int32 NumRemoved = 0;

	for (int32 Index = ActiveProperties.Num() - 1; Index >= 0; --Index)
	{
		if (HandlesToRemove.Contains(ActiveProperties[Index].PropertyHandle))
		{
			if (bResetOnRemoval)
			{
				ActiveProperties[Index].Property->ResetDeviceProperty(ActiveProperties[Index].PlatformUser);
			}			
			ActiveProperties.RemoveAtSwap(Index);
			++NumRemoved;
			break;
		}
	}

	if (NumRemoved <= 0)
	{
		UE_LOG(LogInputDeviceProperties, Warning, TEXT("Unable to remove any device property handles!"));
	}

	return NumRemoved;
}

bool UInputDeviceSubsystem::IsDevicePropertyHandleValid(const FInputDevicePropertyHandle& InHandle)
{
	return InHandle.IsValid();
}

void UInputDeviceSubsystem::RemoveAllDeviceProperties()
{
	ActiveProperties.Empty();
}

FHardwareDeviceIdentifier UInputDeviceSubsystem::GetMostRecentlyUsedHardwareDevice(const FPlatformUserId InUserId) const
{
	if (const FHardwareDeviceIdentifier* FoundDevice = LatestUserDeviceIdentifiers.Find(InUserId))
	{
		return *FoundDevice;
	}
		
	return FHardwareDeviceIdentifier::Invalid;
}

FHardwareDeviceIdentifier UInputDeviceSubsystem::GetInputDeviceHardwareIdentifier(const FInputDeviceId InputDevice) const
{
	if (const FHardwareDeviceIdentifier* FoundDevice = LatestInputDeviceIdentifiers.Find(InputDevice))
	{
		return *FoundDevice;
	}

	return FHardwareDeviceIdentifier::Invalid;
}

void UInputDeviceSubsystem::SetMostRecentlyUsedHardwareDevice(const FInputDeviceId InDeviceId, const FHardwareDeviceIdentifier& InHardwareId)
{
	FPlatformUserId OwningUserId = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(InDeviceId);

	// Keep track of each input device's latest hardware id
	LatestInputDeviceIdentifiers.Add(InDeviceId, InHardwareId);

	// Keep a map to platform users so that we can easily get their most recent hardware
	LatestUserDeviceIdentifiers.Add(OwningUserId, InHardwareId);

	if (OnInputHardwareDeviceChanged.IsBound())
	{
		OnInputHardwareDeviceChanged.Broadcast(OwningUserId, InDeviceId);
	}
}

#if WITH_EDITOR
void UInputDeviceSubsystem::OnPrePIEStarted(bool bSimulating)
{
	// Remove all active properties, just in case someone was previewing something in the editor that are still going
	RemoveAllDeviceProperties();
	bIsPIEPlaying = true;
}

void UInputDeviceSubsystem::OnPIEPaused(bool bSimulating)
{
	bIsPIEPlaying = false;
}

void UInputDeviceSubsystem::OnPIEResumed(bool bSimulating)
{
	bIsPIEPlaying = true;
}

void UInputDeviceSubsystem::OnPIEStopped(bool bSimulating)
{
	// Remove all active properties when PIE stops
	RemoveAllDeviceProperties();
	bIsPIEPlaying = false;
}
#endif

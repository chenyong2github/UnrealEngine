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
#if WITH_EDITOR
		// If we're stopped at a breakpoint we need for this input preprocessor to just ignore all incoming input
		// because we're now doing stuff outside the game loop in the editor and it needs to not block all that.
		// This can happen if you suspend input while spawning a dialog and then hit another breakpoint and then
		// try and use the editor, you can suddenly be unable to do anything.
		if (GIntraFrameDebuggingGameThread)
		{
			return;
		}
#endif
		
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
// FActiveDeviceProperty

/** Active properties can just use the hash of their Property Device handle for a fast and unique lookup */
uint32 GetTypeHash(const FActiveDeviceProperty& InProp)
{
	return InProp.PropertyHandle.GetTypeHash();
}

// The property handles are the only things that matter when comparing, they will always be unique.
bool FActiveDeviceProperty::operator==(const FActiveDeviceProperty& Other) const
{
	return PropertyHandle == Other.PropertyHandle;
}

bool FActiveDeviceProperty::operator!=(const FActiveDeviceProperty& Other) const
{
	return PropertyHandle != Other.PropertyHandle;
}

bool operator==(const FActiveDeviceProperty& ActiveProp, const FInputDevicePropertyHandle& Handle)
{
	return ActiveProp.PropertyHandle == Handle;
}

bool operator!=(const FActiveDeviceProperty& ActiveProp, const FInputDevicePropertyHandle& Handle)
{
	return ActiveProp.PropertyHandle != Handle;
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
	// Only tick when there are active device properties or ones we want to remove
	const bool bWantsTick = !ActiveProperties.IsEmpty() || !PropertiesPendingRemoval.IsEmpty();
#if WITH_EDITOR
	// If we are PIE'ing, then check if PIE is paused
	if (GEditor && (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld))
	{
		return bIsPIEPlaying && bWantsTick;
	}	
#endif
	
	return bWantsTick;
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

	for (auto It = ActiveProperties.CreateIterator(); It; ++It)
	{
		FActiveDeviceProperty& ActiveProp = *It;
		if (!ActiveProp.Property)
		{
			// Something has gone wrong if we get here... maybe the Property has been GC'd ?
			// This is really just an emergency handling case
			ensure(false);
			continue;
		}
		
		if (PropertiesPendingRemoval.Contains(ActiveProp.PropertyHandle))
		{
			ActiveProp.Property->ResetDeviceProperty(ActiveProp.PlatformUser);
			It.RemoveCurrent();
			PropertiesPendingRemoval.Remove(ActiveProp.PropertyHandle);
			continue;
		}

		// If the game is paused, only play effects that have explicitly specified that they should be
		// played while paused.
		if (bIsGamePaused && !ActiveProp.bPlayWhilePaused)
		{
			continue;
		}

		const double DeltaTime = ActiveProp.bIgnoreTimeDilation ? NonDialatedDeltaTime : static_cast<double>(InDeltaTime);

		// Increase the evaluated time of this property
		ActiveProp.EvaluatedDuration += DeltaTime;

		// If the property has run past it's duration, reset it and remove it from our active properties
		// Only do this if it is marked as 'bRemoveAfterEvaluationTime' so that you can keep device properties set
		// without having to worry about duration. 
		if (ActiveProp.bRemoveAfterEvaluationTime && ActiveProp.EvaluatedDuration > ActiveProp.Property->GetDuration())
		{
			ActiveProp.Property->ResetDeviceProperty(ActiveProp.PlatformUser);
			It.RemoveCurrent();
			continue;
		}
		// Otherwise, we can evaluate and apply it as normal
		else
		{
			ActiveProp.Property->EvaluateDeviceProperty(ActiveProp.PlatformUser, DeltaTime, ActiveProp.EvaluatedDuration);
			ActiveProp.Property->ApplyDeviceProperty(ActiveProp.PlatformUser);
		}		 
	}

	// After we are done ticking, there should be no properties pending removal
	ensure(PropertiesPendingRemoval.IsEmpty());
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

UInputDeviceProperty* UInputDeviceSubsystem::GetActiveDeviceProperty(const FInputDevicePropertyHandle Handle) const
{
	// Don't include any properties that are pending removal
	if (!PropertiesPendingRemoval.Contains(Handle))
	{
		// We can find the active property based on the handle's hash because FActiveDeviceProperty::GetTypeHash
		// just returns its FInputDevicePropertyHandle's GetTypeHash
		if (const FActiveDeviceProperty* ExistingProp = ActiveProperties.FindByHash(Handle.GetTypeHash(), Handle))
		{
			return ExistingProp->Property;
		}
	}
	
	return nullptr;
}

bool UInputDeviceSubsystem::IsPropertyActive(const FInputDevicePropertyHandle Handle) const
{
	return ActiveProperties.ContainsByHash(Handle.GetTypeHash(), Handle) && !PropertiesPendingRemoval.Contains(Handle);
}

void UInputDeviceSubsystem::RemoveDevicePropertyByHandle(const FInputDevicePropertyHandle HandleToRemove)
{
	PropertiesPendingRemoval.Add(HandleToRemove);
}

void UInputDeviceSubsystem::RemoveDevicePropertyHandles(const TSet<FInputDevicePropertyHandle>& HandlesToRemove)
{
	if (!HandlesToRemove.IsEmpty())
	{
		PropertiesPendingRemoval.Append(HandlesToRemove);	
	}
	else
	{
		UE_LOG(LogInputDeviceProperties, Warning, TEXT("Provided an empty set of handles to remove. Nothing will happen."));
	}
}

bool UInputDeviceSubsystem::IsDevicePropertyHandleValid(const FInputDevicePropertyHandle& InHandle)
{
	return InHandle.IsValid();
}

void UInputDeviceSubsystem::RemoveAllDeviceProperties()
{
	for (FActiveDeviceProperty& ActiveProperty : ActiveProperties)
	{
		if (ActiveProperty.Property)
		{
			ActiveProperty.Property->ResetDeviceProperty(ActiveProperty.PlatformUser);
		}		
	}
	
	ActiveProperties.Empty();
	PropertiesPendingRemoval.Empty();
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
	// If the hardware hasn't changed, then just ignore it because we don't need to update anything.
	if (const FHardwareDeviceIdentifier* ExistingDevice = LatestInputDeviceIdentifiers.Find(InDeviceId))
	{
		if (InHardwareId == *ExistingDevice)
		{
			return;
		}
	}

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

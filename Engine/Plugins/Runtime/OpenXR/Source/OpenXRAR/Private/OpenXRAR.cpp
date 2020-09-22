// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRAR.h"
#include "OpenXRHMD.h"
#include "IOpenXRExtensionPlugin.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#endif

#include <openxr/openxr.h>

#define LOCTEXT_NAMESPACE "OpenXRAR"



FOpenXRARSystem::FOpenXRARSystem()
{
}

FOpenXRARSystem::~FOpenXRARSystem()
{
	OnStopARSession();
}

void FOpenXRARSystem::SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem)
{
	static FName SystemName(TEXT("OpenXR"));
	if (InTrackingSystem->GetSystemName() == SystemName)
	{
		TrackingSystem = static_cast<FOpenXRHMD*>(InTrackingSystem.Get());
	}

	check(TrackingSystem != nullptr);

	for (auto Plugin : TrackingSystem->GetExtensionPlugins())
	{
		CustomAnchorSupport = Plugin->GetCustomAnchorSupport();
		if (CustomAnchorSupport != nullptr)
		{
			break;
		}
	}

}


void FOpenXRARSystem::OnStartARSession(UARSessionConfig* InSessionConfig) 
{ 
	SessionConfig = InSessionConfig; 

	SessionStatus.Status = EARSessionStatus::Running;
}

void FOpenXRARSystem::OnStopARSession() 
{
	SessionStatus.Status = EARSessionStatus::NotStarted;

	SessionConfig = nullptr;
}


/** @return the info about whether the session is running normally or encountered some kind of error. */
FARSessionStatus FOpenXRARSystem::OnGetARSessionStatus() const
{
	return SessionStatus;
}

/** Returns true/false based on whether AR features are available */
bool FOpenXRARSystem::IsARAvailable() const
{
	return true;
}


bool FOpenXRARSystem::OnIsTrackingTypeSupported(EARSessionType SessionType) const
{
	switch (SessionType)
	{
	case EARSessionType::World:
		return true;

	case EARSessionType::Orientation:
	case EARSessionType::Face:
	case EARSessionType::Image:
	case EARSessionType::ObjectScanning:
	default:
		break;
	}
	return false;
}

//=========== Pins =============================================

/** @return a TArray of all the pins that attach components to TrackedGeometries */
TArray<UARPin*> FOpenXRARSystem::OnGetAllPins() const
{
	return Pins;
}

/**
 * Given a scene component find the ARPin which it is pinned by, if any.
 */
UARPin* FOpenXRARSystem::FindARPinByComponent(const USceneComponent* Component) const
{
	for (UARPin* Pin : Pins)
	{
		if (Pin->GetPinnedComponent() == Component)
		{
			return Pin;
		}
	}

	return nullptr;
}

/**
 * Pin an Unreal Component to a location in the world.
 * Optionally, associate with a TrackedGeometry to receive transform updates that effectively attach the component to the geometry.
 *
 * @return the UARPin object that is pinning the component to the world and (optionally) a TrackedGeometry
 */
UARPin* FOpenXRARSystem::OnPinComponent(USceneComponent* ComponentToPin, const FTransform& PinToWorldTransform, UARTrackedGeometry* TrackedGeometry /*= nullptr*/, const FName DebugName/* = NAME_None*/)
{
	if (!ensureMsgf(ComponentToPin != nullptr, TEXT("Cannot pin component.")))
	{
		return nullptr;
	}

	if (UARPin* FindResult = FindARPinByComponent(ComponentToPin))
	{
		UE_LOG(LogOpenXRAR, Warning, TEXT("Component %s is already pinned. Unpinning it first."), *ComponentToPin->GetReadableName());
		OnRemovePin(FindResult);
	}
	TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

	// PinToWorld * AlignedTrackingToWorld(-1) * TrackingToAlignedTracking(-1) = PinToWorld * WorldToAlignedTracking * AlignedTrackingToTracking
	// The Worlds and AlignedTracking cancel out, and we get PinToTracking
	// But we must translate this logic into Unreal's transform API
	const FTransform& TrackingToAlignedTracking = ARSupportInterface->GetAlignmentTransform();
	const FTransform PinToTrackingTransform = PinToWorldTransform.GetRelativeTransform(TrackingSystem->GetTrackingToWorldTransform()).GetRelativeTransform(TrackingToAlignedTracking);
	

	UARPin* NewPin = NewObject<UARPin>();
	NewPin->InitARPin(ARSupportInterface.ToSharedRef(), ComponentToPin, PinToTrackingTransform, TrackedGeometry, DebugName);

	// If the user did not provide a TrackedGeometry, create an anchor for this pin.
	if (TrackedGeometry == nullptr)
	{
		if (CustomAnchorSupport != nullptr)
		{
			XrSession Session = TrackingSystem->GetSession();
			XrTime DisplayTime = TrackingSystem->GetDisplayTime();
			XrSpace TrackingSpace = TrackingSystem->GetTrackingSpace();
			float WorldToMetersScale = TrackingSystem->GetWorldToMetersScale();
			if (!CustomAnchorSupport->OnPinComponent(NewPin, Session, TrackingSpace, DisplayTime, WorldToMetersScale))
			{
				UE_LOG(LogOpenXRAR, Error, TEXT("Component %s failed to pin."), *ComponentToPin->GetReadableName());
			}
		}
	}

	Pins.Add(NewPin);
	return NewPin;
}


/**
 * Given a pin, remove it and stop updating the associated component based on the tracked geometry.
 * The component in question will continue to track with the world, but will not get updates specific to a TrackedGeometry.
 */
void FOpenXRARSystem::OnRemovePin(UARPin* PinToRemove) 
{
	if (PinToRemove == nullptr)
	{
		return;
	}

	Pins.RemoveSingleSwap(PinToRemove);

	if (CustomAnchorSupport != nullptr)
	{
		CustomAnchorSupport->OnRemovePin(PinToRemove);
	}
}

void FOpenXRARSystem::UpdateAnchors()
{
	if (SessionStatus.Status != EARSessionStatus::Running) { return; }

	if (CustomAnchorSupport != nullptr)
	{
		XrSession Session = TrackingSystem->GetSession();
		XrTime DisplayTime = TrackingSystem->GetDisplayTime();
		XrSpace TrackingSpace = TrackingSystem->GetTrackingSpace();
		float WorldToMetersScale = TrackingSystem->GetWorldToMetersScale();
		for (UARPin* Pin : Pins)
		{
			CustomAnchorSupport->OnUpdatePin(Pin, Session, TrackingSpace, DisplayTime, WorldToMetersScale);
		}
	}
}

bool FOpenXRARSystem::IsLocalPinSaveSupported() const
{
	return CustomAnchorSupport != nullptr && CustomAnchorSupport->IsLocalPinSaveSupported();
}

bool FOpenXRARSystem::ArePinsReadyToLoad()
{
	if (!IsLocalPinSaveSupported()) { return false; }

	return CustomAnchorSupport->ArePinsReadyToLoad();
}

void FOpenXRARSystem::LoadARPins(TMap<FName, UARPin*>& LoadedPins)
{
	if (!IsLocalPinSaveSupported()) { return; }

	CustomAnchorSupport->LoadARPins(TrackingSystem->GetSession(),
		[&, this](FName Name)
		{
			check(IsInGameThread());
			for (auto Pin: Pins)
			{
				if (Pin->GetFName().ToString().ToLower() == Name.ToString().ToLower())
				{
					LoadedPins.Add(Name, Pin);
					return (UARPin*)nullptr;
				}
			}
			TSharedPtr<FARSupportInterface, ESPMode::ThreadSafe> ARSupportInterface = TrackingSystem->GetARCompositionComponent();

			UARPin* NewPin = NewObject<UARPin>();
			NewPin->InitARPin(ARSupportInterface.ToSharedRef(), nullptr, FTransform::Identity, nullptr, Name);

			Pins.Add(NewPin);
			LoadedPins.Add(Name, NewPin);
			return NewPin;
		});
}

bool FOpenXRARSystem::SaveARPin(FName InName, UARPin* InPin)
{
	if (!IsLocalPinSaveSupported()) { return false; }

	return CustomAnchorSupport->SaveARPin(TrackingSystem->GetSession(), InName, InPin);
}

void FOpenXRARSystem::RemoveSavedARPin(FName InName)
{
	if (!IsLocalPinSaveSupported()) { return; }

	CustomAnchorSupport->RemoveSavedARPin(TrackingSystem->GetSession(), InName);
}

void FOpenXRARSystem::RemoveAllSavedARPins()
{
	if (!IsLocalPinSaveSupported()) { return; }

	CustomAnchorSupport->RemoveAllSavedARPins(TrackingSystem->GetSession());
}


//=========== End of Pins =============================================

bool FOpenXRARSystem::OnStartARGameFrame(FWorldContext& WorldContext)
{
	UpdateAnchors();

	return true;
}


/**
* Pure virtual that must be overloaded by the inheriting class. Use this
* method to serialize any UObjects contained that you wish to keep around.
*
* @param Collector The collector of referenced objects.
*/
void FOpenXRARSystem::AddReferencedObjects(FReferenceCollector& Collector) 
{
	Collector.AddReferencedObject(SessionConfig);
	Collector.AddReferencedObjects(Pins);
}



/** Used to init our AR system */
IARSystemSupport* OpenXRARModuleImpl::CreateARSystem()
{
	IARSystemSupport* ARSystemPtr = nullptr;
	if (!ARSystem.IsValid())
	{
		ARSystem = MakeShareable(new FOpenXRARSystem());
	}
	ARSystemPtr = ARSystem.Get();
	return ARSystemPtr;
}

void OpenXRARModuleImpl::SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem)
{
	ARSystem->SetTrackingSystem(InTrackingSystem);
}

void OpenXRARModuleImpl::StartupModule()
{
	ensureMsgf(FModuleManager::Get().LoadModule("AugmentedReality"), TEXT("OpenXR depends on the AugmentedReality module."));
}

void OpenXRARModuleImpl::ShutdownModule()
{
	ARSystem = nullptr;
}

bool OpenXRARModuleImpl::GetExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	return true;
}


IMPLEMENT_MODULE(OpenXRARModuleImpl, OpenXRAR)

DEFINE_LOG_CATEGORY(LogOpenXRAR)

#undef LOCTEXT_NAMESPACE
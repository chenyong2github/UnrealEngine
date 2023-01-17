// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSettings.h"
#include "PCGContext.h"
#include "PCGCustomVersion.h"
#include "PCGPin.h"
#include "PCGSubsystem.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "UObject/ObjectSaveContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSettings)

#if WITH_EDITOR
#include "Editor.h"
#endif

/** In order to reuse the cache when only debug settings change, we must make sure to ignore these from the CRC check */
class FPCGSettingsObjectCrc32 : public FArchiveObjectCrc32
{
public:
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
#if WITH_EDITOR
		return InProperty && (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, DebugSettings) || InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, DeterminismSettings));
#else
		return FArchiveObjectCrc32::ShouldSkipProperty(InProperty);
#endif // WITH_EDITOR
	}
};

bool UPCGSettingsInterface::IsInstance() const
{
	return this != GetSettings();
}

void UPCGSettingsInterface::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
#if WITH_EDITOR
		if (UPCGSettings* Settings = GetSettings())
		{
			const bool bIsStructuralChange = Settings->IsStructuralProperty(GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bEnabled));
			OnSettingsChangedDelegate.Broadcast(Settings, (bIsStructuralChange ? EPCGChangeType::Structural : EPCGChangeType::None) | EPCGChangeType::Settings);
		}
#endif
	}
}

bool UPCGSettings::operator==(const UPCGSettings& Other) const
{
	if (this == &Other)
	{
		return true;
	}
	else
	{
		FPCGSettingsObjectCrc32 Ar;
		uint32 ThisCrc = Ar.Crc32(const_cast<UPCGSettings*>(this));
		uint32 OtherCrc = Ar.Crc32(const_cast<UPCGSettings*>(&Other));
		return ThisCrc == OtherCrc;
	}
}

uint32 UPCGSettings::GetCrc32() const
{
	FPCGSettingsObjectCrc32 Ar;
	return Ar.Crc32(const_cast<UPCGSettings*>(this));
}

#if WITH_EDITOR
void UPCGSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	DataVersion = FPCGCustomVersion::LatestVersion;
}
#endif // WITH_EDITOR

void UPCGSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (ExecutionMode_DEPRECATED != EPCGSettingsExecutionMode::Enabled)
	{
		bEnabled = ExecutionMode_DEPRECATED != EPCGSettingsExecutionMode::Disabled;
		bDebug = ExecutionMode_DEPRECATED == EPCGSettingsExecutionMode::Debug;
		ExecutionMode_DEPRECATED = EPCGSettingsExecutionMode::Enabled;
	}
#endif
}

void UPCGSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FPCGCustomVersion::GUID);

#if WITH_EDITOR
	if (Ar.IsLoading())
	{
		// Some data migration must happen after the graph is fully initialized, such as manipulating node connections, so we
		// store off the loaded version number to be used later.
		DataVersion = Ar.CustomVer(FPCGCustomVersion::GUID);
	}
#endif // WITH_EDITOR

	// An additional custom version number that can be driven by external system users to track system modifications. To use a custom
	// version in user settings objects, override the GetUserCustomVersionGuid() method.
	const FGuid UserDataGuid = GetUserCustomVersionGuid();
	const bool bUsingCustomUserVersion = UserDataGuid != FGuid();

	if (bUsingCustomUserVersion)
	{
		Ar.UsingCustomVersion(UserDataGuid);

#if WITH_EDITOR
		if (Ar.IsLoading())
		{
			// Some data migration must happen after the graph is fully initialized, such as manipulating node connections, so we
			// store off the loaded version number to be used later.
			UserDataVersion = Ar.CustomVer(UserDataGuid);
		}
#endif // WITH_EDITOR
	}
}

void UPCGSettings::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);

#if WITH_EDITOR
	// This will get called when an external settings gets saved;
	// This is to trigger generation on save, if we've called changed properties from a blueprint
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Structural);
#endif
}

#if WITH_EDITOR
UObject* UPCGSettings::GetJumpTargetForDoubleClick() const
{
	return const_cast<UObject*>(Cast<UObject>(this));
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	// This is not true for everything, use a virtual call?
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Spatial);

	return PinProperties;
}

TArray<FPCGPinProperties>
UPCGSettings::DefaultInputPinProperties() const
{
	return InputPinProperties();
}

TArray<FPCGPinProperties>
UPCGSettings::DefaultOutputPinProperties() const
{
	return OutputPinProperties();
}

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

#if WITH_EDITOR
void UPCGSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() != GET_MEMBER_NAME_CHECKED(UPCGSettings, DeterminismSettings))
	{
		OnSettingsChangedDelegate.Broadcast(this, IsStructuralProperty(PropertyChangedEvent.GetPropertyName()) ? EPCGChangeType::Structural : EPCGChangeType::Settings);
	}
}

void UPCGSettings::DirtyCache()
{
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		UPCGSubsystem* PCGSubsystem = World ? World->GetSubsystem<UPCGSubsystem>() : nullptr;
		if (PCGSubsystem)
		{
			PCGSubsystem->CleanFromCache(GetElement().Get());
		}
	}
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGSettings::DefaultPointOutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return Properties;
}

void UPCGSettingsInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.AddUObject(this, &UPCGSettingsInstance::OnSettingsChanged);
		Settings->ConditionalPostLoad();
	}
#endif

#if WITH_EDITOR
	OriginalSettings = Settings;
#endif
}

void UPCGSettingsInstance::BeginDestroy()
{
#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

void UPCGSettingsInstance::SetSettings(UPCGSettings* InSettings)
{
#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Settings = InSettings;
#if WITH_EDITOR
	OriginalSettings = Settings;
#endif

#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.AddUObject(this, &UPCGSettingsInstance::OnSettingsChanged);
	}
#endif
}

#if WITH_EDITOR
void UPCGSettingsInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Some setting in the instance has changed. We don't have a flag for that yet (to add if needed)
	// However, we can make it behave like a standard change
	OnSettingsChangedDelegate.Broadcast(Settings, EPCGChangeType::Settings);
}

void UPCGSettingsInstance::OnSettingsChanged(UPCGSettings* InSettings, EPCGChangeType ChangeType)
{
	if (InSettings == Settings)
	{
		OnSettingsChangedDelegate.Broadcast(InSettings, ChangeType);
	}
}
#endif

FPCGElementPtr UPCGTrivialSettings::CreateElement() const
{
	return MakeShared<FPCGTrivialElement>();
}

bool FPCGTrivialElement::ExecuteInternal(FPCGContext* Context) const
{
	// Pass-through
	Context->OutputData = Context->InputData;
	return true;
}

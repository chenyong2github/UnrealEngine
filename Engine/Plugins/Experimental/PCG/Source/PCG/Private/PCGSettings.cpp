// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSettings.h"
#include "PCGNode.h"
#include "Serialization/ArchiveObjectCrc32.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

/** In order to reuse the cache when only debug settings change, we must make sure to ignore these from the CRC check */
class FPCGSettingsObjectCrc32 : public FArchiveObjectCrc32
{
public:
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
#if WITH_EDITORONLY_DATA
		return InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, DebugSettings);
#else
		return FArchiveObjectCrc32::ShouldSkipProperty(InProperty);
#endif // WITH_EDITORONLY_DATA
	}
};

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

TArray<FName> UPCGSettings::InLabels() const
{
	return { PCGPinConstants::DefaultInputLabel };
}

TArray<FName> UPCGSettings::OutLabels() const
{
	return { PCGPinConstants::DefaultOutputLabel };
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
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Settings);
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
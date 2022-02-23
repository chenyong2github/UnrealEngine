// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGExecuteBlueprint.h"

#if WITH_EDITOR
#include "Engine/World.h"

namespace PCGBlueprintHelper
{
	void GatherDependencies(UObject* Object, TSet<TObjectPtr<UObject>>& OutDependencies)
	{
		UClass* ObjectClass = Object ? Object->GetClass() : nullptr;

		if (!ObjectClass)
		{
			return;
		}

		for (FProperty* Property = ObjectClass->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
		{
			GatherDependencies(Property, Object, OutDependencies);
		}
	}

	// Inspired by IteratePropertiesRecursive in ObjectPropertyTrace.cpp
	void GatherDependencies(FProperty* Property, const void* InContainer, TSet<TObjectPtr<UObject>>& OutDependencies)
	{
		auto AddToDependenciesAndGatherRecursively = [&OutDependencies](UObject* Object) {
			if (Object && !OutDependencies.Contains(Object))
			{
				OutDependencies.Add(Object);
				GatherDependencies(Object, OutDependencies);
			}
		};

		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* Object = ObjectProperty->GetPropertyValue_InContainer(InContainer);
			AddToDependenciesAndGatherRecursively(Object);
		}
		else if (FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property))
		{
			FWeakObjectPtr WeakObject = WeakObjectProperty->GetPropertyValue_InContainer(InContainer);
			AddToDependenciesAndGatherRecursively(WeakObject.Get());
		}
		else if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue_InContainer(InContainer);
			AddToDependenciesAndGatherRecursively(SoftObject.Get());
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const void* StructContainer = StructProperty->ContainerPtrToValuePtr<const void>(InContainer);
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				GatherDependencies(*It, StructContainer, OutDependencies);
			}
		}
		else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper_InContainer Helper(ArrayProperty, InContainer);
			for (int32 DynamicIndex = 0; DynamicIndex < Helper.Num(); ++DynamicIndex)
			{
				const void* ValuePtr = Helper.GetRawPtr(DynamicIndex);
				GatherDependencies(ArrayProperty->Inner, ValuePtr, OutDependencies);
			}
		}
		else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper_InContainer Helper(MapProperty, InContainer);
			int32 Num = Helper.Num();
			for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
			{
				if (Helper.IsValidIndex(DynamicIndex))
				{
					const void* KeyPtr = Helper.GetKeyPtr(DynamicIndex);
					GatherDependencies(MapProperty->KeyProp, KeyPtr, OutDependencies);

					const void* ValuePtr = Helper.GetValuePtr(DynamicIndex);
					GatherDependencies(MapProperty->ValueProp, ValuePtr, OutDependencies);

					--Num;
				}
			}
		}
		else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper_InContainer Helper(SetProperty, InContainer);
			int32 Num = Helper.Num();
			for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
			{
				if (Helper.IsValidIndex(DynamicIndex))
				{
					const void* ValuePtr = Helper.GetElementPtr(DynamicIndex);
					GatherDependencies(SetProperty->ElementProp, ValuePtr, OutDependencies);

					--Num;
				}
			}
		}
	}

	TSet<TObjectPtr<UObject>> GetDataDependencies(UPCGBlueprintElement* InElement)
	{
		check(InElement && InElement->GetClass());
		UClass* BPClass = InElement->GetClass();

		TSet<TObjectPtr<UObject>> Dependencies;
		GatherDependencies(InElement, Dependencies);
		return Dependencies;
	}
}
#endif // WITH_EDITOR

UWorld* UPCGBlueprintElement::GetWorld() const
{
#if WITH_EDITOR
	return GWorld;
#else
	return nullptr;
#endif
}

void UPCGBlueprintElement::PostLoad()
{
	Super::PostLoad();
	Initialize();
}

void UPCGBlueprintElement::BeginDestroy()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif

	Super::BeginDestroy();
}

void UPCGBlueprintElement::Initialize()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UPCGBlueprintElement::OnDependencyChanged);
	DataDependencies = PCGBlueprintHelper::GetDataDependencies(this);
#endif
}

#if WITH_EDITOR
void UPCGBlueprintElement::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Since we don't really know what changed, let's just rebuild our data dependencies
	DataDependencies = PCGBlueprintHelper::GetDataDependencies(this);

	OnBlueprintChangedDelegate.Broadcast(this);
}

void UPCGBlueprintElement::OnDependencyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	if (!DataDependencies.Contains(Object))
	{
		return;
	}

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
	if (InElement == BlueprintElementInstance)
	{
		// When a data dependency is changed, this means we have to dirty the cache, otherwise it will not register as a change.
		DirtyCache();

		OnSettingsChangedDelegate.Broadcast(this);
	}
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
		BlueprintElementInstance->Initialize();
		SetupBlueprintElementEvent();
	}
	else
	{
		BlueprintElementInstance = nullptr;
	}	
}

#if WITH_EDITOR
void UPCGBlueprintSettings::GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings) const
{
#if WITH_EDITORONLY_DATA
	for (const FName& Tag : TrackedActorTags)
	{
		OutTagToSettings.FindOrAdd(Tag).Add(this);
	}
#endif // WITH_EDITORONLY_DATA
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGBlueprintSettings::CreateElement() const
{
	return MakeShared<FPCGExecuteBlueprintElement>();
}

bool FPCGExecuteBlueprintElement::ExecuteInternal(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExecuteBlueprintElement::Execute);
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

bool FPCGExecuteBlueprintElement::IsCacheable(const UPCGSettings* InSettings) const
{
	if (const UPCGBlueprintSettings* BPSettings = Cast<const UPCGBlueprintSettings>(InSettings))
	{
		return !BPSettings->bCreatesArtifacts;
	}
	else
	{
		return false;
	}
}
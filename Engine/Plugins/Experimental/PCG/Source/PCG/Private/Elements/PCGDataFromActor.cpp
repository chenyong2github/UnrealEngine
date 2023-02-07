// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDataFromActor.h"

#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "Data/PCGSpatialData.h"

#include "GameFramework/Actor.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDataFromActor)

#define LOCTEXT_NAMESPACE "PCGDataFromActorElement"

#if WITH_EDITOR
void UPCGDataFromActorSettings::GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	if (ActorSelector.ActorSelection == EPCGActorSelection::ByTag &&
		ActorSelector.ActorFilter == EPCGActorFilter::AllWorldActors)
	{
		OutTagToSettings.FindOrAdd(ActorSelector.ActorSelectionTag).Add(this);
	}
}

FText UPCGDataFromActorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("DataFromActorTooltip", "Builds a collection of PCG-compatible data from the selected actors.");
}
#endif

FPCGElementPtr UPCGDataFromActorSettings::CreateElement() const
{
	return MakeShared<FPCGDataFromActorElement>();
}

TArray<FPCGPinProperties> UPCGDataFromActorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::OutputPinProperties();

	if (Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent || Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponentOrParseComponents)
	{
		for (const FName& Pin : ExpectedPins)
		{
			Pins.Emplace(Pin);
		}
	}

	return Pins;
}

FPCGContext* FPCGDataFromActorElement::Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node)
{
	FPCGDataFromActorContext* Context = new FPCGDataFromActorContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}

bool FPCGDataFromActorElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDataFromActorElement::Execute);

	check(InContext);
	FPCGDataFromActorContext* Context = static_cast<FPCGDataFromActorContext*>(InContext);

	const UPCGDataFromActorSettings* Settings = Context->GetInputSettings<UPCGDataFromActorSettings>();
	check(Settings);

	UWorld* World = Context->SourceComponent.IsValid() ? Context->SourceComponent->GetWorld() : nullptr;

	if (!Context->bPerformedQuery)
	{
		Context->FoundActors = PCGActorSelector::FindActors(Settings->ActorSelector, Context->SourceComponent.Get());
		Context->bPerformedQuery = true;

		if (Context->FoundActors.IsEmpty())
		{
			PCGE_LOG(Warning, "No matching actor was found.");
			return true;
		}

		// If we're looking for PCG component data, we might have to wait for it.
		if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent || Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponentOrParseComponents)
		{
			TArray<FPCGTaskId> WaitOnTaskIds;
			for (AActor* Actor : Context->FoundActors)
			{
				GatherWaitTasks(Actor, WaitOnTaskIds);
			}

			if (!WaitOnTaskIds.IsEmpty())
			{
				UPCGSubsystem* Subsystem = Context->SourceComponent.IsValid() ? Context->SourceComponent->GetSubsystem() : nullptr;
				if (Subsystem)
				{
					// Add a trivial task after these generations that wakes up this task
					Context->bIsPaused = true;

					Subsystem->ScheduleGeneric([Context]()
					{
						// Wake up the current task
						Context->bIsPaused = false;
						return true;
					}, Context->SourceComponent.Get(), WaitOnTaskIds);

					return false;
				}
				else
				{
					PCGE_LOG(Error, "Was unable to wait for end of generation tasks");
				}
			}
		}
	}

	if (Context->bPerformedQuery)
	{
		for (AActor* Actor : Context->FoundActors)
		{
			ProcessActor(Context, Settings, Actor);
		}
	}

	return true;
}

void FPCGDataFromActorElement::GatherWaitTasks(AActor* FoundActor, TArray<FPCGTaskId>& OutWaitTasks) const
{
	if (!FoundActor)
	{
		return;
	}

	TInlineComponentArray<UPCGComponent*, 1> PCGComponents;
	FoundActor->GetComponents(PCGComponents);

	for (UPCGComponent* Component : PCGComponents)
	{
		if (Component->IsGenerating())
		{
			OutWaitTasks.Add(Component->GetGenerationTaskId());
		}
	}
}

void FPCGDataFromActorElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const
{
	check(Context);
	check(Settings);

	if (!FoundActor || !IsValid(FoundActor))
	{
		return;
	}

	TInlineComponentArray<UPCGComponent*, 1> PCGComponents;
	bool bHasGeneratedPCGData = false;
	FProperty* FoundProperty = nullptr;

	if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent || Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponentOrParseComponents)
	{
		FoundActor->GetComponents(PCGComponents);

		for (UPCGComponent* Component : PCGComponents)
		{
			bHasGeneratedPCGData |= !Component->GetGeneratedGraphOutput().TaggedData.IsEmpty();
		}
	}
	else if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromProperty)
	{
		if (Settings->PropertyName != NAME_None)
		{
			FoundProperty = FindFProperty<FProperty>(FoundActor->GetClass(), Settings->PropertyName);
		}
	}

	// Some additional validation
	if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent && !bHasGeneratedPCGData)
	{
		PCGE_LOG(Warning, "Actor (%s) does not have any previously generated data.", *FoundActor->GetFName().ToString());
		return;
	}
	else if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromProperty && !FoundProperty)
	{
		PCGE_LOG(Warning, "Actor (%s) does not have a property name (%s)", *FoundActor->GetFName().ToString(), *Settings->PropertyName.ToString());
		return;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	if (bHasGeneratedPCGData)
	{
		for (UPCGComponent* Component : PCGComponents)
		{
			// TODO - Temporary behavior
			// At the moment, intersections that reside in the transient package can hold on to a reference on this data
			// which prevents proper garbage collection on map change, hence why we duplicate here. Normally, we would expect
			// this not to be a problem, as these intersections should be garbage collected, but this requires more investigation
			for (const FPCGTaggedData& TaggedData : Component->GetGeneratedGraphOutput().TaggedData)
			{
				FPCGTaggedData& DuplicatedTaggedData = Outputs.Add_GetRef(TaggedData);
				DuplicatedTaggedData.Data = Cast<UPCGData>(StaticDuplicateObject(TaggedData.Data, GetTransientPackage()));
			}
			//Outputs.Append(Component->GetGeneratedGraphOutput().TaggedData);
		}
	}
	else if (FoundProperty)
	{
		bool bAbleToGetProperty = false;
		const void* PropertyAddressData = FoundProperty->ContainerPtrToValuePtr<void>(FoundActor);
		// TODO: support more property types here
		// Pointer to UPCGData
		// Soft object pointer to UPCGData
		// Array of pcg data -> all on the default pin
		// Map of pcg data -> use key for pin? might not be robust
		// FPCGDataCollection
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(FoundProperty))
		{
			if (StructProperty->Struct == FPCGDataCollection::StaticStruct())
			{
				const FPCGDataCollection* CollectionInProperty = reinterpret_cast<const FPCGDataCollection*>(PropertyAddressData);
				Outputs.Append(CollectionInProperty->TaggedData);

				bAbleToGetProperty = true;
			}
		}

		if (!bAbleToGetProperty)
		{
			PCGE_LOG(Warning, "Actor (%s) property (%s) does not have a supported type", *FoundActor->GetFName().ToString(), *Settings->PropertyName.ToString());
		}
	}
	else
	{
		const bool bParseActor = (Settings->Mode != EPCGGetDataFromActorMode::GetSinglePoint);
		auto DataFilter = [Settings](EPCGDataType InDataType) { return Settings->DataFilter(InDataType); };
		FPCGDataCollection Collection = UPCGComponent::CreateActorPCGDataCollection(FoundActor, Context->SourceComponent.Get(), DataFilter, bParseActor);
		Outputs += Collection.TaggedData;
	}

	if (Context->SourceComponent.IsValid())
	{
		for (FPCGTaggedData& Output : Outputs)
		{
			if (UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Output.Data))
			{
				SpatialData->TargetActor = Context->SourceComponent->GetOwner();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

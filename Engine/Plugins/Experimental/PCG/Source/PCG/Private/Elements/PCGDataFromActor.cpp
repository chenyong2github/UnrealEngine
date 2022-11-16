// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDataFromActor.h"

#include "PCGComponent.h"
#include "Data/PCGSpatialData.h"

#include "GameFramework/Actor.h"
#include "UObject/Package.h"

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

bool FPCGDataFromActorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDataFromActorElement::Execute);

	check(Context);
	const UPCGDataFromActorSettings* Settings = Context->GetInputSettings<UPCGDataFromActorSettings>();
	check(Settings);

	UWorld* World = Context->SourceComponent.IsValid() ? Context->SourceComponent->GetWorld() : nullptr;
	AActor* FoundActor = PCGActorSelector::FindActor(Settings->ActorSelector, World, nullptr);

	if (!FoundActor)
	{
		PCGE_LOG(Warning, "No matching actor was found.");
		return true;
	}

	TArray<UPCGComponent*> PCGComponents;
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
		return true;
	}
	else if (Settings->Mode == EPCGGetDataFromActorMode::GetDataFromProperty && !FoundProperty)
	{
		PCGE_LOG(Warning, "Actor (%s) does not have a property name (%s)", *FoundActor->GetFName().ToString(), *Settings->PropertyName.ToString());
		return true;
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
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output.Data = UPCGComponent::CreateActorPCGData(FoundActor, Context->SourceComponent.Get(), /*bParseActor=*/Settings->Mode != EPCGGetDataFromActorMode::GetSinglePoint);
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

	return true;
}
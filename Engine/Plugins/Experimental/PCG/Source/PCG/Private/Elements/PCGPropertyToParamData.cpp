// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGPropertyToParamData.h"

#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "PCGComponent.h"
#include "PCGParamData.h"
#include "Helpers/PCGBlueprintHelpers.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGPropertyToParamData)

void UPCGPropertyToParamDataSettings::PostLoad()
{
	Super::PostLoad();

	// Migrate deprecated actor selection settings to struct if needed
	if (ActorSelection_DEPRECATED != EPCGActorSelection::ByTag ||
		ActorSelectionTag_DEPRECATED != NAME_None ||
		ActorSelectionName_DEPRECATED != NAME_None ||
		ActorSelectionClass_DEPRECATED != TSubclassOf<AActor>{} ||
		ActorFilter_DEPRECATED != EPCGActorFilter::Self ||
		bIncludeChildren_DEPRECATED != false)
	{
		ActorSelector.ActorSelection = ActorSelection_DEPRECATED;
		ActorSelector.ActorSelectionTag = ActorSelectionTag_DEPRECATED;
		ActorSelector.ActorSelectionName = ActorSelectionName_DEPRECATED;
		ActorSelector.ActorSelectionClass = ActorSelectionClass_DEPRECATED;
		ActorSelector.ActorFilter = ActorFilter_DEPRECATED;
		ActorSelector.bIncludeChildren = bIncludeChildren_DEPRECATED;

		ActorSelection_DEPRECATED = EPCGActorSelection::ByTag;
		ActorSelectionTag_DEPRECATED = NAME_None;
		ActorSelectionName_DEPRECATED = NAME_None;
		ActorSelectionClass_DEPRECATED = TSubclassOf<AActor>{};
		ActorFilter_DEPRECATED = EPCGActorFilter::Self;
		bIncludeChildren_DEPRECATED = false;
	}
}

TArray<FPCGPinProperties> UPCGPropertyToParamDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);

	return PinProperties;
}

FPCGElementPtr UPCGPropertyToParamDataSettings::CreateElement() const
{
	return MakeShared<FPCGPropertyToParamDataElement>();
}

bool FPCGPropertyToParamDataElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPropertyToParamDataElement::Execute);

	check(Context);

	const UPCGPropertyToParamDataSettings* Settings = Context->GetInputSettings<UPCGPropertyToParamDataSettings>();
	check(Settings);

	// Early out if arguments are not specified
	if (Settings->PropertyName == NAME_None || (Settings->bSelectComponent && !Settings->ComponentClass))
	{
		PCGE_LOG(Error, "Some parameters are missing, abort.");
		return true;
	}

#if !WITH_EDITOR
	// If we have no output connected, nothing to do
	// Optimization possibly only in non-editor builds, otherwise we could poison the input-driven cache
	if (!Context->Node || !Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel))
	{
		PCGE_LOG(Verbose, "Node is not connected, nothing to do");
		return true;
	}
#endif

	// First find the actor depending on the selection
	UPCGComponent* OriginalComponent = UPCGBlueprintHelpers::GetOriginalComponent(*Context);
	AActor* FoundActor = PCGActorSelector::FindActor(Settings->ActorSelector, OriginalComponent);

	if (!FoundActor)
	{
		PCGE_LOG(Error, "No matching actor was found.");
		return true;
	}

	// From there, we either check the actor, or the component attached to it.
	UObject* ObjectToInspect = FoundActor;
	if (Settings->bSelectComponent)
	{
		ObjectToInspect = FoundActor->GetComponentByClass(Settings->ComponentClass);
		if (!ObjectToInspect)
		{
			PCGE_LOG(Error, "Component doesn't exist in the found actor.");
			return true;
		}
	}

	// Try to get the property
	FProperty* Property = FindFProperty<FProperty>(ObjectToInspect->GetClass(), Settings->PropertyName);
	if (!Property)
	{
		PCGE_LOG(Error, "Property doesn't exist in the found actor.");
		return true;
	}

	// From there, we should be able to create the data.
	UPCGParamData* ParamData = NewObject<UPCGParamData>();
	UPCGMetadata* Metadata = ParamData->MutableMetadata();
	check(Metadata);
	PCGMetadataEntryKey EntryKey = Metadata->AddEntry();

	if (!Metadata->SetAttributeFromProperty(Settings->OutputAttributeName, EntryKey, ObjectToInspect, Property, /*bCreate=*/ true))
	{
		PCGE_LOG(Error, "Error while creating an attribute. Either the property type is not supported by PCG or attribute creation failed.");
		return true;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = ParamData;

	return true;
}

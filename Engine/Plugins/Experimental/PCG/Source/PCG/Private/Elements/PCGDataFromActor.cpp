// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDataFromActor.h"

#include "PCGComponent.h"

FPCGElementPtr UPCGDataFromActorSettings::CreateElement() const
{
	return MakeShared<FPCGDataFromActorElement>();
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

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = UPCGComponent::CreateActorPCGData(FoundActor, Context->SourceComponent.Get(), /*bParseActor=*/true);

	return true;
}
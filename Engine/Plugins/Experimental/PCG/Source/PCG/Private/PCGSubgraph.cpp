// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSubgraph.h"
#include "PCGGraph.h"

void UPCGSubgraphSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Subgraph)
	{
		Subgraph->OnGraphChangedDelegate.AddUObject(this, &UPCGSubgraphSettings::OnSubgraphChanged);
	}
#endif
}

void UPCGSubgraphSettings::BeginDestroy()
{
#if WITH_EDITOR
	if (Subgraph)
	{
		Subgraph->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

UPCGNode* UPCGSubgraphSettings::CreateNode() const
{
	return NewObject<UPCGSubgraphNode>();
}

FPCGElementPtr UPCGSubgraphSettings::CreateElement() const
{
	return MakeShared<FPCGTrivialElement>();
}

TObjectPtr<UPCGGraph> UPCGSubgraphNode::GetGraph() const
{
	TObjectPtr<UPCGSubgraphSettings> Settings = Cast<UPCGSubgraphSettings>(DefaultSettings);
	return Settings ? Settings->Subgraph : nullptr;
}

#if WITH_EDITOR

void UPCGSubgraphSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSubgraphSettings, Subgraph))
	{
		if (Subgraph)
		{
			Subgraph->OnGraphChangedDelegate.RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGSubgraphSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSubgraphSettings, Subgraph))
	{
		if (Subgraph)
		{
			Subgraph->OnGraphChangedDelegate.AddUObject(this, &UPCGSubgraphSettings::OnSubgraphChanged);
		}
	}
}

void UPCGSubgraphSettings::OnSubgraphChanged(UPCGGraph* InGraph, bool bIsStructural)
{
	if (InGraph == Subgraph)
	{
		OnSettingsChangedDelegate.Broadcast(this);
	}
}

#endif // WITH_EDITOR

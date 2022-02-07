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

#if WITH_EDITOR
TArray<FName> UPCGSubgraphSettings::GetTrackedActorTags() const
{
	return Subgraph ? Subgraph->GetTrackedActorTags() : TArray<FName>();
}
#endif

FPCGElementPtr UPCGSubgraphSettings::CreateElement() const
{
	return MakeShared<FPCGTrivialElement>();
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
		if (bIsStructural)
		{
			OnStructuralSettingsChangedDelegate.Broadcast(this);
		}
		else
		{
			OnSettingsChangedDelegate.Broadcast(this);
		}
	}
}

#endif // WITH_EDITOR

TObjectPtr<UPCGGraph> UPCGSubgraphNode::GetGraph() const
{
	TObjectPtr<UPCGSubgraphSettings> Settings = Cast<UPCGSubgraphSettings>(DefaultSettings);
	return Settings ? Settings->Subgraph : nullptr;
}

void UPCGSubgraphNode::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
	{
		SubgraphSettings->OnStructuralSettingsChangedDelegate.AddUObject(this, &UPCGSubgraphNode::OnStructuralSettingsChanged);
	}
#endif
}

void UPCGSubgraphNode::BeginDestroy()
{
#if WITH_EDITOR
	if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
	{
		SubgraphSettings->OnStructuralSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGSubgraphNode::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
		{
			SubgraphSettings->OnStructuralSettingsChangedDelegate.RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGSubgraphNode::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Implementation note:
	// We must process structural changes before the parent class' otherwise the graph might be rescheduled with its tasks
	// before it is appropriately dirtied (which will trigger a recompilation)
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
		{
			SubgraphSettings->OnStructuralSettingsChangedDelegate.AddUObject(this, &UPCGSubgraphNode::OnStructuralSettingsChanged);

			// Changing the default settings should trigger immediately a structural change
			OnStructuralSettingsChanged(DefaultSettings);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGSubgraphNode::OnStructuralSettingsChanged(UPCGSettings* InSettings)
{
	if (InSettings == DefaultSettings)
	{
		OnNodeStructuralSettingsChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR
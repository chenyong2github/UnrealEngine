// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGNode.h"
#include "PCGGraph.h"
#include "PCGSettings.h"

UPCGNode::UPCGNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultSettings = ObjectInitializer.CreateDefaultSubobject<UPCGTrivialSettings>(this, TEXT("DefaultNodeSettings"));
}

void UPCGNode::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (DefaultSettings)
	{
		DefaultSettings->OnSettingsChangedDelegate.AddUObject(this, &UPCGNode::OnSettingsChanged);
	}
#endif
}

void UPCGNode::BeginDestroy()
{
#if WITH_EDITOR
	if (DefaultSettings)
	{
		DefaultSettings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

UPCGGraph* UPCGNode::GetGraph() const
{
	return Cast<UPCGGraph>(GetOuter());
}

UPCGNode* UPCGNode::AddEdgeTo(UPCGNode* To)
{
	check(GetGraph());
	if (UPCGGraph* Graph = GetGraph())
	{
		return Graph->AddEdge(this, To);
	}
	else
	{
		return nullptr;
	}
}

void UPCGNode::ConnectTo(UPCGNode* InSuccessor)
{
	OutboundNodes.AddUnique(InSuccessor);
}

void UPCGNode::ConnectFrom(UPCGNode* InPredecessor)
{
	InboundNodes.AddUnique(InPredecessor);
}

#if WITH_EDITOR

void UPCGNode::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (DefaultSettings)
		{
			DefaultSettings->OnSettingsChangedDelegate.RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGNode::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (DefaultSettings)
		{
			DefaultSettings->OnSettingsChangedDelegate.AddUObject(this, &UPCGNode::OnSettingsChanged);
		}
	}

	if (DefaultSettings)
	{
		OnSettingsChanged(DefaultSettings);
	}
}

void UPCGNode::OnSettingsChanged(UPCGSettings* InSettings)
{
	if (InSettings == DefaultSettings)
	{
		OnNodeSettingsChangedDelegate.Broadcast(this);
	}
}

#endif // WITH_EDITOR

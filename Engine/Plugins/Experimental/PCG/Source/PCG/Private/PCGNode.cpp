// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGNode.h"
#include "PCGEdge.h"
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

	ApplyDeprecation();
#endif
}

#if WITH_EDITOR
void UPCGNode::ApplyDeprecation()
{
	for (TObjectPtr<UPCGNode> OutboundNode : OutboundNodes_DEPRECATED)
	{
		if (!HasEdgeTo(OutboundNode))
		{
			AddEdgeTo(NAME_None, OutboundNode, NAME_None);
		}
	}
	OutboundNodes_DEPRECATED.Reset();
}
#endif

void UPCGNode::PostEditImport()
{
	Super::PostEditImport();
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

UPCGNode* UPCGNode::AddEdgeTo(FName InboundName, UPCGNode* To, FName OutboundName)
{
	check(GetGraph());
	if (UPCGGraph* Graph = GetGraph())
	{
		return Graph->AddLabeledEdge(this, InboundName, To, OutboundName);
	}
	else
	{
		return nullptr;
	}
}

FName UPCGNode::GetNodeTitle() const
{
	if (NodeTitle != NAME_None)
	{
		return NodeTitle;
	}
	else if (DefaultSettings)
	{
		if (DefaultSettings->AdditionalTaskName() != NAME_None)
		{
			return DefaultSettings->AdditionalTaskName();
		}
#if WITH_EDITOR
		else
		{
			return DefaultSettings->GetDefaultNodeName();
		}
#endif
	}

	return TEXT("Unnamed node");
}

bool UPCGNode::HasInLabel(const FName& Label) const
{
	return DefaultSettings && DefaultSettings->HasInLabel(Label);
}

bool UPCGNode::HasOutLabel(const FName& Label) const
{
	return DefaultSettings && DefaultSettings->HasOutLabel(Label);
}

TArray<FName> UPCGNode::InLabels() const
{
	return DefaultSettings ? DefaultSettings->InLabels() : TArray<FName>();
}

TArray<FName> UPCGNode::OutLabels() const
{
	return DefaultSettings ? DefaultSettings->OutLabels() : TArray<FName>();
}

bool UPCGNode::HasDefaultInLabel() const
{
	return !DefaultSettings || DefaultSettings->HasDefaultInLabel();
}

bool UPCGNode::HasDefaultOutLabel() const
{
	return !DefaultSettings || DefaultSettings->HasDefaultOutLabel();
}

bool UPCGNode::IsInputPinConnected(const FName& Label) const
{
	for (const UPCGEdge* InboundEdge : InboundEdges)
	{
		if (InboundEdge->OutboundLabel == Label)
		{
			return true;
		}
	}

	return false;
}

bool UPCGNode::IsOutputPinConnected(const FName& Label) const
{
	for (const UPCGEdge* OutboundEdge : OutboundEdges)
	{
		if (OutboundEdge->InboundLabel == Label)
		{
			return true;
		}
	}

	return false;
}

void UPCGNode::SetDefaultSettings(TObjectPtr<UPCGSettings> InSettings)
{
#if WITH_EDITOR
	const bool bDifferentSettings = (DefaultSettings != InSettings);
	if (bDifferentSettings && DefaultSettings)
	{
		DefaultSettings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	DefaultSettings = InSettings;

#if WITH_EDITOR
	if (bDifferentSettings && DefaultSettings)
	{
		DefaultSettings->OnSettingsChangedDelegate.AddUObject(this, &UPCGNode::OnSettingsChanged);
	}
#endif
}

UPCGEdge* UPCGNode::GetOutboundEdge(const FName& FromLabel, UPCGNode* To, const FName& ToLabel)
{
	for (UPCGEdge* OutboundEdge : OutboundEdges)
	{
		if (OutboundEdge->OutboundNode == To &&
			OutboundEdge->InboundLabel == FromLabel &&
			OutboundEdge->OutboundLabel == ToLabel)
		{
			return OutboundEdge;
		}
	}

	return nullptr;
}

bool UPCGNode::HasEdgeTo(UPCGNode* InNode) const
{
	for (const UPCGEdge* OutboundEdge : OutboundEdges)
	{
		if (OutboundEdge->OutboundNode == InNode)
		{
			return true;
		}
	}

	return false;
}

void UPCGNode::RemoveInboundEdge(UPCGEdge* InEdge)
{
	InboundEdges.Remove(InEdge);
}

void UPCGNode::RemoveOutboundEdge(UPCGEdge* InEdge)
{
	OutboundEdges.Remove(InEdge);
}

#if WITH_EDITOR

void UPCGNode::PreEditChange(FProperty* PropertyAboutToChange)
{
	// To properly clean up old callbacks during paste we clear during null call since the Settings property doesn't get a specific call.
	if (!PropertyAboutToChange || (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings)))
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
			OnSettingsChanged(DefaultSettings);
		}
	}
	else if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, NodeTitle))
	{
		OnNodeSettingsChangedDelegate.Broadcast(this, /*bSettingsChanged=*/false);
	}
}

void UPCGNode::OnSettingsChanged(UPCGSettings* InSettings)
{
	if (InSettings == DefaultSettings)
	{
		OnNodeSettingsChangedDelegate.Broadcast(this, /*bSettingsChanged=*/true);
	}
}

#endif // WITH_EDITOR

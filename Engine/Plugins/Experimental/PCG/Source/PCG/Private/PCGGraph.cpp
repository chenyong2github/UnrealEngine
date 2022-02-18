// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraph.h"
#include "PCGSubgraph.h"
#include "PCGSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

UPCGGraph::UPCGGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultInputNode"));
	OutputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultOutputNode"));

	// Note: default connection from input to output
	// should be added when creating from scratch,
	// but not when using a blueprint construct script.
	//InputNode->ConnectTo(OutputNode);
	//OutputNode->ConnectFrom(InputNode);
}

void UPCGGraph::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	for (UPCGNode* Node : Nodes)
	{
		OnNodeAdded(Node);
	}
#endif
}

void UPCGGraph::BeginDestroy()
{
#if WITH_EDITOR
	for (UPCGNode* Node : Nodes)
	{
		OnNodeRemoved(Node);
	}

	// Notify the compiler to remove this graph from its cache
	UWorld* World = GEditor->GetEditorWorldContext().World();
	UPCGSubsystem* PCGSubsystem = World ? World->GetSubsystem<UPCGSubsystem>() : nullptr;
	if (PCGSubsystem)
	{
		PCGSubsystem->NotifyGraphChanged(this);
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGGraph::InitializeFromTemplate()
{
	Modify();

	auto ResetDefaultNode = [](UPCGNode* InNode) {
		check(InNode);
		InNode->OutboundNodes.Reset();
		InNode->InboundNodes.Reset();

		// Reset settings as well
		InNode->DefaultSettings = NewObject<UPCGTrivialSettings>(InNode);
	};

	ResetDefaultNode(InputNode);
	ResetDefaultNode(OutputNode);

	for (UPCGNode* Node : Nodes)
	{
		OnNodeRemoved(Node);
	}

	Nodes.Reset();

	if (GraphTemplate)
	{
		bEnableGraphChangeNotifications = false;
		Cast<UPCGGraphSetupBP>(GraphTemplate->GetDefaultObject())->Setup(this);
		bEnableGraphChangeNotifications = true;
	}

	NotifyGraphChanged(/*bIsStructural=*/true);
}
#endif

UPCGNode* UPCGGraph::AddNodeOfType(TSubclassOf<class UPCGSettings> InSettingsClass)
{
	UPCGSettings* Settings = NewObject<UPCGSettings>(GetTransientPackage(), InSettingsClass);

	if (!Settings)
	{
		return nullptr;
	}

	UPCGNode* Node = Settings->CreateNode();

	if (Node)
	{
		Modify();

		// Assign settings to node
		Node->DefaultSettings = Settings;
		Settings->Rename(nullptr, Node);
		Settings->SetFlags(RF_Transactional);

		// Reparent node to this graph
		Node->Rename(nullptr, this);

#if WITH_EDITOR
		const FName DefaultNodeName = Settings->GetDefaultNodeName();
		if (DefaultNodeName != NAME_None)
		{
			FName NodeName = MakeUniqueObjectName(this, UPCGNode::StaticClass(), DefaultNodeName);
			Node->Rename(*NodeName.ToString());
		}
#endif

		Nodes.Add(Node);
		OnNodeAdded(Node);
	}

	return Node;
}

UPCGNode* UPCGGraph::AddNode(UPCGSettings* InSettings)
{
	if (!InSettings)
	{
		return nullptr;
	}

	UPCGNode* Node = InSettings->CreateNode();

	if (Node)
	{
		Modify();

		// Assign settings to node & reparent
		Node->DefaultSettings = InSettings;

		// Reparent node to this graph
		Node->Rename(nullptr, this);

#if WITH_EDITOR
		const FName DefaultNodeName = InSettings->GetDefaultNodeName();
		if (DefaultNodeName != NAME_None)
		{
			FName NodeName = MakeUniqueObjectName(GetOuter(), UPCGNode::StaticClass(), DefaultNodeName);
			Node->Rename(*NodeName.ToString());
		}
#endif

		Nodes.Add(Node);
		OnNodeAdded(Node);
	}

	return Node;
}

void UPCGGraph::OnNodeAdded(UPCGNode* InNode)
{
#if WITH_EDITOR
	InNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);

	if (UPCGSubgraphNode* SubgraphNode = Cast<UPCGSubgraphNode>(InNode))
	{
		SubgraphNode->OnNodeStructuralSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnStructuralSettingsChanged);
	}

	NotifyGraphChanged(/*bIsStructural=*/true);
#endif
}

void UPCGGraph::OnNodeRemoved(UPCGNode* InNode)
{
#if WITH_EDITOR
	if (InNode)
	{
		InNode->OnNodeSettingsChangedDelegate.RemoveAll(this);
		
		if (UPCGSubgraphNode* SubgraphNode = Cast<UPCGSubgraphNode>(InNode))
		{
			SubgraphNode->OnNodeStructuralSettingsChangedDelegate.RemoveAll(this);
		}
	}
#endif
}

UPCGNode* UPCGGraph::AddEdge(UPCGNode* From, UPCGNode* To)
{
	if (!From || !To)
	{
		// TODO: log error
		return To;
	}

	Modify();

	From->ConnectTo(To);
	To->ConnectFrom(From);

#if WITH_EDITOR
	NotifyGraphChanged(/*bIsStructural=*/true);
#endif

	return To;
}

bool UPCGGraph::Contains(UPCGNode* Node) const
{
	return Node == InputNode || Node == OutputNode || Nodes.Contains(Node);
}

#if WITH_EDITOR
FPCGTagToSettingsMap UPCGGraph::GetTrackedTagsToSettings() const
{
	FPCGTagToSettingsMap TagsToSettings;
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->DefaultSettings)
		{
			Node->DefaultSettings->GetTrackedActorTags(TagsToSettings);
		}
	}

	return TagsToSettings;
}

void UPCGGraph::NotifyGraphChanged(bool bIsStructural)
{
	if (!bEnableGraphChangeNotifications)
	{
		return;
	}

	// Notify the subsystem/compiler cache before so it gets recompiled properly
	if (bIsStructural)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		UPCGSubsystem* PCGSubsystem = World ? World->GetSubsystem<UPCGSubsystem>() : nullptr;
		if (PCGSubsystem)
		{
			PCGSubsystem->NotifyGraphChanged(this);
		}
	}

	OnGraphChangedDelegate.Broadcast(this, /*bIsStructural=*/bIsStructural);
}

void UPCGGraph::OnSettingsChanged(UPCGNode* InNode)
{
	NotifyGraphChanged(/*bIsStructural=*/false);
}

void UPCGGraph::OnStructuralSettingsChanged(UPCGNode* InNode)
{
	NotifyGraphChanged(/*bIsStructural=*/true);
}
#endif // WITH_EDITOR

// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraph.h"
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
		if (Node)
		{
			Node->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
		}
	}
#endif
}

void UPCGGraph::BeginDestroy()
{
#if WITH_EDITOR
	for (UPCGNode* Node : Nodes)
	{
		if (Node)
		{
			Node->OnNodeSettingsChangedDelegate.RemoveAll(this);
		}
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

	InputNode->OutboundNodes.Reset();
	OutputNode->InboundNodes.Reset();

	for (UPCGNode* Node : Nodes)
	{
		if (Node)
		{
			Node->OnNodeSettingsChangedDelegate.RemoveAll(this);
		}
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

#if WITH_EDITOR
		Node->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
		NotifyGraphChanged(/*bIsStructural=*/true);
#endif
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

#if WITH_EDITOR
		Node->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
		NotifyGraphChanged(/*bIsStructural=*/true);
#endif
	}

	return Node;
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
#endif // WITH_EDITOR

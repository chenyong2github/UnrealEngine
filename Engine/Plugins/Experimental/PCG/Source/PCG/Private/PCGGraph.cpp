// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGInputOutputSettings.h"
#include "PCGSubgraph.h"
#include "PCGSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

UPCGGraph::UPCGGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultInputNode"));
	InputNode->SetFlags(RF_Transactional);
	InputNode->SetDefaultSettings(ObjectInitializer.CreateDefaultSubobject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultInputNodeSettings")));
	Cast<UPCGGraphInputOutputSettings>(InputNode->DefaultSettings)->SetInput(true);
	OutputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultOutputNode"));
	OutputNode->SetFlags(RF_Transactional);
	OutputNode->SetDefaultSettings(ObjectInitializer.CreateDefaultSubobject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultOutputNodeSettings")));
	Cast<UPCGGraphInputOutputSettings>(OutputNode->DefaultSettings)->SetInput(false);
#if WITH_EDITORONLY_DATA
	OutputNode->PositionX = 200;
#endif

#if WITH_EDITOR
	InputNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
	OutputNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
#endif 

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
	// Deprecation
	InputNode->ApplyDeprecation();

	if (!Cast<UPCGGraphInputOutputSettings>(InputNode->DefaultSettings))
	{
		InputNode->DefaultSettings = NewObject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultInputNodeSettings"));
	}

	Cast<UPCGGraphInputOutputSettings>(InputNode->DefaultSettings)->SetInput(true);

	// Fixup edges on the input node
	for (UPCGEdge* Edge : InputNode->OutboundEdges)
	{
		if (Edge->InboundLabel == NAME_None)
		{
			Edge->InboundLabel = TEXT("In");
		}
	}

	OutputNode->ApplyDeprecation();

	if (!Cast<UPCGGraphInputOutputSettings>(OutputNode->DefaultSettings))
	{
		OutputNode->DefaultSettings = NewObject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultOutputNodeSettings"));
	}

	Cast<UPCGGraphInputOutputSettings>(OutputNode->DefaultSettings)->SetInput(false);

	// Fixup edges on the output node
	for (UPCGEdge* Edge : OutputNode->InboundEdges)
	{
		if (Edge->OutboundLabel == NAME_None)
		{
			Edge->OutboundLabel = TEXT("Out");
		}
	}

	// Fixup edges on subgraph nodes
	for (UPCGNode* Node : Nodes)
	{
		if(Cast<UPCGBaseSubgraphSettings>(Node->DefaultSettings))
		{
			for (UPCGEdge* Edge : Node->InboundEdges)
			{
				if (Edge->OutboundLabel == NAME_None)
				{
					Edge->OutboundLabel = TEXT("In");
				}
			}

			for (UPCGEdge* Edge : Node->OutboundEdges)
			{
				if (Edge->InboundLabel == NAME_None)
				{
					Edge->InboundLabel = TEXT("Out");
				}
			}
		}
	}
#endif

#if WITH_EDITOR
	InputNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);
	OutputNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);

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

	OutputNode->OnNodeSettingsChangedDelegate.RemoveAll(this);
	InputNode->OnNodeSettingsChangedDelegate.RemoveAll(this);

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

	// Disable notification until the end of this method, otherwise it will cause issues in proper refresh dispatch
	DisableNotificationsForEditor();

	auto ResetDefaultNode = [](UPCGNode* InNode) {
		check(InNode);
		InNode->OutboundEdges.Reset();
		InNode->InboundEdges.Reset();

		// Reset settings as well
		InNode->SetDefaultSettings(NewObject<UPCGTrivialSettings>(InNode));
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
		Cast<UPCGGraphSetupBP>(GraphTemplate->GetDefaultObject())->Setup(this);
	}

	// Reenable notifications and notify listeners as needed
	EnableNotificationsForEditor();
}
#endif

UPCGNode* UPCGGraph::AddNodeOfType(TSubclassOf<class UPCGSettings> InSettingsClass, UPCGSettings*& OutDefaultNodeSettings)
{
	UPCGSettings* Settings = NewObject<UPCGSettings>(GetTransientPackage(), InSettingsClass);

	if (!Settings)
	{
		return nullptr;
	}

	UPCGNode* Node = Settings->CreateNode();

	if (Node)
	{
		Node->SetFlags(RF_Transactional);

		Modify();

		// Assign settings to node
		Node->SetDefaultSettings(Settings);
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

	OutDefaultNodeSettings = Settings;
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
		Node->SetFlags(RF_Transactional);

		Modify();

		// Assign settings to node & reparent
		Node->SetDefaultSettings(InSettings);

		// Reparent node to this graph
		Node->Rename(nullptr, this);

#if WITH_EDITOR
		const FName DefaultNodeName = InSettings->GetDefaultNodeName();
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

		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif
}

UPCGNode* UPCGGraph::AddEdge(UPCGNode* From, UPCGNode* To)
{
	return AddLabeledEdge(From, NAME_None, To, NAME_None);
}

UPCGNode* UPCGGraph::AddLabeledEdge(UPCGNode* From, const FName& InboundLabel, UPCGNode* To, const FName& OutboundLabel)
{
	if (!From || !To)
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid edge nodes"));
		return To;
	}

	if (!From->HasOutLabel(InboundLabel))
	{
		UE_LOG(LogPCG, Error, TEXT("From node %s does not have the %s label"), *From->GetName(), *InboundLabel.ToString());
	}

	if (!To->HasInLabel(OutboundLabel))
	{
		UE_LOG(LogPCG, Error, TEXT("To node %s does not have the %s label"), *To->GetName(), *OutboundLabel.ToString());
	}

	Modify();
	From->Modify();
	To->Modify();

	// Create edge
	UPCGEdge* Edge = NewObject<UPCGEdge>(this);
	Edge->InboundLabel = InboundLabel;
	Edge->InboundNode = From;
	Edge->OutboundLabel = OutboundLabel;
	Edge->OutboundNode = To;

	From->OutboundEdges.Add(Edge);
	To->InboundEdges.Add(Edge);
	
#if WITH_EDITOR
	NotifyGraphChanged(/*bIsStructural=*/true);
#endif

	return To;
}

bool UPCGGraph::Contains(UPCGNode* Node) const
{
	return Node == InputNode || Node == OutputNode || Nodes.Contains(Node);
}

void UPCGGraph::AddNode(UPCGNode* InNode)
{
	check(InNode);

	Modify();

	InNode->Rename(nullptr, this);

#if WITH_EDITOR
	const FName DefaultNodeName = InNode->DefaultSettings->GetDefaultNodeName();
	if (DefaultNodeName != NAME_None)
	{
		FName NodeName = MakeUniqueObjectName(this, UPCGNode::StaticClass(), DefaultNodeName);
		InNode->Rename(*NodeName.ToString());
	}
#endif

	Nodes.Add(InNode);
	OnNodeAdded(InNode);
}

void UPCGGraph::RemoveNode(UPCGNode* InNode)
{
	check(InNode);

	Modify();

	for (int32 EdgeIndex = InNode->InboundEdges.Num() - 1; EdgeIndex >= 0; --EdgeIndex)
	{
		UPCGEdge* Edge = InNode->InboundEdges[EdgeIndex];
		Edge->BreakEdge();
	}

	for (int32 EdgeIndex = InNode->OutboundEdges.Num() - 1; EdgeIndex >= 0; --EdgeIndex)
	{
		UPCGEdge* Edge = InNode->OutboundEdges[EdgeIndex];
		Edge->BreakEdge();
	}

	Nodes.Remove(InNode);
	OnNodeRemoved(InNode);
}

bool UPCGGraph::RemoveEdge(UPCGNode* From, const FName& FromLabel, UPCGNode* To, const FName& ToLabel)
{
	if (!From || !To)
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid from/to node in RemoveEdge"));
		return false;
	}

	bool bChanged = false;

	if (UPCGEdge* Edge = From->GetOutboundEdge(FromLabel, To, ToLabel))
	{
		Edge->BreakEdge();
		bChanged = true;
	}
	else
	{
		UE_LOG(LogPCG, Warning, TEXT("Edge does not exist with labels %s and %s"), *FromLabel.ToString(), *ToLabel.ToString());
	}

#if WITH_EDITOR
	if (bChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveAllInboundEdges(UPCGNode* InNode)
{
	check(InNode);
	const bool bChanged = !InNode->InboundEdges.IsEmpty();

	for (int32 i = InNode->InboundEdges.Num() - 1; i >= 0; --i)
	{
		InNode->InboundEdges[i]->BreakEdge();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveAllOutboundEdges(UPCGNode* InNode)
{
	check(InNode);
	const bool bChanged = !InNode->OutboundEdges.IsEmpty();

	for (int32 i = InNode->OutboundEdges.Num() - 1; i >= 0; --i)
	{
		InNode->OutboundEdges[i]->BreakEdge();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveInboundEdges(UPCGNode* InNode, const FName& InboundLabel)
{
	check(InNode);
	bool bChanged = false;

	for (int32 i = InNode->InboundEdges.Num() - 1; i >= 0; --i)
	{
		if (InNode->InboundEdges[i]->OutboundLabel == InboundLabel)
		{
			InNode->InboundEdges[i]->BreakEdge();
			bChanged = true;
		}
	}

#if WITH_EDITOR
	if (bChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveOutboundEdges(UPCGNode* InNode, const FName& OutboundLabel)
{
	check(InNode);
	bool bChanged = false;

	for (int32 i = InNode->OutboundEdges.Num() - 1; i >= 0; --i)
	{
		if (InNode->OutboundEdges[i]->InboundLabel == OutboundLabel)
		{
			InNode->OutboundEdges[i]->BreakEdge();
			bChanged = true;
		}
	}

#if WITH_EDITOR
	if (bChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/true);
	}
#endif

	return bChanged;
}

#if WITH_EDITOR
void UPCGGraph::PreNodeUndo(UPCGNode* InPCGNode)
{
	if (InPCGNode)
	{
		InPCGNode->OnNodeSettingsChangedDelegate.RemoveAll(this);

		if (UPCGSubgraphNode* SubgraphNode = Cast<UPCGSubgraphNode>(InPCGNode))
		{
			SubgraphNode->OnNodeStructuralSettingsChangedDelegate.RemoveAll(this);
		}
	}
}

void UPCGGraph::PostNodeUndo(UPCGNode* InPCGNode)
{
	if (InPCGNode)
	{
		InPCGNode->OnNodeSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnSettingsChanged);

		if (UPCGSubgraphNode* SubgraphNode = Cast<UPCGSubgraphNode>(InPCGNode))
		{
			SubgraphNode->OnNodeStructuralSettingsChangedDelegate.AddUObject(this, &UPCGGraph::OnStructuralSettingsChanged);
		}
	}
}
#endif

#if WITH_EDITOR
void UPCGGraph::DisableNotificationsForEditor()
{
	check(GraphChangeNotificationsDisableCounter >= 0);
	++GraphChangeNotificationsDisableCounter;
}

void UPCGGraph::EnableNotificationsForEditor()
{
	check(GraphChangeNotificationsDisableCounter > 0);
	--GraphChangeNotificationsDisableCounter;

	if (GraphChangeNotificationsDisableCounter == 0 && bDelayedChangeNotification)
	{
		NotifyGraphChanged(bDelayedChangeNotificationStructural);
		bDelayedChangeNotification = false;
		bDelayedChangeNotificationStructural = false;
	}
}
#endif

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
	if(GraphChangeNotificationsDisableCounter > 0)
	{
		bDelayedChangeNotification = true;
		bDelayedChangeNotificationStructural |= bIsStructural;
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

void UPCGGraph::OnSettingsChanged(UPCGNode* InNode, bool bSettingsChanged)
{
	if (bSettingsChanged)
	{
		NotifyGraphChanged(/*bIsStructural=*/false);
	}
}

void UPCGGraph::OnStructuralSettingsChanged(UPCGNode* InNode)
{
	NotifyGraphChanged(/*bIsStructural=*/true);
}
#endif // WITH_EDITOR

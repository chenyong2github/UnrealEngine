// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraph.h"

#include "PCGComponent.h"
#include "PCGInputOutputSettings.h"
#include "PCGModule.h"
#include "PCGPin.h"
#include "PCGSubsystem.h"
#include "PCGSubgraph.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGraph)

#if WITH_EDITOR
#include "Editor.h"
#else
#include "UObject/Package.h"
#endif

/****************************
* UPCGGraphInterface
****************************/

bool UPCGGraphInterface::IsInstance() const
{
	return this != GetGraph();
}

bool UPCGGraphInterface::IsEquivalent(const UPCGGraphInterface* Other) const
{
	if (this == Other)
	{
		return true;
	}

	const UPCGGraph* OtherGraph = Other ? Other->GetGraph() : nullptr;

	return GetGraph() == OtherGraph;
}

/****************************
* UPCGGraph
****************************/

UPCGGraph::UPCGGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultInputNode"));
	InputNode->SetFlags(RF_Transactional);

	// Since pins would be allocated after initializing the input/output nodes, we must make sure to allocate them using the object initializer
	int NumAllocatedPins = 1;
	auto PinAllocator = [&ObjectInitializer, &NumAllocatedPins](UPCGNode* Node)
	{
		FName DefaultPinName = TEXT("DefaultPin");
		DefaultPinName.SetNumber(NumAllocatedPins++);
		return ObjectInitializer.CreateDefaultSubobject<UPCGPin>(Node, DefaultPinName);
	};

	UPCGGraphInputOutputSettings* InputSettings = ObjectInitializer.CreateDefaultSubobject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultInputNodeSettings"));
	InputSettings->SetInput(true);
	InputNode->SetSettingsInterface(InputSettings, /*bUpdatePins=*/false);

	// Only allocate default pins if this is the default object
	if (this->HasAnyFlags(RF_ClassDefaultObject))
	{
		InputNode->CreateDefaultPins(PinAllocator);
	}
	else
	{
		InputNode->UpdatePins();
	}

	OutputNode = ObjectInitializer.CreateDefaultSubobject<UPCGNode>(this, TEXT("DefaultOutputNode"));
	OutputNode->SetFlags(RF_Transactional);

	UPCGGraphInputOutputSettings* OutputSettings = ObjectInitializer.CreateDefaultSubobject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultOutputNodeSettings"));
	OutputSettings->SetInput(false);
	OutputNode->SetSettingsInterface(OutputSettings, /*bUpdatePins=*/false);

	// Only allocate default pins if this is the default object
	if (this->HasAnyFlags(RF_ClassDefaultObject))
	{
		OutputNode->CreateDefaultPins(PinAllocator);
	}
	else
	{
		OutputNode->UpdatePins();
	}

#if WITH_EDITOR
	OutputNode->PositionX = 200;
#endif

#if WITH_EDITOR
	InputNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);
	OutputNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);
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
	// TODO: review this once the data has been updated, it's unwanted weight going forward
	// Deprecation
	InputNode->ConditionalPostLoad();

	if (!Cast<UPCGGraphInputOutputSettings>(InputNode->GetSettings()))
	{
		InputNode->SetSettingsInterface(NewObject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultInputNodeSettings")));
	}

	Cast<UPCGGraphInputOutputSettings>(InputNode->GetSettings())->SetInput(true);

	OutputNode->ConditionalPostLoad();

	if (!Cast<UPCGGraphInputOutputSettings>(OutputNode->GetSettings()))
	{
		OutputNode->SetSettingsInterface(NewObject<UPCGGraphInputOutputSettings>(this, TEXT("DefaultOutputNodeSettings")));
	}

	Cast<UPCGGraphInputOutputSettings>(OutputNode->GetSettings())->SetInput(false);

	// Ensure that all nodes are loaded (& updated their deprecated data)
	for (UPCGNode* Node : Nodes)
	{
		Node->ConditionalPostLoad();
	}
	
	// Also do this for ExtraNodes
	for (UObject* ExtraNode : ExtraEditorNodes)
	{
		ExtraNode->ConditionalPostLoad();
	}

	// Finally, apply deprecation that changes edges/rebinds
	ForEachNode([](UPCGNode* InNode) { return InNode->ApplyDeprecationBeforeUpdatePins(); });

	// Update pins on all nodes
	ForEachNode([](UPCGNode* InNode) { return InNode->UpdatePins(); });

	// Finally, apply deprecation that changes edges/rebinds
	ForEachNode([](UPCGNode* InNode) { return InNode->ApplyDeprecation(); });

	InputNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);
	OutputNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);

	// Also, try to remove all nodes that are invalid (meaning that the settings are null)
	// We remove it at the end, to let the nodes that have null settings to clean up their pins and edges.
	for (int32 i = Nodes.Num() - 1; i >= 0; --i)
	{
		if (!Nodes[i]->GetSettings())
		{
			Nodes.RemoveAtSwap(i);
		}
		else
		{
			OnNodeAdded(Nodes[i]);
		}
	}
#endif
}

#if WITH_EDITOR
void UPCGGraph::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass)
{
	Super::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	OutConstructClasses.Add(FTopLevelAssetPath(UPCGPin::StaticClass()));
}
#endif

void UPCGGraph::BeginDestroy()
{
#if WITH_EDITOR
	for (UPCGNode* Node : Nodes)
	{
		OnNodeRemoved(Node);
	}

	OutputNode->OnNodeChangedDelegate.RemoveAll(this);
	InputNode->OnNodeChangedDelegate.RemoveAll(this);

	// Notify the compiler to remove this graph from its cache
	if (GEditor)
	{
		if (UPCGSubsystem* PCGSubsystem = UPCGSubsystem::GetInstance(GEditor->GetEditorWorldContext().World()))
		{
			PCGSubsystem->NotifyGraphChanged(this);
		}
	}

#endif

	Super::BeginDestroy();
}

UPCGNode* UPCGGraph::AddNodeOfType(TSubclassOf<class UPCGSettings> InSettingsClass, UPCGSettings*& OutDefaultNodeSettings)
{
	UPCGSettings* Settings = NewObject<UPCGSettings>(GetTransientPackage(), InSettingsClass, NAME_None, RF_Transactional);

	if (!Settings)
	{
		return nullptr;
	}

	UPCGNode* Node = AddNode(Settings);

	if (Node)
	{
		Settings->Rename(nullptr, Node);
	}

	OutDefaultNodeSettings = Settings;
	return Node;
}

UPCGNode* UPCGGraph::AddNode(UPCGSettingsInterface* InSettingsInterface)
{
	if (!InSettingsInterface || !InSettingsInterface->GetSettings())
	{
		return nullptr;
	}

	UPCGNode* Node = InSettingsInterface->GetSettings()->CreateNode();

	if (Node)
	{
		Node->SetFlags(RF_Transactional);

		Modify();

		// Assign settings to node & reparent
		Node->SetSettingsInterface(InSettingsInterface);

		// Reparent node to this graph
		Node->Rename(nullptr, this);

#if WITH_EDITOR
		const FName DefaultNodeName = InSettingsInterface->GetSettings()->GetDefaultNodeName();
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

UPCGNode* UPCGGraph::AddNodeInstance(UPCGSettings* InSettings)
{
	if (!InSettings)
	{
		return nullptr;
	}

	UPCGSettingsInstance* SettingsInstance = NewObject<UPCGSettingsInstance>();
	SettingsInstance->SetSettings(InSettings);

	UPCGNode* Node = AddNode(SettingsInstance);

	if (Node)
	{
		SettingsInstance->Rename(nullptr, Node);
		SettingsInstance->SetFlags(RF_Transactional);
	}

	return Node;
}

UPCGNode* UPCGGraph::AddNodeCopy(UPCGSettings* InSettings, UPCGSettings*& DefaultNodeSettings)
{
	if (!InSettings)
	{
		return nullptr;
	}

	UPCGSettings* SettingsCopy = DuplicateObject(InSettings, nullptr);
	UPCGNode* NewNode = AddNode(SettingsCopy);

	if (SettingsCopy)
	{
		SettingsCopy->Rename(nullptr, NewNode);
	}

	DefaultNodeSettings = SettingsCopy;
	return NewNode;
}

void UPCGGraph::OnNodeAdded(UPCGNode* InNode)
{
#if WITH_EDITOR
	InNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);
	NotifyGraphChanged(EPCGChangeType::Structural);
#endif
}

void UPCGGraph::OnNodeRemoved(UPCGNode* InNode)
{
#if WITH_EDITOR
	if (InNode)
	{
		InNode->OnNodeChangedDelegate.RemoveAll(this);
		NotifyGraphChanged(EPCGChangeType::Structural);
	}
#endif
}

UPCGNode* UPCGGraph::AddEdge(UPCGNode* From, const FName& FromPinLabel, UPCGNode* To, const FName& ToPinLabel)
{
	AddLabeledEdge(From, FromPinLabel, To, ToPinLabel);
	return To;
}

bool UPCGGraph::AddLabeledEdge(UPCGNode* From, const FName& FromPinLabel, UPCGNode* To, const FName& ToPinLabel)
{
	if (!From || !To)
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid edge nodes"));
		return false;
	}

	UPCGPin* FromPin = From->GetOutputPin(FromPinLabel);

	if(!FromPin)
	{
		UE_LOG(LogPCG, Error, TEXT("From node %s does not have the %s label"), *From->GetName(), *FromPinLabel.ToString());
		return false;
	}

	UPCGPin* ToPin = To->GetInputPin(ToPinLabel);

	if(!ToPin)
	{
		UE_LOG(LogPCG, Error, TEXT("To node %s does not have the %s label"), *To->GetName(), *ToPinLabel.ToString());
		return false;
	}

	// Create edge
	FromPin->AddEdgeTo(ToPin);

	bool bToPinBrokeOtherEdges = false;
	
	// Add an edge to a pin that doesn't allow multiple connections requires to do some cleanup
	if (!ToPin->AllowMultipleConnections())
	{
		bToPinBrokeOtherEdges = ToPin->BreakAllIncompatibleEdges();
	}
	
	From->UpdateDynamicPins();
	To->UpdateDynamicPins();

#if WITH_EDITOR
	NotifyGraphChanged(EPCGChangeType::Structural);
#endif

	return bToPinBrokeOtherEdges;
}

TObjectPtr<UPCGNode> UPCGGraph::ReconstructNewNode(const UPCGNode* InNode)
{
	UPCGSettings* NewSettings = nullptr;
	TObjectPtr<UPCGNode> NewNode = AddNodeCopy(InNode->GetSettings(), NewSettings);

#if WITH_EDITOR
	InNode->TransferEditorProperties(NewNode);
#endif // WITH_EDITOR

	return NewNode;
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
	const FName DefaultNodeName = InNode->GetSettings()->GetDefaultNodeName();
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

	for (UPCGPin* InputPin : InNode->InputPins)
	{
		InputPin->BreakAllEdges();
	}

	for (UPCGPin* OutputPin : InNode->OutputPins)
	{
		OutputPin->BreakAllEdges();
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

	UPCGPin* OutPin = From->GetOutputPin(FromLabel);
	UPCGPin* InPin = To->GetInputPin(ToLabel);

	const bool bChanged = OutPin && OutPin->BreakEdgeTo(InPin);

	if (bChanged)
	{
		From->UpdateDynamicPins();
		To->UpdateDynamicPins();

#if WITH_EDITOR
		NotifyGraphChanged(EPCGChangeType::Structural);
#endif
	}

	return bChanged;
}

void UPCGGraph::ForEachNode(const TFunction<void(UPCGNode*)>& Action)
{
	Action(InputNode);
	Action(OutputNode);

	for (UPCGNode* Node : Nodes)
	{
		Action(Node);
	}
}

bool UPCGGraph::RemoveAllInboundEdges(UPCGNode* InNode)
{
	check(InNode);
	bool bChanged = false;

	for (UPCGPin* InputPin : InNode->InputPins)
	{
		bChanged |= InputPin->BreakAllEdges();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		InNode->UpdateDynamicPins();
		NotifyGraphChanged(EPCGChangeType::Structural);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveAllOutboundEdges(UPCGNode* InNode)
{
	check(InNode);
	bool bChanged = false;
	for (UPCGPin* OutputPin : InNode->OutputPins)
	{
		bChanged |= OutputPin->BreakAllEdges();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		InNode->UpdateDynamicPins();
		NotifyGraphChanged(EPCGChangeType::Structural);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveInboundEdges(UPCGNode* InNode, const FName& InboundLabel)
{
	check(InNode);
	bool bChanged = false;

	if (UPCGPin* InputPin = InNode->GetInputPin(InboundLabel))
	{
		bChanged = InputPin->BreakAllEdges();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		InNode->UpdateDynamicPins();
		NotifyGraphChanged(EPCGChangeType::Structural);
	}
#endif

	return bChanged;
}

bool UPCGGraph::RemoveOutboundEdges(UPCGNode* InNode, const FName& OutboundLabel)
{
	check(InNode);
	bool bChanged = false;

	if (UPCGPin* OutputPin = InNode->GetOutputPin(OutboundLabel))
	{
		bChanged = OutputPin->BreakAllEdges();
	}

#if WITH_EDITOR
	if (bChanged)
	{
		InNode->UpdateDynamicPins();
		NotifyGraphChanged(EPCGChangeType::Structural);
	}
#endif

	return bChanged;
}

#if WITH_EDITOR
void UPCGGraph::ForceNotificationForEditor()
{
	// Queue up the delayed change
	NotifyGraphChanged(EPCGChangeType::Structural);
	if (bUserPausedNotificationsInGraphEditor)
	{
		EnableNotificationsForEditor();
		DisableNotificationsForEditor();
	}
}

void UPCGGraph::PreNodeUndo(UPCGNode* InPCGNode)
{
	if (InPCGNode)
	{
		InPCGNode->OnNodeChangedDelegate.RemoveAll(this);
	}
}

void UPCGGraph::PostNodeUndo(UPCGNode* InPCGNode)
{
	if (InPCGNode)
	{
		InPCGNode->OnNodeChangedDelegate.AddUObject(this, &UPCGGraph::OnNodeChanged);
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
		NotifyGraphChanged(DelayedChangeType);
		bDelayedChangeNotification = false;
		DelayedChangeType = EPCGChangeType::None;
	}
}

void UPCGGraph::ToggleUserPausedNotificationsForEditor()
{
	if (bUserPausedNotificationsInGraphEditor)
	{
		EnableNotificationsForEditor();
	}
	else
	{
		DisableNotificationsForEditor();
	}

	bUserPausedNotificationsInGraphEditor = !bUserPausedNotificationsInGraphEditor;
}

void UPCGGraph::SetExtraEditorNodes(const TArray<TObjectPtr<const UObject>>& InNodes)
{
	ExtraEditorNodes.Empty();

	for (const UObject* Node : InNodes)
	{
		ExtraEditorNodes.Add(DuplicateObject(Node, this));
	}
}

FPCGTagToSettingsMap UPCGGraph::GetTrackedTagsToSettings() const
{
	FPCGTagToSettingsMap TagsToSettings;
	TArray<TObjectPtr<const UPCGGraph>> VisitedGraphs;

	GetTrackedTagsToSettings(TagsToSettings, VisitedGraphs);
	return TagsToSettings;
}

void UPCGGraph::GetTrackedTagsToSettings(FPCGTagToSettingsMap& OutTagsToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	if (OutVisitedGraphs.Contains(this))
	{
		return;
	}

	OutVisitedGraphs.Emplace(this);

	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetSettings())
		{
			Node->GetSettings()->GetTrackedActorTags(OutTagsToSettings, OutVisitedGraphs);
		}
	}
}

void UPCGGraph::NotifyGraphChanged(EPCGChangeType ChangeType)
{
	if (GraphChangeNotificationsDisableCounter > 0)
	{
		bDelayedChangeNotification = true;
		DelayedChangeType |= ChangeType;
		return;
	}

	// Skip recursive cases which can happen either through direct recursivity (A -> A) or indirectly (A -> B -> A)
	if (bIsNotifying)
	{
		return;
	}

	bIsNotifying = true;

	// Notify the subsystem/compiler cache before so it gets recompiled properly
	const bool bNotifySubsystem = ((ChangeType & (EPCGChangeType::Structural | EPCGChangeType::Edge)) != EPCGChangeType::None);
	if (bNotifySubsystem && GEditor)
	{
		if (UPCGSubsystem* PCGSubsystem = UPCGSubsystem::GetInstance(GEditor->GetEditorWorldContext().World()))
		{
			PCGSubsystem->NotifyGraphChanged(this);
		}
	}

	OnGraphChangedDelegate.Broadcast(this, ChangeType);

	bIsNotifying = false;
}

void UPCGGraph::OnNodeChanged(UPCGNode* InNode, EPCGChangeType ChangeType)
{
	if((ChangeType & ~EPCGChangeType::Cosmetic) != EPCGChangeType::None)
	{
		NotifyGraphChanged(ChangeType);
	}
}

void UPCGGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGGraph, bLandscapeUsesMetadata))
	{
		NotifyGraphChanged(EPCGChangeType::Input);
	}
}
#endif // WITH_EDITOR

/****************************
* UPCGGraphInstance
****************************/

void UPCGGraphInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Graph)
	{
		Graph->ConditionalPostLoad();
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGGraphInstance::OnGraphChanged);
	}
#endif // WITH_EDITOR
}

void UPCGGraphInstance::BeginDestroy()
{
#if WITH_EDITOR
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif // WITH_EDITOR

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGGraphInstance::PreEditChange(FProperty* InProperty)
{
	Super::PreEditChange(InProperty);

	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGGraphInstance, Graph) && Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}
}

void UPCGGraphInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGGraphInstance, Graph) && Graph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGGraphInstance::OnGraphChanged);
	}
}

void UPCGGraphInstance::PreEditUndo()
{
	Super::PreEditUndo();

	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}

	UndoRedoGraphCache = Graph;
}

void UPCGGraphInstance::PostEditUndo()
{
	Super::PostEditUndo();

	if (Graph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGGraphInstance::OnGraphChanged);
	}
}

void UPCGGraphInstance::OnGraphChanged(UPCGGraphInterface* InGraph, EPCGChangeType ChangeType)
{
	if (InGraph == Graph)
	{
		OnGraphChangedDelegate.Broadcast(this, ChangeType);
	}
}

bool UPCGGraphInstance::CanEditChange(const FProperty* InProperty) const
{
	// Graph can only be changed if it is in a PCGComponent (not local) or a PCGSubgraphSettings
	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGGraphInstance, Graph))
	{
		UObject* Outer = this->GetOuter();

		if (UPCGComponent* Component = Cast<UPCGComponent>(Outer))
		{
			return !Component->IsLocalComponent();
		}
		else
		{
			return Outer && Outer->IsA<UPCGSubgraphSettings>();
		}
	}

	return true;
}
#endif

void UPCGGraphInstance::SetGraph(UPCGGraphInterface* InGraph)
{
	if (InGraph == this)
	{
		UE_LOG(LogPCG, Error, TEXT("Try to set the graph of a graph instance to itself, would cause infinite recursion."));
		return;
	}

#if WITH_EDITOR
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif // WITH_EDITOR

	Graph = InGraph;

#if WITH_EDITOR
	if (Graph)
	{
		Graph->OnGraphChangedDelegate.AddUObject(this, &UPCGGraphInstance::OnGraphChanged);
	}
#endif // WITH_EDITOR
}

TObjectPtr<UPCGGraphInterface> UPCGGraphInstance::CreateInstance(UObject* InOwner, UPCGGraphInterface* InGraph)
{
	if (!InOwner || !InGraph)
	{
		return nullptr;
	}

	TObjectPtr<UPCGGraphInstance> GraphInstance = NewObject<UPCGGraphInstance>(InOwner, MakeUniqueObjectName(InOwner, UPCGGraphInstance::StaticClass(), InGraph->GetFName()), RF_Transactional | RF_Public);
	GraphInstance->SetGraph(InGraph);

	return GraphInstance;
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemEditorData.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraOverviewNodeStackItem.h"
#include "EdGraphSchema_NiagaraSystemOverview.h"

static const float SystemOverviewNodePadding = 300.0f;

const FName UNiagaraSystemEditorFolder::GetFolderName() const
{
	return FolderName;
}

void UNiagaraSystemEditorFolder::SetFolderName(FName InFolderName)
{
	FolderName = InFolderName;
}

const TArray<UNiagaraSystemEditorFolder*>& UNiagaraSystemEditorFolder::GetChildFolders() const
{
	return ChildFolders;
}

void UNiagaraSystemEditorFolder::AddChildFolder(UNiagaraSystemEditorFolder* ChildFolder)
{
	Modify();
	ChildFolders.Add(ChildFolder);
}

void UNiagaraSystemEditorFolder::RemoveChildFolder(UNiagaraSystemEditorFolder* ChildFolder)
{
	Modify();
	ChildFolders.Remove(ChildFolder);
}

const TArray<FGuid>& UNiagaraSystemEditorFolder::GetChildEmitterHandleIds() const
{
	return ChildEmitterHandleIds;
}

void UNiagaraSystemEditorFolder::AddChildEmitterHandleId(FGuid ChildEmitterHandleId)
{
	Modify();
	ChildEmitterHandleIds.Add(ChildEmitterHandleId);
}

void UNiagaraSystemEditorFolder::RemoveChildEmitterHandleId(FGuid ChildEmitterHandleId)
{
	Modify();
	ChildEmitterHandleIds.Remove(ChildEmitterHandleId);
}

UNiagaraSystemEditorData::UNiagaraSystemEditorData(const FObjectInitializer& ObjectInitializer)
{
	RootFolder = ObjectInitializer.CreateDefaultSubobject<UNiagaraSystemEditorFolder>(this, TEXT("RootFolder"));
	StackEditorData = ObjectInitializer.CreateDefaultSubobject<UNiagaraStackEditorData>(this, TEXT("StackEditorData"));
	OwnerTransform.SetLocation(FVector(0.0f, 0.0f, 0.0f));
	PlaybackRangeMin = 0;
	PlaybackRangeMax = 10;
}

void UNiagaraSystemEditorData::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		SystemOverviewGraph = NewObject<UEdGraph>(this, "NiagaraOverview", RF_Transactional);
		SystemOverviewGraph->Schema = UEdGraphSchema_NiagaraSystemOverview::StaticClass();
	}
}

void UNiagaraSystemEditorData::PostLoadFromOwner(UObject* InOwner)
{
	if (RootFolder == nullptr)
	{
		RootFolder = NewObject<UNiagaraSystemEditorFolder>(this, TEXT("RootFolder"), RF_Transactional);
	}
	if (StackEditorData == nullptr)
	{
		StackEditorData = NewObject<UNiagaraStackEditorData>(this, TEXT("StackEditorData"), RF_Transactional);
	}

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

	if (NiagaraVer < FNiagaraCustomVersion::PlaybackRangeStoredOnSystem)
	{
		UpdatePlaybackRangeFromEmitters(OwningSystem);
	}

	if (SystemOverviewGraph == nullptr)
	{
		SystemOverviewGraph = NewObject<UEdGraph>(this, "NiagaraOverview", RF_Transactional);
		SystemOverviewGraph->Schema = UEdGraphSchema_NiagaraSystemOverview::StaticClass();
	}
}

UNiagaraSystemEditorFolder& UNiagaraSystemEditorData::GetRootFolder() const
{
	return *RootFolder;
}

UNiagaraStackEditorData& UNiagaraSystemEditorData::GetStackEditorData() const
{
	return *StackEditorData;
}

TRange<float> UNiagaraSystemEditorData::GetPlaybackRange() const
{
	return TRange<float>(PlaybackRangeMin, PlaybackRangeMax);
}

void UNiagaraSystemEditorData::SetPlaybackRange(TRange<float> InPlaybackRange)
{
	PlaybackRangeMin = InPlaybackRange.GetLowerBoundValue();
	PlaybackRangeMax = InPlaybackRange.GetUpperBoundValue();
}

UEdGraph* UNiagaraSystemEditorData::GetSystemOverviewGraph() const
{
	return SystemOverviewGraph;	
}

void UNiagaraSystemEditorData::UpdatePlaybackRangeFromEmitters(UNiagaraSystem* OwnerSystem)
{
	if (OwnerSystem->GetEmitterHandles().Num() > 0)
	{
		float EmitterPlaybackRangeMin = TNumericLimits<float>::Max();
		float EmitterPlaybackRangeMax = TNumericLimits<float>::Lowest();

		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
		{
			UNiagaraEmitterEditorData* EmitterEditorData = Cast<UNiagaraEmitterEditorData>(EmitterHandle.GetInstance()->EditorData);
			if (EmitterEditorData != nullptr)
			{
				EmitterPlaybackRangeMin = FMath::Min(PlaybackRangeMin, EmitterEditorData->GetPlaybackRange().GetLowerBoundValue());
				EmitterPlaybackRangeMax = FMath::Max(PlaybackRangeMax, EmitterEditorData->GetPlaybackRange().GetUpperBoundValue());
			}
		}

		PlaybackRangeMin = EmitterPlaybackRangeMin;
		PlaybackRangeMax = EmitterPlaybackRangeMax;
	}
}

void UNiagaraSystemEditorData::InitSystemOverviewGraph()
{
	float NextNodePosX = 0.0;

	FGraphNodeCreator<UNiagaraOverviewNodeStackItem> SystemOverviewNodeCreator(*SystemOverviewGraph);
	UNiagaraOverviewNodeStackItem* SystemOverviewNode = SystemOverviewNodeCreator.CreateNode(false);
	SystemOverviewNode->Initialize(OwningSystem);

	SystemOverviewNode->NodePosX = NextNodePosX;
	SystemOverviewNode->NodePosY = 0.0;
	NextNodePosX += SystemOverviewNodePadding;

	SystemOverviewNodeCreator.Finalize();

	for (const FNiagaraEmitterHandle& Handle : OwningSystem->GetEmitterHandles())
	{
		FGraphNodeCreator<UNiagaraOverviewNodeStackItem> EmitterOverviewNodeCreator(*SystemOverviewGraph);
		UNiagaraOverviewNodeStackItem* EmitterOverviewNode = EmitterOverviewNodeCreator.CreateNode(false);
		EmitterOverviewNode->Initialize(OwningSystem, Handle.GetId());

		EmitterOverviewNode->NodePosX = NextNodePosX;
		EmitterOverviewNode->NodePosY = 0.0;
		NextNodePosX += SystemOverviewNodePadding;

		EmitterOverviewNodeCreator.Finalize();
	}
}

const FVector2D UNiagaraSystemEditorData::GetGoodPlaceForNewOverviewNode() const
{
	TArray<UNiagaraOverviewNodeStackItem*> OverviewNodes;
	SystemOverviewGraph->GetNodesOfClass<UNiagaraOverviewNodeStackItem>(OverviewNodes);
	const TArray<FNiagaraEmitterHandle>& EmitterHandles = OwningSystem->GetEmitterHandles();

	// Find the last created Emitter overview node location and place next to that. 
	if (EmitterHandles.Num() > 1)
	{
		const FGuid PreviousEmitterHandleGuid = EmitterHandles.Last(1).GetId();
		for (const UNiagaraOverviewNodeStackItem* PerOverviewNode : OverviewNodes)
		{
			if (PreviousEmitterHandleGuid == PerOverviewNode->GetEmitterHandleGuid())
			{
				return FVector2D(PerOverviewNode->NodePosX + SystemOverviewNodePadding, PerOverviewNode->NodePosY);
			}
		}
	}
	return SystemOverviewGraph->GetGoodPlaceForNewNode();
}

bool UNiagaraSystemEditorData::GetSystemOverviewGraphIsValid() const
{
	TArray<UEdGraphNode*> Nodes;
	SystemOverviewGraph->GetNodesOfClass(Nodes);
	if (Nodes.Num() > 0)
	{
		return true;
	}
	return false;
}

void UNiagaraSystemEditorData::Initialize(UNiagaraSystem* OwnerSystem, bool bEditingSystem)
{
	OwningSystem = OwnerSystem;

	if (bEditingSystem && GetSystemOverviewGraphIsValid() == false)
	{
		InitSystemOverviewGraph();
	}
}

void UNiagaraSystemEditorData::SystemOverviewHandleAdded(const FGuid AddedHandleGuid) const
{
	SystemOverviewGraph->Modify();

	const FVector2D NewNodeLocation = GetGoodPlaceForNewOverviewNode();

	FGraphNodeCreator<UNiagaraOverviewNodeStackItem> EmitterOverviewNodeCreator(*SystemOverviewGraph);
	UNiagaraOverviewNodeStackItem* EmitterOverviewNode = EmitterOverviewNodeCreator.CreateNode(false);
	EmitterOverviewNode->Initialize(OwningSystem, AddedHandleGuid);

	EmitterOverviewNode->NodePosX = NewNodeLocation.X;
	EmitterOverviewNode->NodePosY = NewNodeLocation.Y;

	EmitterOverviewNodeCreator.Finalize();

	//@TODO System Overview: if we have a ref to the SGraphEditor here, then focus the new node
}

void UNiagaraSystemEditorData::SystemOverviewHandlesRemoved() const
{
	TArray<UNiagaraOverviewNodeStackItem*> CurrentOverviewNodes;
	SystemOverviewGraph->GetNodesOfClass<UNiagaraOverviewNodeStackItem>(CurrentOverviewNodes);
	for (UNiagaraOverviewNodeStackItem* OverviewNode : CurrentOverviewNodes)
	{
		const FGuid OverviewNodeGuid = OverviewNode->GetEmitterHandleGuid();
		// If the OverviewNode's EmitterHandleGuid is valid (not representing a system) and is not in the current array of the owning System's EmitterHandles, that node can be removed.
		if (OverviewNodeGuid.IsValid() && false == OwningSystem->GetEmitterHandles().ContainsByPredicate([&OverviewNodeGuid](const FNiagaraEmitterHandle& Handle) {return Handle.GetId() == OverviewNodeGuid; }))
		{
			SystemOverviewGraph->RemoveNode(OverviewNode);
		}
	}
}

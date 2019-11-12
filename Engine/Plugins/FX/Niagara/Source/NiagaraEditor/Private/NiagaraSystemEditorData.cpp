// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemEditorData.h"
#include "NiagaraStackEditorData.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraOverviewNode.h"
#include "EdGraphSchema_NiagaraSystemOverview.h"
#include "EdGraph/EdGraph.h"

static const float SystemOverviewNodePadding = 250.0f;

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
	bSystemIsPlaceholder = false;
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
	UNiagaraSystem* OwningSystem = CastChecked<UNiagaraSystem>(InOwner, ECastCheckedType::NullChecked);

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
		UpdatePlaybackRangeFromEmitters(*OwningSystem);
	}

	if (SystemOverviewGraph == nullptr)
	{
		SystemOverviewGraph = NewObject<UEdGraph>(this, "NiagaraOverview", RF_Transactional);
		SystemOverviewGraph->Schema = UEdGraphSchema_NiagaraSystemOverview::StaticClass();
	}

	if(SystemOverviewGraph->Nodes.Num() == 0)
	{
		SynchronizeOverviewGraphWithSystem(*OwningSystem);
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

const FNiagaraGraphViewSettings& UNiagaraSystemEditorData::GetSystemOverviewGraphViewSettings() const
{
	return OverviewGraphViewSettings;
}

void UNiagaraSystemEditorData::SetSystemOverviewGraphViewSettings(const FNiagaraGraphViewSettings& InOverviewGraphViewSettings)
{
	OverviewGraphViewSettings = InOverviewGraphViewSettings;
}

bool UNiagaraSystemEditorData::GetOwningSystemIsPlaceholder() const
{
	return bSystemIsPlaceholder;
}

void UNiagaraSystemEditorData::SetOwningSystemIsPlaceholder(bool bInSystemIsPlaceholder, UNiagaraSystem& OwnerSystem)
{
	bSystemIsPlaceholder = bInSystemIsPlaceholder;
	SynchronizeOverviewGraphWithSystem(OwnerSystem);
}

void UNiagaraSystemEditorData::UpdatePlaybackRangeFromEmitters(UNiagaraSystem& OwnerSystem)
{
	if (OwnerSystem.GetEmitterHandles().Num() > 0)
	{
		float EmitterPlaybackRangeMin = TNumericLimits<float>::Max();
		float EmitterPlaybackRangeMax = TNumericLimits<float>::Lowest();

		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem.GetEmitterHandles())
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

void FindOuterNodes(const TArray<UNiagaraOverviewNode*> OverviewNodes, UNiagaraOverviewNode*& OutLeft, UNiagaraOverviewNode*& OutRight)
{
	if (OverviewNodes.Num() > 0)
	{
		OutLeft = OverviewNodes[0];
		OutRight = OutLeft;
		for (int32 i = 1; i < OverviewNodes.Num(); i++)
		{
			UNiagaraOverviewNode* Current = OverviewNodes[i];
			if (Current->NodePosX < OutLeft->NodePosX)
			{
				OutLeft = Current;
			}
			if (Current->NodePosX > OutRight->NodePosX)
			{
				OutRight = Current;
			}
		}
	}
	else
	{
		OutLeft = nullptr;
		OutRight = nullptr;
	}
}

void UNiagaraSystemEditorData::SynchronizeOverviewGraphWithSystem(UNiagaraSystem& OwnerSystem)
{
	float NextNodePosX = 0.0;

	TArray<UNiagaraOverviewNode*> OverviewNodes;
	SystemOverviewGraph->GetNodesOfClass<UNiagaraOverviewNode>(OverviewNodes);

	UNiagaraOverviewNode* LeftNode;
	UNiagaraOverviewNode* RightNode;
	FindOuterNodes(OverviewNodes, LeftNode, RightNode);

	// Validate system node.
	int32 SystemNodeIndex = OverviewNodes.IndexOfByPredicate([] (UNiagaraOverviewNode* OverviewNode) { return OverviewNode->GetEmitterHandleGuid().IsValid() == false; });
	if (SystemNodeIndex != INDEX_NONE)
	{
		if (bSystemIsPlaceholder)
		{
			// Systems which are placeholder are for editing emitter assets and shouldn't have system nodes in their overview graph.
			UNiagaraOverviewNode* SystemNode = OverviewNodes[SystemNodeIndex];
			SystemNode->DestroyNode();
		}

		// Remove the node from the list to track which nodes are no longer used.
		OverviewNodes.RemoveAt(SystemNodeIndex);
	}
	else
	{
		if(bSystemIsPlaceholder == false)
		{
			SystemOverviewGraph->Modify();
			FGraphNodeCreator<UNiagaraOverviewNode> SystemOverviewNodeCreator(*SystemOverviewGraph);
			UNiagaraOverviewNode* SystemOverviewNode = SystemOverviewNodeCreator.CreateNode(false);
			SystemOverviewNode->Initialize(&OwnerSystem);

			FVector2D SystemOverviewNodeLocation = LeftNode != nullptr
				? FVector2D(LeftNode->NodePosX - SystemOverviewNodePadding, LeftNode->NodePosY)
				: SystemOverviewGraph->GetGoodPlaceForNewNode();
			SystemOverviewNode->NodePosX = SystemOverviewNodeLocation.X;
			SystemOverviewNode->NodePosY = SystemOverviewNodeLocation.Y;

			SystemOverviewNodeCreator.Finalize();

			if (RightNode == nullptr)
			{
				RightNode = SystemOverviewNode;
			}
		}
	}

	// Validate emitter nodes.
	for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem.GetEmitterHandles())
	{
		int32 EmitterNodeIndex = OverviewNodes.IndexOfByPredicate([&EmitterHandle] (UNiagaraOverviewNode* OverviewNode) { return OverviewNode->GetEmitterHandleGuid() == EmitterHandle.GetId(); });
		UNiagaraOverviewNode* EmitterNode = nullptr;
		if (EmitterNodeIndex != INDEX_NONE)
		{
			// If a node for this emitter exists already, remove it from the collection so that we can track nodes for emitters which no longer exist.
			EmitterNode = OverviewNodes[EmitterNodeIndex];
			OverviewNodes.RemoveAt(EmitterNodeIndex);
		}
		else
		{
			SystemOverviewGraph->Modify();
			FGraphNodeCreator<UNiagaraOverviewNode> EmitterOverviewNodeCreator(*SystemOverviewGraph);
			UNiagaraOverviewNode* EmitterOverviewNode = EmitterOverviewNodeCreator.CreateNode(false);
			EmitterOverviewNode->Initialize(&OwnerSystem, EmitterHandle.GetId());

			FVector2D EmitterOverviewNodeLocation = RightNode != nullptr
				? FVector2D(RightNode->NodePosX + SystemOverviewNodePadding, RightNode->NodePosY)
				: SystemOverviewGraph->GetGoodPlaceForNewNode();

			checkf(EmitterOverviewNode != nullptr, TEXT("Emitter overview node creation failed!"));
			EmitterOverviewNode->NodePosX = EmitterOverviewNodeLocation.X;
			EmitterOverviewNode->NodePosY = EmitterOverviewNodeLocation.Y;

			EmitterOverviewNodeCreator.Finalize();

			RightNode = EmitterOverviewNode;
			EmitterNode = EmitterOverviewNode;
		}
		EmitterNode->Modify();
		EmitterNode->SetEnabledState(EmitterHandle.GetIsEnabled() ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled);
	}

	// If there are any nodes remaining in the list they're no longer being used so destroy them.
	for (UNiagaraOverviewNode* OverviewNode : OverviewNodes)
	{
		OverviewNode->Modify();
		OverviewNode->DestroyNode();
	}

	// Dispatch an empty graph changed message here so that any graph UIs which are visible refresh their node's widgets.
	SystemOverviewGraph->NotifyGraphChanged();
}

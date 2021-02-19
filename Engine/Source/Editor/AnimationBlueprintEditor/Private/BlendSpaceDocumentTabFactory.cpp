// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlendSpaceDocumentTabFactory.h"
#include "EditorStyleSet.h"
#include "AnimationBlueprintEditor.h"
#include "PersonaModule.h"
#include "BlendSpaceGraph.h"
#include "AnimGraphNode_BlendSpaceGraph.h"
#include "AnimationGraph.h"
#include "Animation/BlendSpace1D.h"
#include "Widgets/SBoxPanel.h"
#include "TabPayload_BlendSpaceGraph.h"
#include "AnimationBlendSpaceSampleGraph.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SGraphPreviewer.h"
#include "AnimNodes/AnimNode_BlendSpaceGraphBase.h"
#include "Widgets/Docking/SDockTab.h"

static const FName BlendSpaceEditorID("BlendSpaceEditor");

#define LOCTEXT_NAMESPACE "FBlendSpaceDocumentTabFactory"

// Simple wrapper widget used to hold a reference to the graph document
class SBlendSpaceDocumentTab : public SCompoundWidget
{	
	SLATE_BEGIN_ARGS(SBlendSpaceDocumentTab) {}

	SLATE_DEFAULT_SLOT(FArguments, Content)
	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UBlendSpaceGraph* InDocument)
	{
		Document = InDocument;
		
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	TWeakObjectPtr<UBlendSpaceGraph> Document;
};

FBlendSpaceDocumentTabFactory::FBlendSpaceDocumentTabFactory(TSharedPtr<FAnimationBlueprintEditor> InBlueprintEditorPtr)
	: FDocumentTabFactory(BlendSpaceEditorID, InBlueprintEditorPtr)
	, BlueprintEditorPtr(InBlueprintEditorPtr)
{
}

TSharedRef<SWidget> FBlendSpaceDocumentTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	UBlendSpaceGraph* DocumentID = FTabPayload_BlendSpaceGraph::GetBlendSpaceGraph(Info.Payload);

	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

	UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = CastChecked<UAnimGraphNode_BlendSpaceGraphBase>(DocumentID->GetOuter());

	FBlendSpaceEditorArgs Args;

	Args.OnBlendSpaceSampleDoubleClicked = FOnBlendSpaceSampleDoubleClicked::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex)
	{
		if(BlueprintEditorPtr.IsValid() && WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			if(BlendSpaceNode->GetGraphs().IsValidIndex(InSampleIndex))
			{
				BlueprintEditorPtr.Pin()->JumpToHyperlink(BlendSpaceNode->GetGraphs()[InSampleIndex], false);
			}
		}
	});

	Args.OnBlendSpaceSampleAdded = FOnBlendSpaceSampleAdded::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](UAnimSequence* InSequence, const FVector& InSamplePoint)
	{
		if(WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			UAnimationBlendSpaceSampleGraph* NewGraph = BlendSpaceNode->AddGraph(TEXT("NewSample"), InSequence);
			BlueprintEditorPtr.Pin()->RefreshMyBlueprint();
			BlueprintEditorPtr.Pin()->RenameNewlyAddedAction(NewGraph->GetFName());
		}
	});

	Args.OnBlendSpaceSampleRemoved = FOnBlendSpaceSampleRemoved::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex)
	{
		if(WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			BlendSpaceNode->RemoveGraph(InSampleIndex);
			BlueprintEditorPtr.Pin()->RefreshMyBlueprint();
		}
	});

	Args.OnBlendSpaceSampleReplaced = FOnBlendSpaceSampleReplaced::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex, UAnimSequence* InSequence)
	{
		if(WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			BlendSpaceNode->ReplaceGraph(InSampleIndex, InSequence);
			BlueprintEditorPtr.Pin()->RefreshMyBlueprint();
		}
	});

	Args.OnGetBlendSpaceSampleName = FOnGetBlendSpaceSampleName::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex) -> FName
	{
		if(WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			return BlendSpaceNode->GetGraphs()[InSampleIndex]->GetFName();
		}

		return NAME_None;
	});

	Args.OnExtendSampleTooltip = FOnExtendBlendSpaceSampleTooltip::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex) -> TSharedRef<SWidget>
	{
		if(WeakBlendSpaceNode.Get())
		{
			UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
			if(BlendSpaceNode->GetGraphs().IsValidIndex(InSampleIndex))
			{
				return SNew(SGraphPreviewer, BlendSpaceNode->GetGraphs()[InSampleIndex])
					.CornerOverlayText(LOCTEXT("SampleGraphOverlay", "ANIMATION"))
					.ShowGraphStateOverlay(false);
			}
		}

		return SNullWidget::NullWidget;
	});

	Args.PreviewPosition = MakeAttributeLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)]()
	{
		if(WeakBlendSpaceNode.Get())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(WeakBlendSpaceNode.Get()))
			{
				if (UObject* ActiveObject = Blueprint->GetObjectBeingDebugged())
				{
					if (UAnimBlueprintGeneratedClass* Class = Cast<UAnimBlueprintGeneratedClass>(ActiveObject->GetClass()))
					{
						if(int32* NodeIndexPtr = Class->GetAnimBlueprintDebugData().NodePropertyToIndexMap.Find(WeakBlendSpaceNode))
						{
							int32 AnimNodeIndex = *NodeIndexPtr;
							// reverse node index temporarily because of a bug in NodeGuidToIndexMap
							AnimNodeIndex = Class->GetAnimNodeProperties().Num() - AnimNodeIndex - 1;

							if (FAnimBlueprintDebugData::FBlendSpacePlayerRecord* DebugInfo = Class->GetAnimBlueprintDebugData().BlendSpacePlayerRecordsThisFrame.FindByPredicate(
								[AnimNodeIndex](const FAnimBlueprintDebugData::FBlendSpacePlayerRecord& InRecord){ return InRecord.NodeID == AnimNodeIndex; }))
							{
								return DebugInfo->Position;
							}
						}
					}
				}
			}
		}

		return FVector::ZeroVector;
	});

	Args.PreviewFilteredPosition = MakeAttributeLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)]()
	{
		if (WeakBlendSpaceNode.Get())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(WeakBlendSpaceNode.Get()))
			{
				if (UObject* ActiveObject = Blueprint->GetObjectBeingDebugged())
				{
					if (UAnimBlueprintGeneratedClass* Class = Cast<UAnimBlueprintGeneratedClass>(ActiveObject->GetClass()))
					{
						if (int32* NodeIndexPtr = Class->GetAnimBlueprintDebugData().NodePropertyToIndexMap.Find(WeakBlendSpaceNode))
						{
							int32 AnimNodeIndex = *NodeIndexPtr;
							// reverse node index temporarily because of a bug in NodeGuidToIndexMap
							AnimNodeIndex = Class->GetAnimNodeProperties().Num() - AnimNodeIndex - 1;

							if (FAnimBlueprintDebugData::FBlendSpacePlayerRecord* DebugInfo = Class->GetAnimBlueprintDebugData().BlendSpacePlayerRecordsThisFrame.FindByPredicate(
								[AnimNodeIndex](const FAnimBlueprintDebugData::FBlendSpacePlayerRecord& InRecord) { return InRecord.NodeID == AnimNodeIndex; }))
							{
								return DebugInfo->FilteredPosition;
							}
						}
					}
				}
			}
		}

		return FVector::ZeroVector;
	});

	Args.OnSetPreviewPosition = FOnSetBlendSpacePreviewPosition::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](FVector InPreviewPosition)
	{
		if(WeakBlendSpaceNode.Get())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(WeakBlendSpaceNode.Get()))
			{
				if (UObject* ActiveObject = Blueprint->GetObjectBeingDebugged())
				{
					if (UAnimBlueprintGeneratedClass* Class = Cast<UAnimBlueprintGeneratedClass>(ActiveObject->GetClass()))
					{
						if(FAnimNode_BlendSpaceGraphBase* BlendSpaceGraphNode = Class->GetPropertyInstance<FAnimNode_BlendSpaceGraphBase>(ActiveObject, WeakBlendSpaceNode.Get()))
						{
							BlendSpaceGraphNode->SetPreviewPosition(InPreviewPosition);
						}
					}
				}
			}
		}
	});

	Args.StatusBarName = TEXT("AssetEditor.AnimationBlueprintEditor.MainMenu");

	return
		SNew(SBlendSpaceDocumentTab, DocumentID)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				BlueprintEditorPtr.Pin()->CreateGraphTitleBarWidget(Info.TabInfo.ToSharedRef(), DocumentID)
			]
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				PersonaModule.CreateBlendSpaceEditWidget(DocumentID->BlendSpace, Args)
			]
		];
}

const FSlateBrush* FBlendSpaceDocumentTabFactory::GetTabIcon(const FWorkflowTabSpawnInfo& Info) const
{
	UBlendSpaceGraph* DocumentID = FTabPayload_BlendSpaceGraph::GetBlendSpaceGraph(Info.Payload);

	if (UBlendSpace1D* BlendSpace1D = Cast<UBlendSpace1D>(DocumentID->BlendSpace))
	{
		return FEditorStyle::GetBrush("ClassIcon.BlendSpace1D");
	}
	else
	{
		return FEditorStyle::GetBrush("ClassIcon.BlendSpace");
	}
}

bool FBlendSpaceDocumentTabFactory::IsPayloadSupported(TSharedRef<FTabPayload> Payload) const
{
	return (Payload->PayloadType == UBlendSpaceGraph::StaticClass()->GetFName() && Payload->IsValid());
}

bool FBlendSpaceDocumentTabFactory::IsPayloadValid(TSharedRef<FTabPayload> Payload) const
{
	if (Payload->PayloadType == UBlendSpaceGraph::StaticClass()->GetFName())
	{
		return Payload->IsValid();
	}
	return false;
}

TAttribute<FText> FBlendSpaceDocumentTabFactory::ConstructTabName(const FWorkflowTabSpawnInfo& Info) const
{
	check(Info.Payload.IsValid());

	UBlendSpaceGraph* DocumentID = FTabPayload_BlendSpaceGraph::GetBlendSpaceGraph(Info.Payload);

	return MakeAttributeLambda([WeakBlendSpace = TWeakObjectPtr<UBlendSpace>(DocumentID->BlendSpace)]()
		{ 
			return WeakBlendSpace.Get() ? FText::FromName(WeakBlendSpace->GetFName()) : FText::GetEmpty();
		});
}

void FBlendSpaceDocumentTabFactory::OnTabActivated(TSharedPtr<SDockTab> Tab) const
{
	TSharedRef<SBlendSpaceDocumentTab> DocumentWidget = StaticCastSharedRef<SBlendSpaceDocumentTab>(Tab->GetContent());
	if(UBlendSpaceGraph* Document = DocumentWidget->Document.Get())
	{
		if(TSharedPtr<FAnimationBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
		{
			BlueprintEditor->SetDetailObject(Document);
		}
	}
}

void FBlendSpaceDocumentTabFactory::OnTabForegrounded(TSharedPtr<SDockTab> Tab) const
{
	TSharedRef<SBlendSpaceDocumentTab> DocumentWidget = StaticCastSharedRef<SBlendSpaceDocumentTab>(Tab->GetContent());
	if(UBlendSpaceGraph* Document = DocumentWidget->Document.Get())
	{
		if(TSharedPtr<FAnimationBlueprintEditor> BlueprintEditor = BlueprintEditorPtr.Pin())
		{
			BlueprintEditor->SetDetailObject(Document);
		}
	}
}

#undef LOCTEXT_NAMESPACE
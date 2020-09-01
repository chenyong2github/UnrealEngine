// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationNodes/SGraphNodeBlendSpacePlayer.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimGraphNode_BlendSpaceBase.h"
#include "PersonaModule.h"
#include "Widgets/Layout/SBox.h"
#include "Modules/ModuleManager.h"

PRAGMA_DISABLE_OPTIMIZATION

void SGraphNodeBlendSpacePlayer::Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode)
{
	this->GraphNode = InNode;

	this->SetCursor(EMouseCursor::CardinalCross);

	this->UpdateGraphNode();

	SAnimationGraphNode::Construct(SAnimationGraphNode::FArguments(), InNode);

	RegisterActiveTimer(1.0f / 60.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
	{
		GetBlendSpaceInfo(CachedBlendSpace, CachedPosition);
		return EActiveTimerReturnType::Continue;
	}));
}

void SGraphNodeBlendSpacePlayer::CreateBelowWidgetControls(TSharedPtr<SVerticalBox> MainBox)
{
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

	MainBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Fill)
	.Padding(0.0f)
	[
		SNew(SBox)
		.HeightOverride_Lambda([WeakMainBox = TWeakPtr<SVerticalBox>(MainBox)]()
		{
			if(TSharedPtr<SVerticalBox> LocalMainBox = WeakMainBox.Pin())
			{
				float Size = LocalMainBox->GetDesiredSize().X;
				return FMath::Min(Size, 100.0f);
			}

			return 0.0f;
		})
		.Visibility(this, &SGraphNodeBlendSpacePlayer::GetBlendSpaceVisibility)
		[
			PersonaModule.CreateBlendSpacePreviewWidget(
			MakeAttributeLambda([this]()
			{
				return CachedBlendSpace.Get();
			}),
			MakeAttributeLambda([this]()
			{
				return CachedPosition;
			}))
		]
	];
}

EVisibility SGraphNodeBlendSpacePlayer::GetBlendSpaceVisibility() const
{
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(GraphNode))
	{
		if (FProperty* Property = FKismetDebugUtilities::FindClassPropertyForNode(Blueprint, GraphNode))
		{
			if (UObject* ActiveObject = Blueprint->GetObjectBeingDebugged())
			{
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

bool SGraphNodeBlendSpacePlayer::GetBlendSpaceInfo(TWeakObjectPtr<const UBlendSpaceBase>& OutBlendSpace, FVector& OutPosition) const
{
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(GraphNode))
	{
		if (UObject* ActiveObject = Blueprint->GetObjectBeingDebugged())
		{
			if (UAnimGraphNode_BlendSpaceBase* VisualBlendSpacePlayer = Cast<UAnimGraphNode_BlendSpaceBase>(GraphNode))
			{
				if (UAnimBlueprintGeneratedClass* Class = Cast<UAnimBlueprintGeneratedClass>((UObject*)ActiveObject->GetClass()))
				{
					if(int32* NodeIndexPtr = Class->GetAnimBlueprintDebugData().NodePropertyToIndexMap.Find(TWeakObjectPtr<UAnimGraphNode_Base>(Cast<UAnimGraphNode_Base>(GraphNode))))
					{
						int32 AnimNodeIndex = *NodeIndexPtr;
						// reverse node index temporarily because of a bug in NodeGuidToIndexMap
						AnimNodeIndex = Class->GetAnimNodeProperties().Num() - AnimNodeIndex - 1;

						if (FAnimBlueprintDebugData::FBlendSpacePlayerRecord* DebugInfo = Class->GetAnimBlueprintDebugData().BlendSpacePlayerRecordsThisFrame.FindByPredicate([AnimNodeIndex](const FAnimBlueprintDebugData::FBlendSpacePlayerRecord& InRecord){ return InRecord.NodeID == AnimNodeIndex; }))
						{
							OutBlendSpace = DebugInfo->BlendSpace.Get();
							OutPosition = FVector(DebugInfo->PositionX, DebugInfo->PositionY, DebugInfo->PositionZ);
							return true;
						}
					}
				}
			}
		}
	}

	OutBlendSpace = nullptr;
	OutPosition = FVector::ZeroVector;
	return false;
}

PRAGMA_ENABLE_OPTIMIZATION
// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundSubmixGraph/SoundSubmixGraphNode.h"
#include "Sound/SoundSubmix.h"
#include "SoundSubmixGraph/SoundSubmixGraphSchema.h"
#include "SoundSubmixGraph/SoundSubmixGraph.h"
#include "Toolkits/AssetEditorManager.h"
#include "SoundSubmixEditor.h"

#define LOCTEXT_NAMESPACE "SoundSubmixGraphNode"

USoundSubmixGraphNode::USoundSubmixGraphNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ChildPin(NULL)
	, ParentPin(NULL)
{
}

bool USoundSubmixGraphNode::CheckRepresentsSoundSubmix()
{
	if (!SoundSubmix)
	{
		return false;
	}

	for (int32 ChildIndex = 0; ChildIndex < ChildPin->LinkedTo.Num(); ChildIndex++)
	{
		USoundSubmixGraphNode* ChildNode = CastChecked<USoundSubmixGraphNode>(ChildPin->LinkedTo[ChildIndex]->GetOwningNode());
		if (!SoundSubmix->ChildSubmixes.Contains(ChildNode->SoundSubmix))
		{
			return false;
		}
	}

	for (int32 ChildIndex = 0; ChildIndex < SoundSubmix->ChildSubmixes.Num(); ChildIndex++)
	{
		bool bFoundChild = false;
		for (int32 NodeChildIndex = 0; NodeChildIndex < ChildPin->LinkedTo.Num(); NodeChildIndex++)
		{
			USoundSubmixGraphNode* ChildNode = CastChecked<USoundSubmixGraphNode>(ChildPin->LinkedTo[NodeChildIndex]->GetOwningNode());
			if (ChildNode->SoundSubmix == SoundSubmix->ChildSubmixes[ChildIndex])
			{
				bFoundChild = true;
				break;
			}
		}

		if (!bFoundChild)
		{
			return false;
		}
	}

	return true;
}

FLinearColor USoundSubmixGraphNode::GetNodeTitleColor() const
{
	return Super::GetNodeTitleColor();
}

void USoundSubmixGraphNode::AllocateDefaultPins()
{
	check(Pins.Num() == 0);

	ChildPin = CreatePin(EGPD_Input, TEXT("SoundSubmix"), *LOCTEXT("SoundSubmixGraphNode_Input", "Input").ToString());
	ParentPin = CreatePin(EGPD_Output, TEXT("SoundSubmix"), *LOCTEXT("SoundSubmixGraphNode_Output", "Output").ToString());
}

void USoundSubmixGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin)
	{
		const USoundSubmixGraphSchema* Schema = CastChecked<USoundSubmixGraphSchema>(GetSchema());

		if (FromPin->Direction == EGPD_Input)
		{
			Schema->TryCreateConnection(FromPin, ChildPin);
		}
		else
		{
			Schema->TryCreateConnection(FromPin, ParentPin);
		}
	}
}

bool USoundSubmixGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(USoundSubmixGraphSchema::StaticClass());
}

bool USoundSubmixGraphNode::CanUserDeleteNode() const
{
	check(GEditor);
	UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	TArray<IAssetEditorInstance*> SubmixEditors = EditorSubsystem->FindEditorsForAsset(SoundSubmix);
	for (IAssetEditorInstance* Editor : SubmixEditors)
	{
		if (!Editor)
		{
			continue;
		}

		FSoundSubmixEditor* SubmixEditor = static_cast<FSoundSubmixEditor*>(Editor);
		if (UEdGraph* Graph = SubmixEditor->GetGraph())
		{
			if (SoundSubmix->SoundSubmixGraph == Graph)
			{
				USoundSubmix* RootSubmix = CastChecked<USoundSubmixGraph>(Graph)->GetRootSoundSubmix();
				if (RootSubmix == SoundSubmix)
				{
					return false;
				}
			}
		}
	}

	return UEdGraphNode::CanUserDeleteNode();
}

FText USoundSubmixGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (SoundSubmix)
	{
		return FText::FromString(SoundSubmix->GetName());
	}
	else
	{
		return Super::GetNodeTitle(TitleType);
	}
}
#undef LOCTEXT_NAMESPACE

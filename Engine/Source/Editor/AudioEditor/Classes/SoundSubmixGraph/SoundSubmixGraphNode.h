// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AudioDevice.h"
#include "Audio/AudioWidgetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNode.h"
#include "SGraphNode.h"
#include "Sound/SoundSubmix.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/MultithreadedPatching.h"
#include "SoundSubmixGraphNode.generated.h"


// Forward Declarations
class USoundSubmixBase;




class SSubmixGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SSubmixGraphNode) {}

	SLATE_ARGUMENT(TWeakObjectPtr<USoundSubmixBase>, SubmixBase)
	SLATE_ARGUMENT(TWeakObjectPtr<UUserWidget>, SubmixNodeUserWidget)

	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode);

	TSharedRef<SWidget> CreateNodeContentArea() override;

private:
	TWeakObjectPtr<USoundSubmixBase> SubmixBase;
	TWeakObjectPtr<UUserWidget> SubmixNodeUserWidget;
};

UCLASS(MinimalAPI)
class USoundSubmixGraphNode : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	/** The SoundSubmix this represents */
	UPROPERTY(VisibleAnywhere, instanced, Category=Sound)
	TObjectPtr<USoundSubmixBase> SoundSubmix;

	/** A user widget to use to represent the graph node */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SubmixNodeUserWidget;

	/** Get the Pin that connects to all children */
	UEdGraphPin* GetChildPin() const { return ChildPin; }

	/** Get the Pin that connects to its parent */
	UEdGraphPin* GetParentPin() const { return ParentPin; }
	
	/** Check whether the children of this node match the SoundSubmix it is representing */
	bool CheckRepresentsSoundSubmix();

	//~ Begin UEdGraphNode Interface.
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void AllocateDefaultPins() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	virtual bool CanUserDeleteNode() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget();
	//~ End UEdGraphNode Interface.

private:
	/** Pin that connects to all children */
	UEdGraphPin* ChildPin;
	
	/** Pin that connects to its parent */
	UEdGraphPin* ParentPin;
};

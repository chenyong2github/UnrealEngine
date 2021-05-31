// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "AnimNodes/AnimNode_IKRig.h"
#include "AnimGraphNode_IKRig.generated.h"

class FPrimitiveDrawInterface;
class USkeletalMeshComponent;

// Editor node for IKRig 
UCLASS()
class IKRIGDEVELOPER_API UAnimGraphNode_IKRig : public UAnimGraphNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_IKRig Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
	// End of UEdGraphNode interface

	// UAnimGraphNode_Base interface
	virtual void PreloadRequiredAssets() override;
	virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* AnimNode) override;
	virtual void Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const override;
	virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const override;
	// End of UAnimGraphNode_Base interface
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeModifierBase.h"

#include "CustomizableObjectNodeMeshClipDeform.generated.h"

UENUM()
enum class EShapeBindingMethod : uint32
{
	ClosestProject = 0,
	ClosestToSurface = 1,
	NormalProject = 2
};

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMeshClipDeform : public UCustomizableObjectNodeModifierBase
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeMeshClipDeform();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshClipDeform)
	TArray<FString> Tags;
	
	UPROPERTY(EditAnywhere, Category = MeshClipDeform)
	EShapeBindingMethod BindingMethod;
	
	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	
	inline UEdGraphPin* ClipShapePin() const
	{
		return FindPin(TEXT("Clip Shape"), EGPD_Input);
	}

	UEdGraphPin* OutputPin() const override
	{
		return FindPin(TEXT("Material"));
	}
};


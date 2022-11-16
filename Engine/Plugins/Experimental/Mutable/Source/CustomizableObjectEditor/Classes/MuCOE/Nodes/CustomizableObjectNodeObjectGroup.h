// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "EdGraph/EdGraphNode.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "MuCO/CustomizableObjectParameterTypeDefinitions.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "UObject/UObjectGlobals.h"

#include "CustomizableObjectNodeObjectGroup.generated.h"

class FArchive;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FPropertyChangedEvent;


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeObjectGroup : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeObjectGroup();

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	FString GroupName;

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	ECustomizableObjectGroupType GroupType;

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "Parameter UI Metadata"))
	FMutableParamUIMetadata ParamUIMetadata;

	// The sockets defined in meshes deriving from this node will inherit this socket priority. When in the generated merged mesh there
	// are clashes with socket names, the one with higher priority will be kept and the other discarded.
	UPROPERTY(EditAnywhere, Category = MeshSockets)
	int32 SocketPriority = 0;

	// UObject interface.
	void Serialize(FArchive& Ar) override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// Own interface
	UEdGraphPin* ObjectsPin() const
	{
		return FindPin(TEXT("Objects"));
	}

	UEdGraphPin* GroupProjectorsPin() const
	{
		return FindPin(TEXT("Projectors"));
	}

	UEdGraphPin* GroupPin() const
	{
		return FindPin(TEXT("Group"));
	}

private:
	FString LastGroupName;
};


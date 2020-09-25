// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingItem.h"
#include "EdGraph/EdGraphNode.h"

#include "DisplayClusterConfiguratorBaseNode.generated.h"

class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorOutputMappingSlot;

UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorBaseNode
	: public UEdGraphNode 
	, public IDisplayClusterConfiguratorOutputMappingItem
{
	GENERATED_BODY()

public:
	void Initialize(UObject* InObject, const FString& InNodeName, const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot> InSlot, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	void Initialize(UObject* InObject, const FString& InNodeName, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorItem Interface
	virtual void OnSelection() override;
	virtual UObject* GetObject() const override { return ObjectToEdit.Get(); }
	virtual bool IsSelected() override;
	//~ End IDisplayClusterConfiguratorItem Interface

	//~ Begin IDisplayClusterConfiguratorOutputMappingItem Interface
	virtual const FString& GetNodeName() const override;
	virtual TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> GetSlot() const override;
	//~ End IDisplayClusterConfiguratorOutputMappingItem Interface

	virtual void SetSlot(TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot> InSlot);

protected:
	TWeakObjectPtr<UObject> ObjectToEdit;

	FString NodeName;

	TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> SlotPtr;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

};

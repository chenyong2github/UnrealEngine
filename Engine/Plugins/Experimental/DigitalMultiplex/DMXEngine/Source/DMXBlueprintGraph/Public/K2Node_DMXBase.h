// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node_DMXBase.generated.h"

struct FK2Node_DMXBaseHelper
{
	static FName ClassPinName;
};

/**   */
UCLASS()
class DMXBLUEPRINTGRAPH_API UK2Node_DMXBase : public UK2Node
{
	GENERATED_BODY()
	
public:
	~UK2Node_DMXBase();

	//~ Begin UK2Node implementation
	virtual void AllocateDefaultPins() override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual void AddSearchMetaDataInfo(TArray<struct FSearchTagDataPair>& OutTaggedMetaData) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin);
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	//~ End UK2Node implementation

	virtual bool IsNodeSafeToIgnore() const override { return true; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	/** Get the blueprint input pin */
	UEdGraphPin* GetClassPin(const TArray<UEdGraphPin*>* InPinsToSearch = NULL) const;

	virtual UClass* GetClassPinBaseClass() const;

	virtual void OnLibraryAssetChanged(UDMXLibrary* Library);

protected:

	UFUNCTION()
	void OnLibraryEntitiesUpdated(UDMXLibrary* Library);

	/** Tooltip text for this node. */
	FText NodeTooltip;

	/** Constructing FText strings can be costly, so we cache the node's title */
	FNodeTextCache CachedNodeTitle;

	/** Stores the Library to be able to unbind from it later */
	UDMXLibrary* CachedLibrary;
	FDelegateHandle LibraryEntitiesUpdatedHandle;
};

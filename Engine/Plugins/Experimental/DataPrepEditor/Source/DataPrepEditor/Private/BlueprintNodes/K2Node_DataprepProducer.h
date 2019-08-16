// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"

#include "DataPrepAsset.h"

#include "K2Node_DataprepProducer.generated.h"

UCLASS(Experimental, HideCategories = (DataprepProducer_Internal))
class UK2Node_DataprepProducer : public UK2Node
{
public:
	GENERATED_BODY()

	UK2Node_DataprepProducer() : DataprepAsset(nullptr) {}

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
#if WITH_EDITOR
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
#endif
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanDuplicateNode() const override;
	virtual bool CanUserDeleteNode() const override;
	//~ End UEdGraphNode Interface


	//~ Begin K2Node Interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsNodePure() const override { return true; }
	//~ End K2Node Interface

public:
	void SetDataprepAsset( UDataprepAsset* InDataprepAsset );
	UDataprepAsset* GetDataprepAsset();

protected:
	UPROPERTY(EditAnywhere, Category="DataprepProducer_Internal")
	FSoftObjectPath DataprepAssetPath;

private:
	UDataprepAsset* DataprepAsset;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "BPFunctionClipboardData.generated.h"

struct FBPVariableDescription;

/** A helper struct for copying a Blueprint Function to the clipboard */
USTRUCT()
struct FBPFunctionClipboardData
{
	GENERATED_BODY()

	/** Default constructor */
	FBPFunctionClipboardData() = default;

	/** Constructs a FBPFunctionClipboardData from a graph */
	FBPFunctionClipboardData(const UEdGraph* InFuncGraph);
	
	/** Checks if the data is valid for configuring a graph */
	bool IsValid() const;

	/** Populates the struct based on a graph */
	void SetFromGraph(const UEdGraph* InFuncGraph);

	/**
	 * Creates and configures a new graph with data from this struct
	 *
	 * @param InBlueprint The Blueprint to add the new graph to
	 * @param InSchema    The schema to use for the new graph
	 * @return The new Graph, properly configured and populated if data is valid, or nullptr if data is invalid.
	 */
	UEdGraph* CreateAndPopulateGraph(UBlueprint* InBlueprint, TSubclassOf<UEdGraphSchema> InSchema);

private:
	/** Configures a graph with the nodes and settings */
	void PopulateGraph(UEdGraph* InFuncGraph);

private:
	/** Name of the Function */
	UPROPERTY()
	FName FuncName;

	/** A string containing the exported text for the nodes in this function */
	UPROPERTY()
	FString NodesString;
};
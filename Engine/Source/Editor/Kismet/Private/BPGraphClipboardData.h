// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "BPGraphClipboardData.generated.h"

struct FBPVariableDescription;
class FBlueprintEditor;

/** A helper struct for copying a Blueprint Function to the clipboard */
USTRUCT()
struct FBPGraphClipboardData
{
	GENERATED_BODY()

	/** Default constructor */
	FBPGraphClipboardData();

	/** Constructs a FBPGraphClipboardData from a graph */
	FBPGraphClipboardData(const UEdGraph* InFuncGraph);
	
	/** Checks if the data is valid for configuring a graph */
	bool IsValid() const;

	/** Returns whether the graph represents a function */
	bool IsFunction() const;

	/** Returns whether the graph represents a macro */
	bool IsMacro() const;

	/** Populates the struct based on a graph */
	void SetFromGraph(const UEdGraph* InFuncGraph);

	/**
	 * Creates and configures a new graph with data from this struct
	 *
	 * @param InBlueprint        The Blueprint to add the new graph to
	 * @param InBlueprintEditor  Editor that is being pasted into
	 * @param InCategoryOverride Category to place the new graph into
	 * @return The new Graph, properly configured and populated if data is valid, or nullptr if data is invalid.
	 */
	UEdGraph* CreateAndPopulateGraph(UBlueprint* InBlueprint, FBlueprintEditor* InBlueprintEditor, const FText& InCategoryOverride = FText::GetEmpty());

private:
	/** Configures a graph with the nodes and settings, returns false if operation was aborted */
	bool PopulateGraph(UEdGraph* InFuncGraph, FBlueprintEditor* InBlueprintEditor);

private:
	/** Name of the Graph */
	UPROPERTY()
	FName GraphName;

	/** The type of graph */
	UPROPERTY()
	TEnumAsByte<EGraphType> GraphType;

	/** A string containing the exported text for the nodes in this function */
	UPROPERTY()
	FString NodesString;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraph.h"
#include "DisplayClusterConfiguratorGraph.generated.h"

class FDisplayClusterConfiguratorToolkit;

UCLASS()
class UDisplayClusterConfiguratorGraph
	: public UEdGraph
{
	GENERATED_BODY()

public:
	void Initialize(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};

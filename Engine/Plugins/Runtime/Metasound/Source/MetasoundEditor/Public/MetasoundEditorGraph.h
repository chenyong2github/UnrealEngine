// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Metasound.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraph.generated.h"

UCLASS(MinimalAPI)
class UMetasoundEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UMetasound* Metasound;
};

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Metasound.h"
#include "MetasoundEditor.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraph.generated.h"

// Forward Declarations
struct FMetasoundDocument;


UCLASS(MinimalAPI)
class UMetasoundEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UObject* GetMetasound() const;
	UObject& GetMetasoundChecked() const;

private:
	UPROPERTY()
	UObject* ParentMetasound;

	friend class Metasound::Editor::FEditor;
};

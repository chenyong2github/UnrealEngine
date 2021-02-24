// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "MetasoundEditor.h"
#include "MetasoundFrontendLiteral.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraph.generated.h"

// Forward Declarations
struct FMetasoundFrontendDocument;
class UMetasoundEditorGraphInputNode;


UCLASS(MinimalAPI)
class UMetasoundEditorGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	UMetasoundEditorGraphInputNode* CreateInputNode(EMetasoundFrontendLiteralType LiteralType, UClass* InLiteralObjectClass, bool bInSelectNewNode);

	UObject* GetMetasound() const;
	UObject& GetMetasoundChecked() const;

private:
	UPROPERTY()
	UObject* ParentMetasound;

	friend class Metasound::Editor::FEditor;
};

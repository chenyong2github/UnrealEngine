// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/ObjectMacros.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"

#include "MetasoundEditorGraphNode.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetasound;

namespace Metasound
{
	namespace Editor
	{
		class FGraphBuilder;
	}
}


UCLASS(MinimalAPI)
class UMetasoundEditorGraphNode : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

public:
	/** Create a new input pin for this node */
	METASOUNDEDITOR_API void CreateInputPin();

	/** Estimate the width of this Node from the length of its title */
	METASOUNDEDITOR_API int32 EstimateNodeWidth() const;

	METASOUNDEDITOR_API void IteratePins(TUniqueFunction<void(UEdGraphPin* /* Pin */, int32 /* Index */)> Func, EEdGraphPinDirection InPinDirection = EGPD_MAX);

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	virtual bool CanUserDeleteNode() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FString GetDocumentationExcerptName() const override;
	virtual FString GetDocumentationLink() const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual FText GetTooltipText() const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PrepareForCopying() override;
	virtual void ReconstructNode() override;
	// End of UEdGraphNode interface

	// UObject interface
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& InEvent) override;
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR

	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	// End of UObject interface

	virtual bool CanAddInputPin() const
	{
		return false;
	}

	UObject& GetMetasoundChecked();
	const UObject& GetMetasoundChecked() const;

	Metasound::Frontend::FGraphHandle GetRootGraphHandle() const;
	Metasound::Frontend::FConstGraphHandle GetConstRootGraphHandle() const;

	Metasound::Frontend::FNodeHandle GetNodeHandle() const;
	Metasound::Frontend::FConstNodeHandle GetConstNodeHandle() const;

	FGuid GetNodeID() const;

protected:
	UPROPERTY()
	FMetasoundFrontendClassName ClassName;

	UPROPERTY()
	FGuid NodeID;

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphOutputNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	FName OutputTypeName;

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphExternalNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()
};
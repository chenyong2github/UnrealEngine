// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/Guid.h"
#include "Sound/SoundWave.h"
#include "Textures/SlateIcon.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphNode.generated.h"

// Forward Declarations
class UEdGraphPin;
class UMetaSound;
class UMetasoundEditorGraphOutput;

namespace Metasound
{
	namespace Editor
	{
		struct FGraphNodeValidationResult;
		class FGraphBuilder;

		// Map of class names to sorted array of registered version numbers
		using FSortedClassVersionMap = TMap<FName, TArray<FMetasoundFrontendVersionNumber>>;
	} // namespace Editor
} // namespace Metasound

UCLASS(MinimalAPI)
class UMetasoundEditorGraphNode : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

public:
	/** Create a new input pin for this node */
	METASOUNDEDITOR_API void CreateInputPin();

	/** Estimate the width of this Node from the length of its title */
	METASOUNDEDITOR_API int32 EstimateNodeWidth() const;

	METASOUNDEDITOR_API void IteratePins(TUniqueFunction<void(UEdGraphPin& /* Pin */, int32 /* Index */)> Func, EEdGraphPinDirection InPinDirection = EGPD_MAX);

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
	virtual void ReconstructNode() override;
	virtual FString GetPinMetaData(FName InPinName, FName InKey) override;
	// End of UEdGraphNode interface

	// UObject interface
	virtual void PreSave(FObjectPreSaveContext InSaveContext) override;

	virtual void PostLoad() override;
	virtual void PostEditUndo() override;

	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	// End of UObject interface

	virtual FSlateIcon GetNodeTitleIcon() const { return FSlateIcon(); }

	virtual bool CanAddInputPin() const
	{
		return false;
	}

	UObject& GetMetasoundChecked();
	const UObject& GetMetasoundChecked() const;

	void UpdatePosition();

	Metasound::Frontend::FGraphHandle GetRootGraphHandle() const;
	Metasound::Frontend::FConstGraphHandle GetConstRootGraphHandle() const;
	Metasound::Frontend::FNodeHandle GetNodeHandle() const;
	Metasound::Frontend::FConstNodeHandle GetConstNodeHandle() const;
	Metasound::Frontend::FDataTypeRegistryInfo GetPinDataTypeInfo(const UEdGraphPin& InPin) const;

	TSet<FString> GetDisallowedPinClassNames(const UEdGraphPin& InPin) const;

	virtual FMetasoundFrontendClassName GetClassName() const { return FMetasoundFrontendClassName(); }
	virtual FGuid GetNodeID() const { return FGuid(); }

protected:
	virtual void SetNodeID(FGuid InNodeID) { }

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphOutputNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UMetasoundEditorGraphOutput* Output;

	virtual FMetasoundFrontendClassName GetClassName() const override;
	virtual FGuid GetNodeID() const override;
	
	// Disallow deleting outputs as they require being connected to some
	// part of the graph by the Frontend Graph Builder (which is enforced
	// even when the Editor Graph Node does not have a visible input by
	// way of a literal input.
	virtual bool CanUserDeleteNode() const override;

protected:
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetNodeTitleIcon() const override;
	virtual void SetNodeID(FGuid InNodeID) override;

	friend class Metasound::Editor::FGraphBuilder;
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphExternalNode : public UMetasoundEditorGraphNode
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	FMetasoundFrontendClassName ClassName;

	UPROPERTY()
	FGuid NodeID;

	// Whether or not the referenced class is natively defined
	// (false if defined in another asset). Cached from node
	// implementation for fast access when validated.
	UPROPERTY()
	bool bIsClassNative = true;

public:
	virtual void PostLoad() override;

	virtual FMetasoundFrontendClassName GetClassName() const override { return ClassName; }
	virtual FGuid GetNodeID() const override { return NodeID; }
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetNodeTitleIcon() const override;

	FMetasoundFrontendVersionNumber FindHighestVersionInRegistry() const;
	FMetasoundFrontendVersionNumber FindHighestMinorVersionInRegistry() const;
	bool CanAutoUpdate() const;

	// Attempts to replace this node with a new one of the same class and given version number.
	// If this node is already of the given version, returns itself. If update fails, returns this node.
	UMetasoundEditorGraphExternalNode* UpdateToVersion(const FMetasoundFrontendVersionNumber& InNewVersion, bool bInPropagateErrorMessages);

	// Validates node and returns whether or not the node is valid.
	bool Validate(Metasound::Editor::FGraphNodeValidationResult& OutResult, bool bClearUpgradeMessage = true);


protected:
	// Refreshes all Pin Metadata from the associated Frontend node's default class interface.
	bool RefreshPinMetadata();

	virtual void SetNodeID(FGuid InNodeID) override
	{
		NodeID = InNodeID;
	}

	friend class Metasound::Editor::FGraphBuilder;
};

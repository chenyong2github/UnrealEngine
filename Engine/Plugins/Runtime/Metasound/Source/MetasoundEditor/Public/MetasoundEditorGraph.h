// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "AudioParameterInterface.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundEditorGraph.generated.h"


// Forward Declarations
struct FMetasoundFrontendDocument;
class ITargetPlatform;
class UMetasoundEditorGraphInputNode;
class UMetasoundEditorGraphNode;


namespace Metasound
{
	namespace Editor
	{
		struct FGraphValidationResults;
	} // namespace Editor
} // namespace Metasound

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetasoundMemberNameChanged, FGuid /* NodeID */);

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInputLiteral : public UObject
{
	GENERATED_BODY()

public:
	virtual void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const
	{
	}

	virtual FMetasoundFrontendLiteral GetDefault() const
	{
		return FMetasoundFrontendLiteral();
	}

	virtual EMetasoundFrontendLiteralType GetLiteralType() const
	{
		return EMetasoundFrontendLiteralType::None;
	}

	virtual void SetFromLiteral(const FMetasoundFrontendLiteral& InLiteral)
	{
	}

	virtual void PostEditUndo() override;
};

/** UMetasoundEditorGraphMember is a base class for non-node graph level members 
 * such as inputs, outputs and variables. */
UCLASS(Abstract)
class METASOUNDEDITOR_API UMetasoundEditorGraphMember : public UObject
{
	GENERATED_BODY()

public:
	/** Metasound Data Type. */
	UPROPERTY()
	FName TypeName;

	/** Delegate called when the name of the associated Frontend Node is changed */
	FOnMetasoundMemberNameChanged NameChanged;

	/** Return the section of where this member belongs. */
	virtual Metasound::Editor::ENodeSection GetSectionID() const;
	/** If true, this member cannot be removed by the user. */
	virtual bool IsRequired() const;
	/** Return the nodes associated with this member */
	virtual TArray<UMetasoundEditorGraphNode*> GetNodes() const;

	/** Sets the datatype on the member. */
	virtual void SetDataType(FName InNewType, bool bPostTransaction = true, bool bRegisterParentGraph = true);
	
	/** If the Member Name can be changed to InNewName, returns true,
	 * otherwise returns false with an error. */
	virtual bool CanRename(const FText& InNewName, FText& OutError) const;

	/** Set the display name */
	virtual void SetDisplayName(const FText& InNewName);
	/** Get the member display name */
	virtual FText GetDisplayName() const;

	/** Set the member name */
	virtual void SetMemberName(const FName& InNewName);
	/** Gets the members name */
	virtual FName GetMemberName() const;

	/** Set the member description */
	virtual void SetDescription(const FText& InDescription);
	/** Get the member description */
	virtual FText GetDescription() const;

	/** Returns the label of the derived member type (e.g. Input/Output/Variable) */
	virtual const FText& GetGraphMemberLabel() const;

	/** Returns the parent MetaSound Graph. If the Outer object of the member is non
	 * a UMetasoundEditorGraph, returns a nullptr. */
	UMetasoundEditorGraph* GetOwningGraph();

	/** Returns the parent MetaSound Graph. If the Outer object of the member is non
	 * a UMetasoundEditorGraph, returns a nullptr. */
	const UMetasoundEditorGraph* GetOwningGraph() const;
};

/** Base class for an input or output of the graph. */
UCLASS(Abstract)
class METASOUNDEDITOR_API UMetasoundEditorGraphVertex : public UMetasoundEditorGraphMember
{
	GENERATED_BODY()

protected:
	/** Adds the node handle for a newly created vertex. */
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FName& InNodeName, FName InDataType)
	{
		unimplemented();
		return Metasound::Frontend::INodeController::GetInvalidHandle();
	}


public:
	/** ID of Metasound Frontend node. */
	UPROPERTY()
	FGuid NodeID;

	/* Class name of Metasound Frontend node. */
	UPROPERTY()
	FMetasoundFrontendClassName ClassName;

	/* ~Begin UMetasoundEditorGraphMember interface */
	virtual FName GetMemberName() const override; 
	virtual FText GetDescription() const override;
	virtual FText GetDisplayName() const override;

	virtual bool CanRename(const FText& InNewName, FText& OutError) const override;
	virtual bool IsRequired() const override;
	virtual TArray<UMetasoundEditorGraphNode*> GetNodes() const override;
	virtual void SetDescription(const FText& InDescription) override;
	virtual void SetMemberName(const FName& InNewName) override;
	virtual void SetDisplayName(const FText& InNewName) override;
	virtual void SetDataType(FName InNewType, bool bPostTransaction = true, bool bRegisterParentGraph = true) override;
	/* ~End UMetasoundEditorGraphMember interface */

	/** Called when the data type changes. */
	virtual void OnDataTypeChanged();

	/** Returns the Metasound class type of the associated node */
	virtual EMetasoundFrontendClassType GetClassType() const { return EMetasoundFrontendClassType::Invalid; }

	/** Returns the node handle associated with the vertex. */
	Metasound::Frontend::FNodeHandle GetNodeHandle();
	/** Returns the node handle associated with the vertex. */
	Metasound::Frontend::FConstNodeHandle GetConstNodeHandle() const;
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInput : public UMetasoundEditorGraphVertex
{
	GENERATED_BODY()

protected:
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FName& InNodeName, FName InDataType) override;
	virtual EMetasoundFrontendClassType GetClassType() const override { return EMetasoundFrontendClassType::Input; }
	virtual const FText& GetGraphMemberLabel() const override;
public:
	UPROPERTY(VisibleAnywhere, Category = DefaultValue)
	UMetasoundEditorGraphInputLiteral* Literal;

	void UpdatePreviewInstance(const Metasound::FVertexName& InParameterName, TScriptInterface<IAudioParameterInterface>& InParameterInterface) const;

	void OnDataTypeChanged() override;

	void UpdateDocumentInput(bool bPostTransaction = true);
	virtual Metasound::Editor::ENodeSection GetSectionID() const override; 

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR
};


UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphOutput : public UMetasoundEditorGraphVertex
{
	GENERATED_BODY()

protected:
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FName& InNodeName, FName InDataType) override;
	virtual EMetasoundFrontendClassType GetClassType() const override { return EMetasoundFrontendClassType::Output; }
	virtual const FText& GetGraphMemberLabel() const override;
	virtual Metasound::Editor::ENodeSection GetSectionID() const override; 
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphVariable : public UMetasoundEditorGraphMember
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid VariableID;

public:

	UPROPERTY(VisibleAnywhere, Category = DefaultValue)
	UMetasoundEditorGraphInputLiteral* Literal;

	/* ~Begin UMetasoundEditorGraphMember interface */
	virtual Metasound::Editor::ENodeSection GetSectionID() const override;
	virtual void SetDataType(FName InNewType, bool bPostTransaction = true, bool bRegisterParentGraph = false) override;

	virtual FText GetDescription() const override;
	virtual void SetDescription(const FText& InDescription) override;

	virtual bool CanRename(const FText& InNewName, FText& OutError) const override;
	virtual void SetMemberName(const FName& InNewName) override; 
	virtual FName GetMemberName() const override; 

	virtual FText GetDisplayName() const override;
	virtual void SetDisplayName(const FText& InNewName) override;

	virtual bool IsRequired() const override;
	virtual TArray<UMetasoundEditorGraphNode*> GetNodes() const override;

	virtual const FText& GetGraphMemberLabel() const override;
	/* ~EndUMetasoundEditorGraphMember interface */

	void SetFrontendVariable(const Metasound::Frontend::FConstVariableHandle& InVariable);
	const FGuid& GetVariableID() const;

	void UpdateDocumentVariable(bool bPostTransaction = true);
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR
private:
	struct FVariableEditorNodes
	{
		UMetasoundEditorGraphNode* MutatorNode = nullptr;
		TArray<UMetasoundEditorGraphNode*> AccessorNodes;
		TArray<UMetasoundEditorGraphNode*> DeferredAccessorNodes;
	};

	struct FVariableNodeLocations
	{

		TOptional<FVector2D> MutatorLocation;
		TArray<FVector2D> AccessorLocations;
		TArray<FVector2D> DeferredAccessorLocations;
	};

	Metasound::Frontend::FVariableHandle GetVariableHandle();
	Metasound::Frontend::FConstVariableHandle GetConstVariableHandle() const;
	FVariableEditorNodes GetVariableNodes() const;
	FVariableNodeLocations GetVariableNodeLocations() const;
	void AddVariableNodes(UObject& InMetasound, Metasound::Frontend::FGraphHandle& InFrontendGraph, const FVariableNodeLocations& InNodeLocs);
	void UpdateEditorLiteralType();
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraph : public UMetasoundEditorGraphBase
{
	GENERATED_BODY()

public:
	UMetasoundEditorGraphInputNode* CreateInputNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode);

	Metasound::Frontend::FDocumentHandle GetDocumentHandle();
	Metasound::Frontend::FConstDocumentHandle GetDocumentHandle() const;
	Metasound::Frontend::FGraphHandle GetGraphHandle();
	Metasound::Frontend::FConstGraphHandle GetGraphHandle() const;

	UObject* GetMetasound();
	const UObject* GetMetasound() const;
	UObject& GetMetasoundChecked();
	const UObject& GetMetasoundChecked() const;

	void IterateInputs(TUniqueFunction<void(UMetasoundEditorGraphInput&)> InFunction) const;
	void IterateOutputs(TUniqueFunction<void(UMetasoundEditorGraphOutput&)> InFunction) const;

	bool ContainsInput(UMetasoundEditorGraphInput* InInput) const;
	bool ContainsOutput(UMetasoundEditorGraphOutput* InOutput) const;

	void SetPreviewID(uint32 InPreviewID);
	bool IsPreviewing() const;
	bool IsEditable() const;

	virtual bool Validate(bool bInClearUpdateNotes) override;

	virtual void RegisterGraphWithFrontend() override;


private:

	bool RemoveFrontendInput(UMetasoundEditorGraphInput& Input);
	bool RemoveFrontendOutput(UMetasoundEditorGraphOutput& Output);
	bool RemoveFrontendVariable(UMetasoundEditorGraphVariable& Variable);
	bool ValidateInternal(Metasound::Editor::FGraphValidationResults& OutResults, bool bClearUpgradeMessaging = true);

	// Preview ID is the Unique ID provided by the UObject that implements
	// a sound's ParameterInterface when a sound begins playing.
	uint32 PreviewID = INDEX_NONE;

	UPROPERTY()
	TArray<TObjectPtr<UMetasoundEditorGraphInput>> Inputs;

	UPROPERTY()
	TArray<TObjectPtr<UMetasoundEditorGraphOutput>> Outputs;

	UPROPERTY()
	TArray<TObjectPtr<UMetasoundEditorGraphVariable>> Variables;

public:
	UMetasoundEditorGraphInput* FindInput(FGuid InNodeID) const;
	UMetasoundEditorGraphInput* FindInput(FName InName) const;
	UMetasoundEditorGraphInput* FindOrAddInput(Metasound::Frontend::FNodeHandle InNodeHandle);

	UMetasoundEditorGraphOutput* FindOutput(FGuid InNodeID) const;
	UMetasoundEditorGraphOutput* FindOutput(FName InName) const;
	UMetasoundEditorGraphOutput* FindOrAddOutput(Metasound::Frontend::FNodeHandle InNodeHandle);

	UMetasoundEditorGraphVariable* FindVariable(const FGuid& InVariableID) const;
	UMetasoundEditorGraphVariable* FindOrAddVariable(const Metasound::Frontend::FConstVariableHandle& InVariableHandle);

	UMetasoundEditorGraphMember* FindMember(FGuid InNodeID) const;
	UMetasoundEditorGraphMember* FindAdjacentMember(const UMetasoundEditorGraphMember& InMember);

	bool RemoveMember(UMetasoundEditorGraphMember& InGraphMember);
	bool RemoveMemberNodes(UMetasoundEditorGraphMember& InGraphMember);
	bool RemoveFrontendMember(UMetasoundEditorGraphMember& InMember);

	friend class UMetaSoundFactory;
	friend class UMetaSoundSourceFactory;
	friend class Metasound::Editor::FGraphBuilder;
};

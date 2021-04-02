// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundEditor.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraph.generated.h"

// Forward Declarations
struct FMetasoundFrontendDocument;
class UMetasoundEditorGraphInputNode;
class UMetasoundEditorGraphNode;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetasoundNodeNameChanged, FGuid /* NodeID */);

UCLASS(Abstract)
class METASOUNDEDITOR_API UMetasoundEditorGraphVariable : public UObject
{
	GENERATED_BODY()

protected:
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FString& InNodeName, const FText& InNodeDisplayName, FName InDataType)
	{
		return Metasound::Frontend::INodeController::GetInvalidHandle();
	}

public:
	UPROPERTY()
	FMetasoundFrontendClassName ClassName;

	UPROPERTY()
	FGuid NodeID;

	UPROPERTY()
	FName TypeName;

	// Called when the name of the associated Frontend Node is changed
	FOnMetasoundNodeNameChanged NameChanged;

	Metasound::Frontend::FNodeHandle GetNodeHandle() const;
	Metasound::Frontend::FConstNodeHandle GetConstNodeHandle() const;

	TArray<UMetasoundEditorGraphNode*> GetNodes() const;
	void SetDataType(FName InNewType);
	virtual void OnDataTypeChanged() { }
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInputLiteral : public UObject
{
	GENERATED_BODY()

public:
	virtual void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const
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

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override;
#endif // WITH_EDITORONLY_DATA
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphInput : public UMetasoundEditorGraphVariable
{
	GENERATED_BODY()

protected:
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FString& InNodeName, const FText& InNodeDisplayName, FName InDataType) override;


public:
	UPROPERTY(VisibleAnywhere, Category = DefaultValue)
	UMetasoundEditorGraphInputLiteral* Literal;

	void UpdateDocumentInput(bool bPostTransaction = true);
	void UpdatePreviewInstance(const Metasound::FVertexKey& InParameterName, IAudioInstanceTransmitter& InInstanceTransmitter) const;

	void OnDataTypeChanged() override;
	void OnLiteralChanged(bool bPostTransaction = true);

#if WITH_EDITORONLY_DATA
	virtual void PostEditUndo() override;
#endif // WITH_EDITORONLY_DATA
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraphOutput : public UMetasoundEditorGraphVariable
{
	GENERATED_BODY()

protected:
	virtual Metasound::Frontend::FNodeHandle AddNodeHandle(const FString& InNodeName, const FText& InNodeDisplayName, FName InDataType) override;
};

UCLASS()
class METASOUNDEDITOR_API UMetasoundEditorGraph : public UMetasoundEditorGraphBase
{
	GENERATED_BODY()

public:
	UMetasoundEditorGraphInputNode* CreateInputNode(Metasound::Frontend::FNodeHandle InNodeHandle, bool bInSelectNewNode);

	UObject* GetMetasound() const;
	UObject& GetMetasoundChecked() const;

	void IterateInputs(TUniqueFunction<void(UMetasoundEditorGraphInput&)> InFunction) const;
	void IterateOutputs(TUniqueFunction<void(UMetasoundEditorGraphOutput&)> InFunction) const;

	bool ContainsInput(UMetasoundEditorGraphInput* InInput) const;
	bool ContainsOutput(UMetasoundEditorGraphOutput* InOutput) const;

	virtual void Synchronize();

	// Sets the transmitter interface for the current metasound preview.
	void SetMetasoundInstanceTransmitter(TUniquePtr<IAudioInstanceTransmitter>&& InTransmitter);

	// Gets the transmitter interface for the current metasound preview.
	IAudioInstanceTransmitter* GetMetasoundInstanceTransmitter();

	// Gets the transmitter interface for the current metasound preview.
	const IAudioInstanceTransmitter* GetMetasoundInstanceTransmitter() const;

private:
	UPROPERTY()
	TObjectPtr<UObject> ParentMetasound;

	UPROPERTY()
	TArray<TObjectPtr<UMetasoundEditorGraphInput>> Inputs;

	UPROPERTY()
	TArray<TObjectPtr<UMetasoundEditorGraphOutput>> Outputs;

public:
	UMetasoundEditorGraphInput* FindInput(FGuid InNodeID) const;
	UMetasoundEditorGraphInput* FindOrAddInput(Metasound::Frontend::FNodeHandle InNodeHandle);

	UMetasoundEditorGraphOutput* FindOutput(FGuid InNodeID) const;
	UMetasoundEditorGraphOutput* FindOrAddOutput(Metasound::Frontend::FNodeHandle InNodeHandle);

	UMetasoundEditorGraphVariable* FindVariable(FGuid InNodeID) const;

	bool RemoveVariable(UMetasoundEditorGraphVariable& InVariable);

	TUniquePtr<IAudioInstanceTransmitter> Transmitter;

// 	friend class Metasound::Editor::FEditor;
	friend class UMetasoundFactory;
	friend class UMetasoundSourceFactory;
};

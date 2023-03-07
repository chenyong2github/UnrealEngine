// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentCacheInterface.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundVertex.h"
#include "Templates/Function.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundFrontendDocumentBuilder.generated.h"


// Forward Declarations
class FMetasoundAssetBase;


namespace Metasound::Frontend
{
	struct FNamedEdge
	{
		const FGuid OutputNodeID;
		const FName OutputName;
		const FGuid InputNodeID;
		const FName InputName;

		friend bool operator==(const FNamedEdge& InLHS, const FNamedEdge& InRHS)
		{
			return InLHS.OutputNodeID == InRHS.OutputNodeID
				&& InLHS.OutputName == InRHS.OutputName
				&& InLHS.InputNodeID == InRHS.InputNodeID
				&& InLHS.InputName == InRHS.InputName;
		}

		friend bool operator!=(const FNamedEdge& InLHS, const FNamedEdge& InRHS)
		{
			return !(InLHS == InRHS);
		}

		friend FORCEINLINE uint32 GetTypeHash(const FNamedEdge& InBinding)
		{
			const int32 NameHash = HashCombineFast(GetTypeHash(InBinding.OutputName), GetTypeHash(InBinding.InputName));
			const int32 GuidHash = HashCombineFast(GetTypeHash(InBinding.OutputNodeID), GetTypeHash(InBinding.InputNodeID));
			return HashCombineFast(NameHash, GuidHash);
		}
	};
} // namespace Metasound::Frontend


UCLASS()
class  METASOUNDFRONTEND_API UMetaSoundBuilderDocument : public UObject, public IMetaSoundDocumentInterface
{
	GENERATED_BODY()

public:
	virtual const FMetasoundFrontendDocument& GetDocument() const override
	{
		return Document;
	}

private:
	virtual FMetasoundFrontendDocument& GetDocument() override
	{
		return Document;
	}

	UPROPERTY(Transient)
	FMetasoundFrontendDocument Document;

	friend struct FMetaSoundFrontendDocumentBuilder;
};

// Builder used to support dynamically generating MetaSound documents at runtime. Builder contains caches that speed up
// common search and modification operations on a given document, which may result in slower performance on construction,
// but faster manipulation of its managed document.  The builder's managed copy of a document is expected to not be modified
// by any external system to avoid cache to not become stale.
USTRUCT()
struct METASOUNDFRONTEND_API FMetaSoundFrontendDocumentBuilder
{
	GENERATED_BODY()

public:
	FMetaSoundFrontendDocumentBuilder();
	FMetaSoundFrontendDocumentBuilder(FMetaSoundFrontendDocumentBuilder&& InBuilder);
	FMetaSoundFrontendDocumentBuilder(TScriptInterface<IMetaSoundDocumentInterface> InDocumentInterface);

	// Copying actively used builders is highly discouraged (as only one builder should be acting on a document at any given time
	// to avoid internal cache state to become out-of-date) but must contractually be implemented in this case as a USTRUCT (builders
	// are USTRUCTS to keep document dependencies from being GC'ed).  To protect against this kind of "back door" building from another
	// builder, copying builders also creates a new document & builder owner.
	FMetaSoundFrontendDocumentBuilder(const FMetaSoundFrontendDocumentBuilder& InBuilder);
	FMetaSoundFrontendDocumentBuilder& operator=(const FMetaSoundFrontendDocumentBuilder& InRHS);

	FMetaSoundFrontendDocumentBuilder& operator=(FMetaSoundFrontendDocumentBuilder&& InRHS);

	const FMetasoundFrontendClass* AddGraphDependency(const FMetasoundFrontendGraphClass& InClass);
	const FMetasoundFrontendClass* AddNativeDependency(const FMetasoundFrontendClassMetadata& InClassMetadata);
	const FMetasoundFrontendEdge* AddEdge(FMetasoundFrontendEdge&& InNewEdge);
	bool AddEdgesByNamedConnections(const TSet<Metasound::Frontend::FNamedEdge>& ConnectionsToMake, TArray<const FMetasoundFrontendEdge*>* OutNewEdges = nullptr);
	bool AddEdgesByNodeClassInterfaceBindings(const FGuid& InFromNodeID, const FGuid& InToNodeID);
	bool AddEdgesFromMatchingInterfaceNodeOutputsToGraphOutputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated);
	bool AddEdgesFromMatchingInterfaceNodeInputsToGraphInputs(const FGuid& InNodeID, TArray<const FMetasoundFrontendEdge*>& OutEdgesCreated);
	const FMetasoundFrontendNode* AddGraphInput(const FMetasoundFrontendClassInput& InClassInput);
	const FMetasoundFrontendNode* AddGraphOutput(const FMetasoundFrontendClassOutput& InClassOutput);
	bool AddInterface(FName InterfaceName);

	const FMetasoundFrontendNode* AddGraphNode(const FMetasoundFrontendGraphClass& InGraphClass, FGuid InNodeID = FGuid::NewGuid());

	bool CanAddEdge(const FMetasoundFrontendEdge& InEdge) const;

	bool ContainsEdge(const FMetasoundFrontendEdge& InEdge) const;
	bool ContainsNode(const FGuid& InNodeID) const;
	bool ConvertFromPreset();

	static bool FindDeclaredInterfaces(const FMetasoundFrontendDocument& InDocument, TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces);
	bool FindDeclaredInterfaces(TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces) const;

	const FMetasoundFrontendClass* FindDependency(const FGuid& InClassID) const;
	const FMetasoundFrontendClass* FindDependency(const FMetasoundFrontendClassMetadata& InMetadata) const;

	bool FindInterfaceInputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutInputs) const;
	bool FindInterfaceOutputNodes(FName InterfaceName, TArray<const FMetasoundFrontendNode*>& OutOutputs) const;

	const FMetasoundFrontendNode* FindGraphInputNode(FName InputName) const;
	const FMetasoundFrontendNode* FindGraphOutputNode(FName OutputName) const;

	const FMetasoundFrontendVertex* FindNodeInput(const FGuid& InNodeID, const FGuid& InVertexID) const;
	const FMetasoundFrontendVertex* FindNodeInput(const FGuid& InNodeID, FName InVertexName) const;
	const FMetasoundFrontendVertex* FindNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID) const;
	const FMetasoundFrontendVertex* FindNodeOutput(const FGuid& InNodeID, FName InVertexName) const;

	const FMetasoundFrontendNode* FindNode(const FGuid& InNodeID) const;
	TArray<const FMetasoundFrontendVertex*> FindNodeInputs(const FGuid& InNodeID, FName TypeName = FName()) const;
	TArray<const FMetasoundFrontendVertex*> FindNodeOutputs(const FGuid& InNodeID, FName TypeName = FName()) const;

	const FMetasoundFrontendDocument& GetDocument() const;

	static void InitGraphClassMetadata(FMetasoundFrontendClassMetadata& InOutMetadata, bool bResetVersion = false);
	void InitDocument(FName UClassName);
	void InitNodeLocations();

	bool IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const;
	bool IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const;

	bool IsInterfaceDeclared(FName InInterfaceName) const;
	bool IsInterfaceDeclared(const FMetasoundFrontendVersion& InInterfaceVersion) const;

	bool ModifyInterfaces(TArray<FMetasoundFrontendVersion> OutputFormatsToRemove, TArray<FMetasoundFrontendVersion> OutputFormatsToAdd);

	bool RemoveDependency(EMetasoundFrontendClassType ClassType, const FMetasoundFrontendClassName& InClassName, const FMetasoundFrontendVersionNumber& InClassVersionNumber);
	bool RemoveEdge(const FMetasoundFrontendEdge& EdgeToRemove);
	bool RemoveEdgesByNamedConnections(const TSet<Metasound::Frontend::FNamedEdge>& ConnectionsToRemove, TArray<FMetasoundFrontendEdge>* OutRemovedEdges = nullptr);
	bool RemoveEdgesByNodeClassInterfaceBindings(const FGuid& InOutputNodeID, const FGuid& InInputNodeID);
	bool RemoveEdgesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID);
	bool RemoveEdgeToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID);
	bool RemoveInterface(FName InName);
	bool RemoveNode(const FGuid& InNodeID);

#if WITH_EDITOR
	void SetAuthor(const FString& InAuthor);
#endif // WITH_EDITOR

	bool SetNodeInputDefault(const FGuid& InNodeID, const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral);

private:
	using FFinalizeNodeFunctionRef = TFunctionRef<void(FMetasoundFrontendNode&, const Metasound::Frontend::FNodeRegistryKey&)>;

	UPROPERTY(Transient)
	TScriptInterface<IMetaSoundDocumentInterface> DocumentInterface;

	TUniquePtr<Metasound::Frontend::IDocumentCache> DocumentCache;

	FMetasoundFrontendDocument& GetDocument();

	FMetasoundFrontendNode* AddNodeInternal(const FMetasoundFrontendClassMetadata& InClassMetadata, FFinalizeNodeFunctionRef FinalizeNode, FGuid InNodeID = FGuid::NewGuid());

	const TScriptInterface<IMetaSoundDocumentInterface> FindOrLoadNodeClassDocument(const FGuid& InNodeID) const;
	const FMetasoundFrontendClassInput* FindNodeInputClassInput(const FGuid& InNodeID, const FGuid& InVertexID) const;
	const FMetasoundFrontendClassOutput* FindNodeOutputClassOutput(const FGuid& InNodeID, const FGuid& InVertexID) const;

	void ReloadCache();
};

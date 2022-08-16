// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundAccessPtr.h"
#include "MetasoundAssetManager.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundGraph.h"
#include "MetasoundLog.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundVertex.h"
#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Forward Declarations
class UEdGraph;

namespace Metasound
{
	namespace Frontend
	{
		// Forward Declarations
		class IInterfaceRegistryEntry;

		METASOUNDFRONTEND_API float GetDefaultBlockRate();
	} // namespace Frontend
} // namespace Metasound


/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundFrontendDocuments, control
 * over the FMetasoundFrontendClassInterface of the FMetasoundFrontendDocument.  It also enables the UObject
 * to be utilized by a host of other engine tools built to support MetaSounds.
 */
class METASOUNDFRONTEND_API FMetasoundAssetBase
{
public:
	static const FString FileExtension;

	FMetasoundAssetBase() = default;
	virtual ~FMetasoundAssetBase() = default;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const = 0;

	// Returns the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @return Editor graph associated with this metasound uobject.
	virtual UEdGraph* GetGraph() = 0;
	virtual const UEdGraph* GetGraph() const = 0;
	virtual UEdGraph& GetGraphChecked() = 0;
	virtual const UEdGraph& GetGraphChecked() const = 0;

	// Sets the graph associated with this Metasound. Graph is required to be referenced on
	// Metasound UObject for editor serialization purposes.
	// @param Editor graph associated with this metasound object.
	virtual void SetGraph(UEdGraph* InGraph) = 0;

	// Only required for editor builds. Adds metadata to properties available when the object is
	// not loaded for use by the Asset Registry.
	virtual void SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InClassInfo) = 0;
#endif // WITH_EDITORONLY_DATA

	// Called when the interface is changed, presenting the opportunity for
	// any reflected object data to be updated based on the new interface.
	// Returns whether or not any edits were made.
	virtual bool ConformObjectDataToInterfaces() = 0;

	// Registers the root graph of the given asset with the MetaSound Frontend.
	void RegisterGraphWithFrontend(Metasound::Frontend::FMetaSoundAssetRegistrationOptions InRegistrationOptions = Metasound::Frontend::FMetaSoundAssetRegistrationOptions());

	// Unregisters the root graph of the given asset with the MetaSound Frontend.
	void UnregisterGraphWithFrontend();

	// Sets/overwrites the root class metadata
	virtual void SetMetadata(FMetasoundFrontendClassMetadata& InMetadata);

	// Rebuild the asset class dependency key array.
	void RebuildReferencedAssetClassKeys();

	// Returns the interface entries declared by the given asset's document from the InterfaceRegistry.
	bool GetDeclaredInterfaces(TArray<const Metasound::Frontend::IInterfaceRegistryEntry*>& OutInterfaces) const;

	// Returns whether an interface with the given version is declared by the given asset's document.
	bool IsInterfaceDeclared(const FMetasoundFrontendVersion& InVersion) const;

	// Gets the asset class info.
	virtual Metasound::Frontend::FNodeClassInfo GetAssetClassInfo() const = 0;

	// Returns all the class keys of this asset's referenced assets
	virtual const TSet<FString>& GetReferencedAssetClassKeys() const = 0;

	// Returns set of cached class references set on last registration
	// prior to serialize. Used at runtime to hint where to load referenced
	// class if sound loads before AssetManager scan is completed.  When registered
	// hint paths to classes here can be superseded by another asset class if it shares
	// the same key and has already been registered in the MetaSoundAssetManager.
	virtual TSet<FSoftObjectPath>& GetReferencedAssetClassCache() = 0;
	virtual const TSet<FSoftObjectPath>& GetReferencedAssetClassCache() const = 0;

	bool AddingReferenceCausesLoop(const FSoftObjectPath& InReferencePath) const;
	bool IsReferencedAsset(const FMetasoundAssetBase& InAssetToCheck) const;
	void ConvertFromPreset();
	bool IsRegistered() const;

	// Imports data from a JSON string directly
	bool ImportFromJSON(const FString& InJSON);

	// Imports the asset from a JSON file at provided path
	bool ImportFromJSONAsset(const FString& InAbsolutePath);

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FDocumentHandle GetDocumentHandle();
	Metasound::Frontend::FConstDocumentHandle GetDocumentHandle() const;

	// Returns handle for the root metasound graph of this asset.
	Metasound::Frontend::FGraphHandle GetRootGraphHandle();
	Metasound::Frontend::FConstGraphHandle GetRootGraphHandle() const;

	// Overwrites the existing document. If the document's interface is not supported,
	// the FMetasoundAssetBase be while queried for a new one using `GetPreferredInterface`.
	void SetDocument(const FMetasoundFrontendDocument& InDocument);

	FMetasoundFrontendDocument& GetDocumentChecked();
	const FMetasoundFrontendDocument& GetDocumentChecked() const;

	void AddDefaultInterfaces();

	bool VersionAsset();

#if WITH_EDITOR
	/*
	 * Caches transient metadata (class & vertex) found in the registry
	 * that is not necessary for serialization or core graph generation.
	 *
	 * @return - Whether class was found in the registry & data was cached successfully.
	 */
	void CacheRegistryMetadata();

	// TODO: These flags & associated functions are highly UE editor-specific.
	// Split synchronization requirement flag into synchronization required &
	// object type refresh or checking frontend class guids when synchronizing.
	bool GetSynchronizationRequired() const;
	bool GetSynchronizationUpdateDetails() const;
	void ResetSynchronizationState();
	void SetUpdateDetailsOnSynchronization();
	void SetSynchronizationRequired();
#endif // WITH_EDITOR

	// Calls the outermost package and marks it dirty.
	bool MarkMetasoundDocumentDirty() const;

	struct FSendInfoAndVertexName
	{
		Metasound::FMetaSoundParameterTransmitter::FSendInfo SendInfo;
		Metasound::FVertexName VertexName;
	};

	// Returns the owning asset responsible for transactions applied to MetaSound
	virtual UObject* GetOwningAsset() = 0;

	// Returns the owning asset responsible for transactions applied to MetaSound
	virtual const UObject* GetOwningAsset() const = 0;

	FString GetOwningAssetName() const;

protected:
	virtual void SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys) = 0;

	// Get information for communicating asynchronously with MetaSound running instance.
	TArray<FSendInfoAndVertexName> GetSendInfos(uint64 InInstanceID) const;

#if WITH_EDITORONLY_DATA
	FText GetDisplayName(FString&& InTypeName) const;
#endif // WITH_EDITORONLY_DATA

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FDocumentAccessPtr GetDocument() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const = 0;

#if WITH_EDITORONLY_DATA
	bool bSynchronizationRequired = true;
	bool bSynchronizationUpdateDetails = false;
#endif // WITH_EDITORONLY_DATA

protected:
	// Container for runtime data of MetaSound graph.
	struct FRuntimeData
	{
		// Current ID of graph.
		FGuid ChangeID;

		// Array of inputs which can be transmitted to.
		TArray<FMetasoundFrontendClassInput> TransmittableInputs;

		// Core graph.
		TSharedPtr<Metasound::IGraph, ESPMode::ThreadSafe> Graph;
	};

	// Returns the cached runtime data. Call updates cached data if out-of-date.
	const FRuntimeData& CacheRuntimeData(const FMetasoundFrontendDocument& InPreprocessedDoc);

	// Returns the cached runtime data.
	const FRuntimeData& GetRuntimeData() const;

	// Returns all transmissible class inputs.  This is a potentially expensive.
	// Prefer accessing transmissible class inputs using CacheRuntimeData.
	TArray<FMetasoundFrontendClassInput> GetTransmittableClassInputs() const;

private:
	Metasound::Frontend::FNodeRegistryKey RegistryKey;

	// Cache ID is used to determine whether CachedRuntimeData is out-of-date.
	FGuid CurrentCachedRuntimeDataChangeID;
	FRuntimeData CachedRuntimeData;

	TSharedPtr<Metasound::IGraph, ESPMode::ThreadSafe> BuildMetasoundDocument(const FMetasoundFrontendDocument& InPreprocessDoc, const TArray<FMetasoundFrontendClassInput>& InTransmittableInputs) const;
	Metasound::FSendAddress CreateSendAddress(uint64 InInstanceID, const Metasound::FVertexName& InVertexName, const FName& InDataTypeName) const;
	Metasound::Frontend::FNodeHandle AddInputPinForSendAddress(const Metasound::FMetaSoundParameterTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const;
};

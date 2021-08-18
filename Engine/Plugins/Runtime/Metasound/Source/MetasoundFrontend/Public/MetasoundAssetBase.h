// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MetasoundAccessPtr.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundGraph.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"


// Forward Declarations
class FMetasoundAssetBase;
class UEdGraph;


namespace Metasound
{
	namespace AssetTags
	{
		extern const FString METASOUNDFRONTEND_API ArrayDelim;

		extern const FName METASOUNDFRONTEND_API AssetClassID;
		extern const FName METASOUNDFRONTEND_API RegistryVersionMajor;
		extern const FName METASOUNDFRONTEND_API RegistryVersionMinor;

#if WITH_EDITORONLY_DATA
		extern const FName METASOUNDFRONTEND_API RegistryInputTypes;
		extern const FName METASOUNDFRONTEND_API RegistryOutputTypes;
#endif // WITH_EDITORONLY_DATA
	} // namespace AssetTags
} // namespace Metasound

struct METASOUNDFRONTEND_API FMetaSoundAssetRegistrationOptions
{
	// If true, forces a re-register of this class (and all class dependencies
	// if the following option 'bRegisterDependencies' is enabled).
	bool bForceReregister = true;

	// If true, recursively attempts to register dependencies.
	bool bRegisterDependencies = false;

	// Attempt to auto-update (only runs if class not registered or set to force re-register)
	bool bAutoUpdate = true;

	// Attempt to rebuild referenced class keys (only run if class not registered or set to force re-register)
	bool bRebuildReferencedAssetClassKeys = true;
};

class METASOUNDFRONTEND_API IMetaSoundAssetManager
{
	static IMetaSoundAssetManager* Instance;

public:
	static void Set(IMetaSoundAssetManager& InInterface)
	{
		check(!Instance);
		Instance = &InInterface;
	}

	static IMetaSoundAssetManager& GetChecked()
	{
		check(Instance);
		return *Instance;
	}

	virtual bool CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const = 0;
	virtual FMetasoundAssetBase* FindAssetFromKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const = 0;
	virtual const FSoftObjectPath* FindObjectPathFromKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const = 0;

	// Rescans settings for blacklisted assets not to run reference auto-update against.
	virtual void RescanAutoUpdateBlacklist() = 0;

	// Attempts to load an FMetasoundAssetBase from the given path, or returns it if its already loaded
	virtual FMetasoundAssetBase* TryLoadAsset(const FSoftObjectPath& InObjectPath) const = 0;
};

/** FMetasoundAssetBase is intended to be a mix-in subclass for UObjects which utilize
 * Metasound assets.  It provides consistent access to FMetasoundFrontendDocuments, control
 * over the FMetasoundFrontendArchetype of the FMetasoundFrontendDocument.  It also enables the UObject
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

	// Called when the archetype is changed, presenting the opportunity for
	// any reflected object data to be updated based on the new archetype.
	// Returns whether or not any edits were made.
	virtual bool ConformObjectDataToArchetype() = 0;

	// Registers the root graph of the given asset with the MetaSound Frontend.
	void RegisterGraphWithFrontend(FMetaSoundAssetRegistrationOptions InRegistrationOptions = FMetaSoundAssetRegistrationOptions());

	// Unregisters the root graph of the given asset with the MetaSound Frontend.
	void UnregisterGraphWithFrontend();

	// Sets/overwrites the root class metadata
	virtual void SetMetadata(FMetasoundFrontendClassMetadata& InMetadata);

	// Returns  a description of the required inputs and outputs for this metasound UClass.
	virtual const FMetasoundFrontendVersion& GetDefaultArchetypeVersion() const = 0;

	// Returns true if the archetype is supported by this object.
	virtual bool IsArchetypeSupported(const FMetasoundFrontendVersion& InArchetypeVersion) const;

	// Returns an array of archetypes preferred for this class.
	virtual const TArray<FMetasoundFrontendVersion>& GetSupportedArchetypeVersions() const = 0;

	// Returns the preferred archetype for the given document.
	virtual FMetasoundFrontendVersion GetPreferredArchetypeVersion(const FMetasoundFrontendDocument& InDocument) const;
	bool GetArchetype(FMetasoundFrontendArchetype& OutArchetype) const;

	// Gets the asset class info.
	virtual Metasound::Frontend::FNodeClassInfo GetAssetClassInfo() const = 0;

	// Returns all the class keys of this asset's referenced assets
	virtual const TSet<FString>& GetReferencedAssetClassKeys() const = 0;

	// Returns set of cached class references required by this class
	// to persist during the duration of this class being registered.
	// TODO: May no longer be needed, but leaving for now to avoid
	// potential edge cases with GC/UObject references.  Could potentially
	// move to cook only and making auto-update optional (to avoid large
	// MetaSound patches caused by references being updated).
	virtual TSet<UObject*>& GetReferencedAssetClassCache() = 0;

	bool AddingReferenceCausesLoop(const FSoftObjectPath& InReferencePath) const;
	void ConvertFromPreset();
	TArray<FMetasoundAssetBase*> FindOrLoadReferencedAssets() const;
	bool IsRegistered() const;
	void RebuildReferencedAssetClassKeys();

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

	// Overwrites the existing document. If the document's archetype is not supported,
	// the FMetasoundAssetBase be while queried for a new one using `GetPreferredArchetype`. If `bForceUpdateArchetype`
	// is true, `GetPreferredArchetype` will be used whether or not the provided document's archetype
	// is supported. 
	void SetDocument(const FMetasoundFrontendDocument& InDocument);

	FMetasoundFrontendDocument& GetDocumentChecked();
	const FMetasoundFrontendDocument& GetDocumentChecked() const;

	void ConformDocumentToArchetype();
	bool AutoUpdate(bool bInMarkDirty = false);
	bool VersionAsset();

	// Calls the outermost package and marks it dirty.
	bool MarkMetasoundDocumentDirty() const;

	struct FSendInfoAndVertexName
	{
		Metasound::FMetasoundInstanceTransmitter::FSendInfo SendInfo;
		FString VertexName;
	};

	// Builds the Metasound Document returned by `GetDocument() const`.
	virtual TUniquePtr<Metasound::IGraph> BuildMetasoundDocument() const;

protected:
	virtual void SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys) = 0;

	TArray<FSendInfoAndVertexName> GetSendInfos(uint64 InInstanceID) const;

#if WITH_EDITORONLY_DATA
	FText GetDisplayName(FString&& InTypeName) const;
#endif // WITH_EDITORONLY_DATA

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FDocumentAccessPtr GetDocument() = 0;

	// Returns an access pointer to the document.
	virtual Metasound::Frontend::FConstDocumentAccessPtr GetDocument() const = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual UObject* GetOwningAsset() = 0;

	// Returns the owning asset responsible for transactions applied to metasound
	virtual const UObject* GetOwningAsset() const = 0;

	FString GetOwningAssetName() const;


private:
	Metasound::Frontend::FNodeRegistryKey RegistryKey;

	TSet<Metasound::FVertexKey> GetNonTransmittableInputVertices(const FMetasoundFrontendDocument& InDoc) const;
	TArray<FString> GetTransmittableInputVertexNames() const;
	Metasound::FSendAddress CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const;
	Metasound::Frontend::FNodeHandle AddInputPinForSendAddress(const Metasound::FMetasoundInstanceTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const;
};

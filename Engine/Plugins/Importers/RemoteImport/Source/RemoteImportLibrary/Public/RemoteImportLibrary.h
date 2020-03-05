// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "RemoteImportLibrary.generated.h"

class FRemoteImportServer;


DECLARE_DELEGATE_OneParam(FStringArgDelegate, const FString&);


/**
 * Structure describing a RemoteImport destination.
 *
 * Server-side, Anchors are setup to implement RemoteImport commands.
 * By registering Anchors to the URemoteImportLibrary, Anchors becomes visible from clients.
 * Clients can send commands to registered Anchors.
 */
USTRUCT(BlueprintType, meta=(Experimental))
struct REMOTEIMPORTLIBRARY_API FRemoteImportAnchor
{
	GENERATED_BODY()

public:
	/** User friendly name, exposed in client UIs */
	UPROPERTY(BlueprintReadWrite, Category=Anchor)
	FString Name;

	/** User friendly description, exposed in client UIs */
	UPROPERTY(BlueprintReadWrite, Category=Anchor)
	FString Description;

public:
	/** Custom implementation of the 'ImportFile' intent */
	FStringArgDelegate OnImportFileDelegate;
};


/**
 * Exposes:
 * - API to enable/disable RemoteImport server (process visibility for clients)
 * - API to register/unregister RemoteImportAnchors (specific destination for clients)
 */
UCLASS(meta=(Experimental))
class REMOTEIMPORTLIBRARY_API URemoteImportLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	URemoteImportLibrary();
public:
	/**
	 * Triggers OnImportFileDelegate on specified Anchor
	 *
	 * @param FilePath          Path of the file to import
	 * @param DestinationName   Name of the destination FRemoteImportAnchor
	 * @return                  Whether the import operation has been triggered
	 */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Action")
	static bool ImportSource(const FString& FilePath, const FString& DestinationName);


	/**
	 * Register a possible destination for a remote import
	 * @param Anchor        Description of the destination
	 * @param bAllowRename  When true, a unique name is generated to register the anchor even when names collide
	 * @returns the anchor handle, required to unregister the Anchor
	 */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Anchor")
	static FString RegisterAnchor(const FRemoteImportAnchor& Anchor, bool bAllowRename=true);

	/**
	 * Remove a destination
	 * @param AnchorHandle  Registered handle as returned by RegisterAnchor()
	 */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Anchor")
	static void UnregisterAnchor(const FString& AnchorHandle);

	/**
	 * List registered Anchor names.
	 * @return Names that are valid as destination names.
	 */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Anchor")
	static TArray<FString> ListAnchors();


	/** Enable the server so that the process is visible to clients */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Server")
	static void StartRemoteImportServer();

	/** Disable the server so that the process is hidden to clients */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Server")
	static void StopRemoteImportServer();

	/** @return Whether the server is currently active */
	UFUNCTION(BlueprintCallable, Category="RemoteImport|Server")
	static bool IsRemoteImportServerActive();

public:
	/** @return The delegate that will be triggered when the registered anchor list changes */
	static FSimpleMulticastDelegate& GetAnchorListChangeDelegate() { return OnAnchorListChange; }

private:
	static FRemoteImportAnchor* FindAnchor(const FString& AnchorName);

private:
	static TUniquePtr<FRemoteImportServer> Instance;
	static TArray<FRemoteImportAnchor> Anchors;
	static FSimpleMulticastDelegate OnAnchorListChange;
};

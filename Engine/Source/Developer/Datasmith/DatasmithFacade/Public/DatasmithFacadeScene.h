// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith SDK.
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneExporter.h"

// Datasmith facade classes.
class FDatasmithFacadeActor;
class FDatasmithFacadeBaseMaterial;
class FDatasmithFacadeElement;
class FDatasmithFacadeMesh;
class FDatasmithFacadeMetaData;
class FDatasmithFacadeTexture;


class DATASMITHFACADE_API FDatasmithFacadeScene
{
public:

	// Copy from EDatasmithActorRemovalRule
	enum class EActorRemovalRule : uint32
	{
		/** Remove also the actors children */
		RemoveChildren,

		/** Keeps current relative transform as the relative transform to the new parent. */
		KeepChildrenAndKeepRelativeTransform,
	};

	FDatasmithFacadeScene(
		const TCHAR* InApplicationHostName,      // name of the host application used to build the scene
		const TCHAR* InApplicationVendorName,    // vendor name of the application used to build the scene
		const TCHAR* InApplicationProductName,   // product name of the application used to build the scene
		const TCHAR* InApplicationProductVersion // product version of the application used to build the scene
	);

	// Collect an element for the Datasmith scene to build.
	void AddActor(
		FDatasmithFacadeActor* InActorPtr // Datasmith scene element
	);

	int32 GetActorsCount() const;

	FDatasmithFacadeActor* GetNewActor(
		int32 ActorIndex
	);

	void RemoveActor(
		FDatasmithFacadeActor* InActorPtr,
		EActorRemovalRule RemovalRule = EActorRemovalRule::RemoveChildren
	);

	void AddMaterial(
		FDatasmithFacadeBaseMaterial* InMaterialPtr
	);

	/**
	 * Returns the number of material elements added to the scene.
	 */
	int32 GetMaterialsCount() const;

	/**
	 * Returns a new FDatasmithFacadeBaseMaterial pointing to the Material at the specified index.
	 * If the given index is invalid, the returned value is nullptr.
	 * 
	 * @param MaterialIndex The index of the material in the scene.
	 */
	FDatasmithFacadeBaseMaterial* GetNewMaterial(
		int32 MaterialIndex
	);

	/**
	 * Removes a Material Element from the scene.
	 *
	 * @param InMaterialPtr the Material Element to remove
	 */
	void RemoveMaterial(
		FDatasmithFacadeBaseMaterial* InMaterialPtr
	);

	void AddMesh(
		FDatasmithFacadeMesh* InMeshPtr
	);

	void AddTexture(
		FDatasmithFacadeTexture* InTexturePtr
	);

	int32 GetTexturesCount() const;

	/**
	 *	Returns a new FDatasmithFacadeTexture pointing to the Texture at the specified index.
	 *	If the given index is invalid, the returned value is nullptr.
	 */
	FDatasmithFacadeTexture* GetNewTexture(
		int32 TextureIndex
	);

	void RemoveTexture(
		FDatasmithFacadeTexture* InTexturePtr
	);

	void AddMetaData(
		FDatasmithFacadeMetaData* InMetaDataPtr
	);

	int32 GetMetaDataCount() const;

	/**
	 *	Returns a new FDatasmithFacadeMetaData pointing to the MetaData at the specified index.
	 *	If the given index is invalid, the returned value is nullptr.
	 */
	FDatasmithFacadeMetaData* GetNewMetaData(
		int32 MetaDataIndex
	);

	void RemoveMetaData(
		FDatasmithFacadeMetaData* InMetaDataPtr
	);
	
	/** Instantiate an exporter and register export start time */
	void PreExport();

	/** 
	 * Manually shutdown the Facade and close down all the core engine systems and release the resources they allocated
	 * You don't need to call this function on Windows platform.
	 */
	static void Shutdown();

	/** Build and export a Datasmith scene instance and its scene element assets. */
	void ExportScene(
		const TCHAR* InOutputPath // Datasmith scene output file path
	);

	/** 
	 * Build and export the Datasmith scene element assets.
	 * This must be done before building a Datasmith scene instance.
	 */
	void ExportAssets(
		const TCHAR* InAssetFolder // Datasmith asset folder path
	);

	/** Build a Datasmith scene instance. */
	void BuildScene(
		const TCHAR* InSceneName // Datasmith scene name
	);


	/**
	 * Set the Datasmith scene's label.
	 * This is mainly used in conjunction with DirectLink. The scene's label is used
	 * to name the source (stream) created to broadcast the content of the scene
	 */
	void SetLabel(
		const TCHAR* InSceneLabel
	);

	/** Return the Datasmith scene's label. */
	const TCHAR* GetLabel() const;


#ifdef SWIG_FACADE
protected:
#endif
	
	// Return the build Datasmith scene instance.
	TSharedRef<IDatasmithScene> GetScene() const;

	// Return the list of texture names added to the current datasmith scene.
	TSharedRef<TSet<FString>> GetExportedTextures() const;

private:

	// Name of the host application used to build the scene.
	FString ApplicationHostName;

	// Vendor name of the application used to build the scene.
	FString ApplicationVendorName;

	// Product name of the application used to build the scene.
	FString ApplicationProductName;

	// Product version of the application used to build the scene.
	FString ApplicationProductVersion;

	// Array of collected elements for the Datasmith scene to build.
	TSet<TSharedPtr<FDatasmithFacadeElement>> SceneElementSet;

	// Datasmith scene instance built with the collected elements.
	TSharedRef<IDatasmithScene> SceneRef;

	// Datasmith scene exporter
	TSharedPtr<FDatasmithSceneExporter> SceneExporterRef;

	// Set of texture name to make sure we don't export duplicates.
	TSharedRef<TSet<FString>> ExportedTextureSet;

	// Indicates if a clean up is required or not.
	// Always true except on an export as the SceneExporter is performing the clean up
	bool bCleanUpNeeded;
};

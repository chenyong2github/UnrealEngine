// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef _MELANGE_SDK_

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "MeshDescription.h"
#include "Stats/Stats.h"

#include "DatasmithC4DMelangeSDKEnterGuard.h"
#include "c4d.h"
#include "c4d_browsecontainer.h"
#include "DatasmithC4DMelangeSDKLeaveGuard.h"

class FDatasmithSceneExporter;
class IDatasmithActorElement;
class IDatasmithCameraActorElement;
class IDatasmithLevelSequenceElement;
class IDatasmithLightActorElement;
class IDatasmithMasterMaterialElement;
class IDatasmithMeshActorElement;
class IDatasmithMeshElement;
class IDatasmithMetaDataElement;
class IDatasmithScene;
class IDatasmithTextureElement;
class UDatasmithC4DImportOptions;
class UDatasmithStaticMeshImportData;
enum class EDatasmithTextureMode : uint8;
struct FCraneCameraAttributes;
struct FDatasmithImportContext;
struct FRichCurve;

DECLARE_STATS_GROUP(TEXT("C4DImporter"), STATGROUP_C4DImporter, STATCAT_Advanced);

DECLARE_LOG_CATEGORY_EXTERN(LogDatasmithC4DImport, Log, All);

class FDatasmithC4DImporter
{
public:
	FDatasmithC4DImporter(TSharedRef<IDatasmithScene>& OutScene, UDatasmithC4DImportOptions* InOptions);
	~FDatasmithC4DImporter();

	/** Updates the used import options to InOptions */
	void SetImportOptions(UDatasmithC4DImportOptions* InOptions);

	/** Open and load a .4cd file into C4dDocument */
	bool OpenFile(const FString& InFilename);

	/** Parse the scene contained in the previously opened file and process its content according to parameters from incoming context */
	bool ProcessScene();

	/** Unload melange resources after importing */
	void UnloadScene();

	/** Finds the most derived cache for a melange object. That will be e.g. a polygon cache or a deformed polygon cache, if it has one */
	melange::BaseObject* GetBestMelangeCache(melange::BaseObject* Object);

	/** Generates a unique identifier string for a melange object based on the object's name and its position in the hierarchy */
	FString MelangeObjectID(melange::BaseObject* Object);

	/** Searches the melange object hierarchy for a melange::BaseObject that has a MelangeObjectID equal to SearchObjectID */
	melange::BaseObject* FindMelangeObject(const FString& SearchObjectID, melange::BaseObject* Object);

	/** Fetch the object corresponding to the position encoded in HierarchyPosition, starting from Object */
	melange::BaseObject* GoToMelangeHierarchyPosition(melange::BaseObject* Object, const FString& HierarchyPosition);

	/** Gets all melange objects that are part of the hierarchy of InstanceRoot. Used to identify child hierarchies of Oinstance objects */
	const TArray<melange::BaseObject*>& GetMelangeInstanceObjects(melange::BaseObject* InstanceRoot);

	/**
	 * Marks actors children of EmitterObject as ParticleActors, so that they can receive an artificial visibility animation track to
	 * emulate the look of the particle spawning and despawning
	 */
	void MarkActorsAsParticles(melange::BaseObject* EmitterObject, melange::BaseObject* EmittersCache);

	/**
	 * Import melange objects into Datasmith elements. Assets like meshes, materials and textures are added to the Datasmith scene directly,
	 * while actors are merely returned and must be added as children to scene actors or added to the scene manually.
	 */
	TSharedPtr<IDatasmithActorElement> ImportNullActor(melange::BaseObject* Object, const FString& DatasmithName, const FString& DatasmithLabel);
	TSharedPtr<IDatasmithLightActorElement> ImportLight(melange::BaseObject* LightObject, const FString& DatasmithName, const FString& DatasmithLabel);
	TSharedPtr<IDatasmithCameraActorElement> ImportCamera(melange::BaseObject* CameraObject, const FString& DatasmithName, const FString& DatasmithLabel);
	TSharedPtr<IDatasmithMeshActorElement> ImportPolygon(melange::PolygonObject* PolyObject, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& DatasmithName, const FString& DatasmithLabel, const TArray<melange::TextureTag*>& TextureTags);
	TSharedPtr<IDatasmithMasterMaterialElement> ImportMaterial(melange::Material* InC4DMaterialPtr);
	TSharedPtr<IDatasmithTextureElement> ImportTexture(const FString& TexturePath, EDatasmithTextureMode TextureMode);
	TSharedPtr<IDatasmithMeshElement> ImportMesh(melange::PolygonObject* PolyObject, const FString& DatasmithMeshName, const FString& DatasmithLabel, const TArray<melange::TextureTag*>& TextureTags);

	/** Parses the spline and its cache into SplineCurves so that it can be used as paths for animation later */
	void ImportSpline(melange::SplineObject* ActorObject);

	/** Traverse the melange material hierarchy contained in the c4d file and import each into IDatasmithMasterMaterialElements */
	void ImportMaterialHierarchy(melange::BaseMaterial* InC4DMaterialPtr);

	/** Uses ActorElementToC4DObject to find the corresponding melange object for ActorElement and adds all of its animations to LevelSequence */
	void ImportAnimations(TSharedPtr<IDatasmithActorElement> ActorElement);

	/** Traverse the Datasmith scene's IDatasmithActorElement hierarchy and import all animations for the corresponding melange actors*/
	void ImportActorHierarchyAnimations(TSharedPtr<IDatasmithActorElement> ActorElement);

	/** Searches for the first valid texture used by BaseShader */
	FString GetBaseShaderTextureFilePath(melange::BaseList2D* BaseShader);

	/**
	 * Generates a new copy of the IDatasmithMasterMaterialElement with name InMaterialID and alter its properties to match values retrieved
	 * from InTextureTag, and adds the new material to the Datasmith scene. This is used because texture tags are closer to material
	 * instances, and may have different "overrides" for each property
	 */
	FString CustomizeMaterial(const FString& InMaterialID, const FString& InMeshID, melange::TextureTag* InTextureTag);

	/** Imports a melange actor, which might involve parsing another small hierarchy of subnodes and deformers*/
	TSharedPtr<IDatasmithActorElement> ImportObjectAndChildren(melange::BaseObject* ActorObject, melange::BaseObject* DataObject, TSharedPtr<IDatasmithActorElement> ParentActor, const melange::Matrix& WorldTransformMatrix, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& InstancePath, TArray<melange::BaseObject*>* InstanceObjects, TArray<melange::TextureTag*> TextureTags, const FString& DatasmithLabel);

	/** Traverse the melange actor hierarchy importing all nodes */
	void ImportHierarchy(melange::BaseObject* ActorObject, melange::BaseObject* DataObject, TSharedPtr<IDatasmithActorElement> ParentActor, const melange::Matrix& WorldTransformMatrix, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& InstancePath, TArray<melange::BaseObject*>* InstanceObjects, const TArray<melange::TextureTag*>& TextureTags);

	/**
	 * Adds Actor as a child of ParentActor using the corresponding WorldTransformMatrix. Object is the corresponding melange Object to Actor.
	 * This will also do the necessary coordinate system conversions between melange and Unreal.
	 */
	void AddChildActor(melange::BaseObject* Object, TSharedPtr<IDatasmithActorElement> ParentActor, melange::Matrix WorldTransformMatrix, const TSharedPtr<IDatasmithActorElement>& Actor);

	/** Creates a new IDatasmithMetaDataElement for the IDatasmithActorElement, adds it to the scene and returns it */
	TSharedPtr<IDatasmithMetaDataElement> CreateMetadataForActor(const IDatasmithActorElement& Actor);

	void GetGeometriesForMeshElementAndRelease(const TSharedRef<IDatasmithMeshElement> MeshElement, TArray<FMeshDescription>& OutMeshDescriptions);
	TSharedPtr<IDatasmithLevelSequenceElement> GetLevelSequence() { return LevelSequence; }

	melange::BaseDocument* C4dDocument = nullptr;
	FString C4dDocumentFilename;

private:
	/** Removes from Context->Scene all empty actors that have a single child */
	void RemoveEmptyActors();

	/** Storage of FMeshDescriptions until they're retrieved by GetGeometriesForMeshElement */
	TMap<IDatasmithMeshElement*, FMeshDescription> MeshElementToMeshDescription;

	/** Storage of imported spline data to be used exclusively for importing animations that follow spline paths */
	TMap<melange::SplineObject*, TArray<FRichCurve>> SplineCurves;

	/** Storage of created materials used by CustomizeMaterial to create new "material instances" */
	TMap<FString, TSharedPtr<IDatasmithMasterMaterialElement>> MaterialNameToMaterialElement;

	/** Cache to prevent us from importing the mesh from a melange::PolygonObject more than once */
	TMap<melange::PolygonObject*, TSharedRef<IDatasmithMeshElement>> PolygonObjectToMeshElement;

	/** Cache to prevent us from importing the same texture in the same mode more than once (mode is encoded in the FString as well) */
	TMap<FString, TSharedPtr<IDatasmithTextureElement>> ImportedTextures;

	/** Storage of all parsed actors from the melange document, used so we can import all animations afterwards */
	TMap<IDatasmithActorElement*, melange::BaseObject*> ActorElementToC4DObject;

	/** Cache caches all objects that are children of instance objects i.e. caches all instanced objects */
	TMap<melange::BaseObject*, TArray<melange::BaseObject*>> InstancesObjectsMap;

	/** Keeps track of the owners of every melange cache object so we can climb the hierarchy upwards */
	TMap<melange::BaseObject*, melange::BaseObject*> CachesOriginalObject;

	/** Caches all the existing actor metadata elements we created for each IDatasmithActorElement in our Datasmith scene */
	TMap<const IDatasmithActorElement*, TSharedPtr<IDatasmithMetaDataElement>> ActorMetadata;

	/** Stores all FCraneCameraAttributes for each camera */
	TMap<melange::BaseObject*, TSharedRef<FCraneCameraAttributes>> CraneCameraToAttributes;

	/** Melange actors that are actually baked 'mesh particles' and need to receive an extra visibility track in ImportAnimations */
	TSet<melange::BaseObject*> ParticleActors;

	/** Caches to make sure we don't have any actor name collisions */
	TSet<FString> NamesOfAllActors;

	/** Names of IDatasmithActorElements that shouldn't be removed when optimizing the scene */
	TSet<FString> NamesOfCameraTargetActors;
	TSet<FString> NamesOfAnimatedActors;

	/** Where all actor animations are imported into when parsing the scene */
	TSharedPtr<IDatasmithLevelSequenceElement> LevelSequence;

	/** Chosen import options from the import options dialog*/
	UDatasmithC4DImportOptions* Options;

	/** Output Datasmith scene */
	TSharedRef<IDatasmithScene> DatasmithScene;

	/** Can be used to also export the imported scene in a .udatasmith format during import */
	TSharedPtr<FDatasmithSceneExporter> SceneExporterRef;
};

#endif // _MELANGE_SDK_
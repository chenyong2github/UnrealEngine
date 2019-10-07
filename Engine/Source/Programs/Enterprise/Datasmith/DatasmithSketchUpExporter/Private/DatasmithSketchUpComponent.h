// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/defs.h"
#include "SketchUpAPI/transformation.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FDatasmithSketchUpMesh;
class IDatasmithActorElement;
class IDatasmithScene;


class FDatasmithSketchUpComponent
{
public:

	// Initialize the dictionary of component definitions.
	static void InitComponentDefinitionMap(
		SUModelRef InSModelRef // model containing SketchUp component definitions
	);

	// Clear the dictionary of component definitions.
	static void ClearComponentDefinitionMap();

	FDatasmithSketchUpComponent(
		SUModelRef InSModelRef // source SketchUp model
	);

	// Convert the SketchUp component entities into a hierarchy of Datasmith actors.
	void ConvertEntities(
		int32                              InComponentDepth,       // current depth of the component in the SketckUp model hierarchy
		SUTransformation const&            InSWorldTransform,      // world transform of the SketchUp component entities parent
		SULayerRef                         InSInheritedLayerRef,   // SketchUp inherited layer
		int32                              InSInheritedMaterialID, // SketchUp inherited material ID
		TSharedRef<IDatasmithScene>        IODSceneRef,            // Datasmith scene to populate
		TSharedPtr<IDatasmithActorElement> IODComponentActorPtr    // Datasmith actor for the component
	);

private:

	// Get the component ID of a SketckUp component definition.
	static int32 GetComponentID(
		SUComponentDefinitionRef InSComponentDefinitionRef // valid SketckUp component definition
	);

	// Get the component persistent ID of a SketckUp component instance.
	static int64 GetComponentPID(
		SUComponentInstanceRef InSComponentInstanceRef // valid SketckUp component instance
	);

	FDatasmithSketchUpComponent(
		SUComponentDefinitionRef InSComponentDefinitionRef // source SketchUp component definition
	);

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpComponent(FDatasmithSketchUpComponent const&) = delete;
	FDatasmithSketchUpComponent& operator=(FDatasmithSketchUpComponent const&) = delete;

	// Add the source SketchUp entities geometry to the baked component meshes.
	void BakeEntities(
		SULayerRef InSInheritedLayerRef // SketchUp inherited layer
	);

	// Convert a SketchUp component instance into a hierarchy of Datasmith actors.
	void ConvertInstance(
		int32                              InComponentDepth,        // current depth of the component in the SketckUp model hierarchy
		SUTransformation const&            InSWorldTransform,       // world transform of the SketchUp component instance parent
		SULayerRef                         InSEffectiveLayerRef,    // SketchUp effective layer
		int32                              InSInheritedMaterialID,  // SketchUp inherited material ID
		SUComponentInstanceRef             InSComponentInstanceRef, // SketchUp component instance to convert
		TSharedRef<IDatasmithScene>        IODSceneRef,             // Datasmith scene to populate
		TSharedPtr<IDatasmithActorElement> IODComponentActorPtr     // Datasmith actor for the component
	);

	// Return the effective layer of a SketckUp component instance.
	SULayerRef GetEffectiveLayer(
		SUComponentInstanceRef InSComponentInstanceRef, // valid SketckUp component instance
		SULayerRef             InSInheritedLayerRef     // SketchUp inherited layer
	) const;

	// Return whether or not a SketckUp component instance is visible in the current SketchUp scene.
	bool IsVisible(
		SUComponentInstanceRef InSComponentInstanceRef, // valid SketckUp component instance
		SULayerRef             InSEffectiveLayerRef     // SketchUp component instance effective layer
	) const;

	// Retrieve a component definition in the dictionary of component definitions.
	TSharedPtr<FDatasmithSketchUpComponent> GetComponentDefinition(
		SUComponentInstanceRef InSComponentInstanceRef // SketchUp component instance
	) const;

	// Set the world transform of a Datasmith actor.
	void SetActorTransform(
		TSharedPtr<IDatasmithActorElement> IODActorPtr,      // Datasmith actor to transform
		SUTransformation const&            InSWorldTransform // SketchUp world transform to apply
	) const;

private:

	// Dictionary of component definitions indexed by the SketchUp component IDs.
	static TMap<int32, TSharedPtr<FDatasmithSketchUpComponent>> ComponentDefinitionMap;

	// Source SketchUp component entities.
	SUEntitiesRef SSourceEntitiesRef;

	// Source SketchUp component ID.
	int32 SSourceID;

	// Source SketchUp component IFC GUID (22 character string).
	// Reference: http://www.buildingsmart-tech.org/implementation/get-started/ifc-guid
	FString SSourceGUID;

	// Source SketchUp component name.
	FString SSourceName;

	// Number of component instances in the source SketchUp entities.
	size_t SSourceComponentInstanceCount;

	// Number of groups in the source SketchUp entities.
	size_t SSourceGroupCount;

	// Whether or not the source SketchUp component behaves like a billboard, always presenting a 2D surface perpendicular to the direction of camera.
	bool bSSourceFaceCamera;

	// Whether or not the source SketchUp entities geometry was added to the baked component mesh.
	bool bBakeEntitiesDone;

	// Baked component meshes combining the faces of the SketchUp component definition.
	TArray<TSharedPtr<FDatasmithSketchUpMesh>> BakedMeshes;
};

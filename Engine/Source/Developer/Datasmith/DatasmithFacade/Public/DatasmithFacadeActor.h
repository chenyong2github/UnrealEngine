// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"


class DATASMITHFACADE_API FDatasmithFacadeActor :
	public FDatasmithFacadeElement
{
public:

	FDatasmithFacadeActor(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeActor() {}

	// Prevent the Datasmith actor from being removed by optimization.
	void KeepActor();

	// Set the world transform of the Datasmith actor.
	void SetWorldTransform(
		const float* InWorldMatrix // Datasmith actor world transform matrix
	);

	// Set the layer of the Datasmith actor.
	void SetLayer(
		const TCHAR* InLayerName // Datasmith actor layer name
	);

	// Add a new tag to the Datasmith actor.
	void AddTag(
		const TCHAR* InTag // Datasmith actor tag
	);

	// Add a metadata boolean property to the Datasmith actor.
	void AddMetadataBoolean(
		const TCHAR* InPropertyName,  // property name
		bool         bInPropertyValue // property value
	);

	// Add a metadata sRGBA color property to the Datasmith actor.
	void AddMetadataColor(
		const TCHAR*  InPropertyName, // color property name
		unsigned char InR,            // red
		unsigned char InG,            // green
		unsigned char InB,            // blue
		unsigned char InA             // alpha
	);

	// Add a metadata float property to the Datasmith actor.
	void AddMetadataFloat(
		const TCHAR* InPropertyName, // property name
		float        InPropertyValue // property value
	);

	// Add a metadata string property to the Datasmith actor.
	virtual void AddMetadataString(
		const TCHAR* InPropertyName, // property name
		const TCHAR* InPropertyValue // property value
	);

	// Add a metadata texture property to the Datasmith actor.
	void AddMetadataTexture(
		const TCHAR* InPropertyName,   // texture property name
		const TCHAR* InTextureFilePath // texture file path
	);

	// Add a metadata vector property to the Datasmith actor.
	void AddMetadataVector(
		const TCHAR* InPropertyName, // property name
		const TCHAR* InPropertyValue // property value
	);

	// Set whether or not the Datasmith actor is a component when used in a hierarchy.
	void SetIsComponent(
		bool bInIsComponent
	);

	// Add a new child to the Datasmith actor.
	void AddChild(
		FDatasmithFacadeActor* InChildActorPtr // Datasmith child actor
	);

	// Make sure all the actor names are unique in the hierarchy of Datasmith actor children.
	void SanitizeActorHierarchyNames();

#ifdef SWIG_FACADE
protected:
#endif

	// Convert a source matrix into a Datasmith actor transform.
	FTransform ConvertTransform(
		const float* InSourceMatrix
	) const;

	// Add a new child to the Datasmith actor.
	void AddChild(
		TSharedPtr<FDatasmithFacadeActor> InChildActorPtr // Datasmith child actor
	);

	// Return the optimized Datasmith actor.
	virtual TSharedPtr<FDatasmithFacadeElement> Optimize(
		TSharedPtr<FDatasmithFacadeElement> InElementPtr,           // this Datasmith actor
		bool                                bInNoSingleChild = true // remove intermediate Datasmith actors having a single child
	);

	// Build a Datasmith actor element and add it to the Datasmith scene.
	virtual void BuildScene(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) override;

	// Create and initialize a Datasmith actor hierarchy.
	virtual TSharedPtr<IDatasmithActorElement> CreateActorHierarchy(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) const;

	// Set the properties of a Datasmith actor.
	void SetActorProperties(
		TSharedRef<IDatasmithScene>        IOSceneRef, // Datasmith scene
		TSharedPtr<IDatasmithActorElement> IOActorPtr  // Datasmith actor element
	) const;

	// Add the hierarchy of children to a Datasmith actor
	// or to the Datasmith scene when a null actor pointer is provided.
	void AddActorChildren(
		TSharedRef<IDatasmithScene>        IOSceneRef, // Datasmith scene
		TSharedPtr<IDatasmithActorElement> IOActorPtr  // Datasmith actor element
	) const;

	// Get the Datasmith actor children.
	const TArray<TSharedPtr<FDatasmithFacadeActor>> GetActorChildren() const;

protected:

	// Datasmith actor world transform.
	FTransform WorldTransform;

private:

	// Datasmith actor layer name.
	FString LayerName;

	// Array of Datasmith actor tags.
	TArray<FString> TagArray;

	// Array of Datasmith metadata properties.
	TArray<TSharedPtr<IDatasmithKeyValueProperty>> MetadataPropertyArray;

	// Whether or not the Datasmith actor is a component when used in a hierarchy.
	bool bIsComponent;

	// Array of Datasmith actor children.
	TArray<TSharedPtr<FDatasmithFacadeActor>> ChildActorArray;

	// Whether or not the Datasmith actor can be removed by optimization.
	bool bOptimizeActor;
};

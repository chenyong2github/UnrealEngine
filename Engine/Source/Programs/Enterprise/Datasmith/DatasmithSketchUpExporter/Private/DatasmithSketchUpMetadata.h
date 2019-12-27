// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/defs.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class IDatasmithMetaDataElement;


class FDatasmithSketchUpMetadata
{
public:

	// Add a new entry into the dictionary of metadata definitions.
	static void AddMetadataDefinition(
		SUModelRef InSModelRef // source SketchUp model
	);

	// Add a new entry into the dictionary of metadata definitions.
	static void AddMetadataDefinition(
		SUComponentDefinitionRef InSComponentDefinitionRef // valid SketckUp component definition
	);

	// Add a new entry into the dictionary of metadata definitions.
	static void AddMetadataDefinition(
		SUComponentInstanceRef InSComponentInstanceRef // valid SketckUp component instance
	);

	// Clear the dictionary of metadata definitions.
	static void ClearMetadataDefinitionMap();

	// Create a Datasmith metadata element for a SketckUp component instance metadata definition.
	static TSharedPtr<IDatasmithMetaDataElement> CreateMetadataElement(
		SUComponentInstanceRef InSComponentInstanceRef, // valid SketckUp component instance
		FString const&         InMetadataElementName    // metadata element name sanitized for Datasmith
	);

private:

	static int32 const MODEL_METADATA_ID = 0;

private:

	// Add a new entry into the dictionary of metadata definitions.
	static void AddMetadataDefinition(
		SUEntityRef InSEntityRef // valid SketckUp entity
	);

	FDatasmithSketchUpMetadata(
		SUModelRef InSModelRef // source SketchUp model
	);

	FDatasmithSketchUpMetadata(
		SUEntityRef InSEntityRef // valid SketckUp entity
	);

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpMetadata(FDatasmithSketchUpMetadata const&) = delete;
	FDatasmithSketchUpMetadata& operator=(FDatasmithSketchUpMetadata const&) = delete;

	// Retrieve the key-value pairs of a SketchUp attribute dictionary.
	void ScanAttributeDictionary(
		SUAttributeDictionaryRef InSAttributeDictionaryRef // valid SketchUp attribute dictionary
	);

	// Retrieve the key-value pairs of a SketchUp classification schema.
	void ScanClassificationSchema(
		SUClassificationAttributeRef InSSchemaAttributeRef // valid SketchUp classification schema attribute
	);

	// Get a string representation of a SketchUp attribute value.
	FString GetAttributeValue(
		SUTypedValueRef InSTypedValueRef // valid SketchUp attribute value
	);

	// Return whether or not the dictionary of metadata contains key-value pairs.
	bool ContainsMetadata() const;

	// Add the metadata key-value pairs into a Datasmith metadata element.
	void AddMetadata(
		TSharedPtr<IDatasmithMetaDataElement> IODMetaDataElementPtr // Datasmith metadata element to populate
	) const;

private:

	// Set of names of the interesting SketchUp attribute dictionaries.
	static TSet<FString> const InterestingAttributeDictionarySet;

	// Dictionary of metadata definitions indexed by the SketchUp metadata IDs.
	static TMap<int32, TSharedPtr<FDatasmithSketchUpMetadata>> MetadataDefinitionMap;

	// Source SketchUp metadata ID.
	int32 SSourceID;

	// Dictionary of metadata key-value pairs.
	TMap<FString, FString> MetadataKeyValueMap;
};


inline bool FDatasmithSketchUpMetadata::ContainsMetadata() const
{
	return (MetadataKeyValueMap.Num() > 0);
}

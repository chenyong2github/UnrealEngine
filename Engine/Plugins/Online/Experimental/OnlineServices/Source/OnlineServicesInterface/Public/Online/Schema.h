// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/SchemaTypes.h"

namespace UE::Online {

class FSchemaRegistry;

class ONLINESERVICESINTERFACE_API FSchemaCategoryInstance
{
public:
	// DerivedSchemaId may remain unset in two situations:
	// 1. DerivedSchemaId will be populated by attributes from a search result. In this scenario
	//    the base schema is required to have an attribute flagged as SchemaCompatibilityId so that
	//    the derived schema can be discovered.
	// 2. Schema swapping is not enabled. All attributes are defined in the base schema.
	FSchemaCategoryInstance(
		const FSchemaId& DerivedSchemaId,
		const FSchemaId& BaseSchemaId,
		const FSchemaCategoryId& CategoryId,
		const TSharedRef<const FSchemaRegistry>& SchemaRegistry);

#if 0
	TOnlineResult<FSchemaCategoryInstanceChangeSchema> ChangeSchema(FSchemaCategoryInstanceChangeSchema::Params&& Params);
#endif
	TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta> ApplyApplicationDelta(FSchemaCategoryInstanceApplyApplicationDelta::Params&& Params);
	TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot> ApplyServiceSnapshot(FSchemaCategoryInstanceApplyServiceSnapshot::Params&& Params);
	// Todo: ApplyServiceDelta

	TSharedPtr<const FSchemaDefinition> GetDerivedDefinition() const { return DerivedSchemaDefinition; };
	TSharedPtr<const FSchemaDefinition> GetBaseDefinition() const { return BaseSchemaDefinition; };

	// Check whether the schema is valid.
	bool IsValid() const;

	// Check whether an application attribute is valid in the base schema.
	// Intended to be used when a schema is not backed by a service.
	bool IsValidBaseAttributeData(const FSchemaAttributeId& Id, const FSchemaVariant& Data);

private:
	bool InitializeSchemaDefinition(
		const FSchemaId& SchemaId,
		const FSchemaCategoryId& CategoryId,
		TSharedPtr<const FSchemaDefinition>* OutSchemaDefinition,
		const FSchemaCategoryDefinition** OutSchemaCategoryDefinition);

	bool GetSerializationSchema(
		const FSchemaDefinition** OutSchemaDefinition,
		const FSchemaCategoryDefinition** OutSchemaCategoryDefinition) const;

	TSharedRef<const FSchemaRegistry> SchemaRegistry;
	TSharedPtr<const FSchemaDefinition> DerivedSchemaDefinition;
	TSharedPtr<const FSchemaDefinition> BaseSchemaDefinition;
	const FSchemaCategoryDefinition* DerivedSchemaCategoryDefinition = nullptr;
	const FSchemaCategoryDefinition* BaseSchemaCategoryDefinition = nullptr;
	int64 LastSentSchemaCompatibilityId = 0;

	// Snapshot of application data as provided by the service.
	TMap<FSchemaAttributeId, FSchemaVariant> ApplicationDataServiceSnapshot;
};

class ONLINESERVICESINTERFACE_API FSchemaRegistry
{
public:
	// Parse the loaded config structures.
	bool ParseConfig(const FSchemaRegistryDescriptorConfig& Config);

	TSharedPtr<const FSchemaDefinition> GetDefinition(const FSchemaId& SchemaId) const;
	TSharedPtr<const FSchemaDefinition> GetDefinition(int64 CompatibilityId) const;
	bool IsSchemaChildOf(const FSchemaId& SchemaId, const FSchemaId& ParentSchemaId) const;

private:

	TMap<FSchemaId, TSharedRef<const FSchemaDefinition>> SchemaDefinitionsById;
	TMap<int64, TSharedRef<const FSchemaDefinition>> SchemaDefinitionsByCompatibilityId;
};

/* UE::Online */ }

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/SchemaTypes.h"

namespace UE::Online {

class FSchemaRegistry;

class ONLINESERVICESINTERFACE_API FSchemaCategoryInstance
{
public:
	FSchemaCategoryInstance(
		const FSchemaId& SchemaId,
		const FSchemaId& BaseSchemaId,
		const FSchemaCategoryId& CategoryId,
		const TSharedRef<const FSchemaRegistry>& Registry);

#if 0 // todo
	TOnlineResult<FSchemaCategoryInstanceChangeSchema> ChangeSchema(FSchemaCategoryInstanceChangeSchema::Params&& Params);
#endif
	TOnlineResult<FSchemaCategoryInstanceApplyApplicationChanges> ApplyChanges(FSchemaCategoryInstanceApplyApplicationChanges::Params&& Changes);
	TOnlineResult<FSchemaCategoryInstanceApplyServiceChanges> ApplyChanges(FSchemaCategoryInstanceApplyServiceChanges::Params&& Changes);

	TSharedPtr<const FSchemaDefinition> GetDefinition() const { return SchemaDefinition; };

	// Check whether the category instance initialized successfully.
	bool IsValid() const;

	// Check whether an application attribute is valid in the schema category.
	// Intended to verify attributes when there is no backing service for a schema.
	bool IsValidAttributeData(const FSchemaAttributeId& Id, const FSchemaVariant& Data);

private:
	TSharedPtr<const FSchemaDefinition> SchemaDefinition;
	const FSchemaCategoryDefinition* SchemaCategoryDefinition = nullptr;

	// Cache of data in application form. Updates from the application may only be part of a
	// service field, so the values need to be cached so that the service field can rebuilt when
	// changes occur.
	TMap<FSchemaAttributeId, FSchemaVariant> AppDataSnapshot;
};

class ONLINESERVICESINTERFACE_API FSchemaRegistry
{
public:
	// Parse the loaded config structures.
	bool ParseConfig(const FSchemaRegistryDescriptorConfig& Config);

	TSharedPtr<const FSchemaDefinition> GetDefinition(const FSchemaId& SchemaId) const;
	bool IsSchemaChildOf(const FSchemaId& SchemaId, const FSchemaId& ParentSchemaId) const;

private:

	TMap<FSchemaId, TSharedRef<const FSchemaDefinition>> SchemaDefinitions;
};

/* UE::Online */ }

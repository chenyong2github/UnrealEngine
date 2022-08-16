// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Schema.h"
#include "Algo/Find.h"
#include "Algo/ForEach.h"

DEFINE_LOG_CATEGORY_STATIC(LogOnlineSchema, Log, All);

namespace UE::Online {

template <typename T>
T BuildFlagsFromArray(const TArray<T>& Input)
{
	T OutValue = T::None;
	Algo::ForEach(Input, [&](T FlagValue){ OutValue |= FlagValue; });
	return OutValue;
}

bool FSchemaRegistry::ParseConfig(const FSchemaRegistryDescriptorConfig& Config)
{
	bool ParsedSuccessfully = true;

	// Populate known schema.
	// Validation will be handled later due to the dependency that schema has on service descriptors.
	TMap<FSchemaId, const FSchemaDescriptor*> KnownSchemaDescriptors;
	for (const FSchemaDescriptor& SchemaDescriptor : Config.SchemaDescriptors)
	{
		// Check that schema descriptor id has not already been used.
		if (KnownSchemaDescriptors.Find(SchemaDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema descriptor found: %s"), *SchemaDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		KnownSchemaDescriptors.Add(SchemaDescriptor.Id, &SchemaDescriptor);
	}

	// Verify and populate known service descriptors.
	TMap<FSchemaId, const FSchemaServiceDescriptor*> KnownSchemaServiceDescriptors;
	for (const FSchemaServiceDescriptor& SchemaServiceDescriptor : Config.ServiceDescriptors)
	{
		// Check that schema service descriptor id has not already been used.
		if (KnownSchemaServiceDescriptors.Find(SchemaServiceDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema service descriptor found: %s"), *SchemaServiceDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		// Verify attributes
		TSet<FSchemaServiceAttributeId> SeenAttributes;
		for (const FSchemaServiceAttributeDescriptor& SchemaServiceAttributeDescriptor : SchemaServiceDescriptor.Attributes)
		{
			// Check that attribute ids are not reused.
			if (SeenAttributes.Find(SchemaServiceAttributeDescriptor.Id))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: Attribute id has already been used."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
				continue;
			}
			SeenAttributes.Add(SchemaServiceAttributeDescriptor.Id);

			ESchemaServiceAttributeSupportedTypeFlags SupportedTypes = BuildFlagsFromArray(SchemaServiceAttributeDescriptor.SupportedTypes);

			// Check that supported type has been set.
			if (SupportedTypes == ESchemaServiceAttributeSupportedTypeFlags::None)
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: A valid supported type must be selected."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
			}

			// Check that variable length data has a set maximum length.
			if (EnumHasAnyFlags(SupportedTypes, ESchemaServiceAttributeSupportedTypeFlags::String) && SchemaServiceAttributeDescriptor.MaxSize <= 0)
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: A valid max size must be set for variable length data."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
			}
		}

		KnownSchemaServiceDescriptors.Add(SchemaServiceDescriptor.Id, &SchemaServiceDescriptor);
	}

	// Verify schema data.
	for (const TPair<FSchemaId, const FSchemaDescriptor*>& SchemaDescriptorPair : KnownSchemaDescriptors)
	{
		const FSchemaDescriptor& SchemaDescriptor = *SchemaDescriptorPair.Get<1>();

		// Verify parent schema.
		if (SchemaDescriptor.ParentId.IsValid())
		{
			TSet<FSchemaId> SeenSchema;
			const FSchemaDescriptor* BaseParentSchemaDescriptor = nullptr;
			const FSchemaDescriptor* TestSchemaDescriptor = &SchemaDescriptor;
			while (TestSchemaDescriptor)
			{
				// Check for circular dependencies in schema.
				if (SeenSchema.Find(TestSchemaDescriptor->Id))
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Circular parent dependency found in schema: %s"), *SchemaDescriptor.Id.ToString().ToLower());
					ParsedSuccessfully = false;
					break;
				}
				SeenSchema.Add(TestSchemaDescriptor->Id);

				// Iterate parent.
				if (TestSchemaDescriptor->ParentId != FSchemaId())
				{
					// Check that parent schema exists.
					if (const FSchemaDescriptor** FoundDescriptor = KnownSchemaDescriptors.Find(TestSchemaDescriptor->ParentId))
					{
						TestSchemaDescriptor = *FoundDescriptor;
						BaseParentSchemaDescriptor = TestSchemaDescriptor;
					}
					else
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Parent schema %s does not exist."),
							*SchemaDescriptor.Id.ToString().ToLower(),
							*TestSchemaDescriptor->ParentId.ToString().ToLower());
						ParsedSuccessfully = false;
						TestSchemaDescriptor = nullptr;
					}
				}
				else
				{
					TestSchemaDescriptor = nullptr;
				}
			}

			// verify that all categories used in schema exist in base parent schema.
			if (BaseParentSchemaDescriptor)
			{
				for (const FSchemaCategoryDescriptor& SchemaCategoryDescriptor : SchemaDescriptor.Categories)
				{
					bool bBaseCategoryExists = Algo::FindByPredicate(BaseParentSchemaDescriptor->Categories,
					[&SchemaCategoryDescriptor](const FSchemaCategoryDescriptor& Descriptor)
					{
						return Descriptor.Id == SchemaCategoryDescriptor.Id;
					}) != nullptr;

					if (!bBaseCategoryExists)
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Category %s does not exist in base parent %s schema."),
							*SchemaDescriptor.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*BaseParentSchemaDescriptor->Id.ToString().ToLower());
							ParsedSuccessfully = false;
					}
				}
			}
		}

		// Verify category descriptors.
		TSet<FSchemaCategoryId> SeenSchemaCategoryIds;
		TSet<FSchemaServiceDescriptorId> SeenSchemaServiceDescriptorIds;
		for (const FSchemaCategoryDescriptor& SchemaCategoryDescriptor : SchemaDescriptor.Categories)
		{
			if (SeenSchemaCategoryIds.Find(SchemaCategoryDescriptor.Id))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Category id has already been used."),
					*SchemaDescriptor.Id.ToString().ToLower(),
					*SchemaCategoryDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
				continue;
			}
			SeenSchemaCategoryIds.Add(SchemaCategoryDescriptor.Id);

			// Verify that service descriptor is valid.
			if (SchemaCategoryDescriptor.ServiceDescriptorId != FSchemaServiceDescriptorId())
			{
				// Verify that only a base parent set service descriptor.
				if (SchemaDescriptor.ParentId != FSchemaId())
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Service descriptor id may only be set by a base parent schmea category."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.ServiceDescriptorId.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				// Verify that service descriptor exists.
				if (KnownSchemaServiceDescriptors.Find(SchemaCategoryDescriptor.ServiceDescriptorId) == nullptr)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Service descriptor id %s not found."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.ServiceDescriptorId.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				// Verify that the service descriptor is not reused between categories.
				if (SeenSchemaServiceDescriptorIds.Find(SchemaCategoryDescriptor.ServiceDescriptorId) != nullptr)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Service descriptor id %s has already been used by another category."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.ServiceDescriptorId.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				SeenSchemaServiceDescriptorIds.Add(SchemaCategoryDescriptor.ServiceDescriptorId);
			}

			// Verify category attributes.
			TSet<FSchemaServiceAttributeId> SeenAttributes;
			FSchemaAttributeId SeenSchemaCompatibilityAttributeId;
			for (const FSchemaAttributeDescriptor& SchemaAttributeDescriptor : SchemaCategoryDescriptor.Attributes)
			{
				// Check that attribute ids are not reused.
				if (SeenAttributes.Find(SchemaAttributeDescriptor.Id))
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: Attribute id has already been used."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaAttributeDescriptor.Id.ToString().ToLower());
					ParsedSuccessfully = false;
					continue;
				}
				SeenAttributes.Add(SchemaAttributeDescriptor.Id);

				// Check that variable length data has a set maximum length.
				if (SchemaAttributeDescriptor.Type == ESchemaAttributeType::String && SchemaAttributeDescriptor.MaxSize <= 0)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: A valid max size must be set for variable length data."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaAttributeDescriptor.Id.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				ESchemaAttributeFlags AttributeFlags = BuildFlagsFromArray(SchemaAttributeDescriptor.Flags);

				// Check that searchable attributes only exist in a base schema.
				if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::Searchable) && SchemaDescriptor.ParentId != FSchemaId())
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: Searchable fields may only exist in the base schema."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaAttributeDescriptor.Id.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				// Check that schema compatibility attribute only exists in a base schema and used only once and that it is the Int64 type.
				if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::SchemaCompatibilityId))
				{
					if (SchemaDescriptor.ParentId != FSchemaId())
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: SchemaCompatibilityId field may only exist in the base schema."),
							*SchemaDescriptor.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*SchemaAttributeDescriptor.Id.ToString().ToLower());
						ParsedSuccessfully = false;
					}

					if (SeenSchemaCompatibilityAttributeId != FSchemaId())
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: SchemaCompatibilityId field has already been used by attribute %s."),
							*SchemaDescriptor.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*SchemaAttributeDescriptor.Id.ToString().ToLower(),
							*SeenSchemaCompatibilityAttributeId.ToString().ToLower());
						ParsedSuccessfully = false;
					}
					else
					{
						SeenSchemaCompatibilityAttributeId = SchemaAttributeDescriptor.Id;
					}

					if (SchemaAttributeDescriptor.Type != ESchemaAttributeType::Int64)
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: SchemaCompatibilityId field may only be set to Int64."),
							*SchemaDescriptor.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*SchemaAttributeDescriptor.Id.ToString().ToLower());
						ParsedSuccessfully = false;
					}
				}

				// Check that group id is valid.
				if (SchemaAttributeDescriptor.UpdateGroupId < 0)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: UpdateGroupId must be set to either 0 or a positive integer value."),
						*SchemaDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaAttributeDescriptor.Id.ToString().ToLower());
					ParsedSuccessfully = false;
				}
			}
		}
	}

	// Build definitions only when there are no parsing errors.
	TMap<FSchemaId, TSharedRef<const FSchemaDefinition>> ParsedSchemaDefinitions;
	if (ParsedSuccessfully)
	{
		// Build schema definitions
		TSet<uint32> SeenSchemaCrcs;
		for (const TPair<FSchemaId, const FSchemaDescriptor*>& SchemaDescriptorPair : KnownSchemaDescriptors)
		{
			uint32 SchemaDataCrc = 0;
			const FSchemaDescriptor& SchemaDescriptor = *SchemaDescriptorPair.Get<1>();

			TSharedRef<FSchemaDefinition> SchemaDefinition = MakeShared<FSchemaDefinition>();
			ParsedSchemaDefinitions.Add(SchemaDescriptor.Id, SchemaDefinition);
			SchemaDefinition->Id = SchemaDescriptor.Id;

			TMap<FSchemaCategoryId, FSchemaServiceDescriptorId> ServiceDescriptors;

			// Populate definition from descriptor
			for (const FSchemaDescriptor** IterSchemaDescriptor = KnownSchemaDescriptors.Find(SchemaDescriptor.Id);
				IterSchemaDescriptor != nullptr;
				IterSchemaDescriptor = KnownSchemaDescriptors.Find((*IterSchemaDescriptor)->ParentId))
			{
				const FSchemaDescriptor& CurrentSchemaDescriptor = **IterSchemaDescriptor;

				// Populate parent info.
				if (CurrentSchemaDescriptor.Id != SchemaDescriptor.Id)
				{
					SchemaDefinition->ParentSchemaIds.Add(CurrentSchemaDescriptor.Id);
					SchemaDataCrc = FCrc::TypeCrc32(CurrentSchemaDescriptor.Id, SchemaDataCrc);
				}

				// Populate attribute definitions.
				for (const FSchemaCategoryDescriptor& CurrentSchemaCategoryDescriptor : CurrentSchemaDescriptor.Categories)
				{
					FSchemaCategoryDefinition& SchemaCategoryDefinition = SchemaDefinition->Categories.FindOrAdd(CurrentSchemaCategoryDescriptor.Id);
					SchemaDataCrc = FCrc::TypeCrc32(CurrentSchemaCategoryDescriptor.Id, SchemaDataCrc);

					for (const FSchemaAttributeDescriptor& CurrentSchemaAttributeDescriptor : CurrentSchemaCategoryDescriptor.Attributes)
					{
						FSchemaAttributeDefinition& AttributeDefinition = SchemaCategoryDefinition.SchemaAttributeDefinitions.Add(CurrentSchemaAttributeDescriptor.Id);
						AttributeDefinition.Id = CurrentSchemaAttributeDescriptor.Id;
						AttributeDefinition.Flags = BuildFlagsFromArray(CurrentSchemaAttributeDescriptor.Flags);
						AttributeDefinition.Type = CurrentSchemaAttributeDescriptor.Type;
						AttributeDefinition.MaxSize = CurrentSchemaAttributeDescriptor.MaxSize;
						SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Id, SchemaDataCrc);
						SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Flags, SchemaDataCrc);
						SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Type, SchemaDataCrc);
						SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.MaxSize, SchemaDataCrc);
					}

					// Store service descriptor for assignment.
					if (CurrentSchemaCategoryDescriptor.ServiceDescriptorId != FSchemaServiceDescriptorId())
					{
						ServiceDescriptors.Add(CurrentSchemaCategoryDescriptor.Id, CurrentSchemaCategoryDescriptor.ServiceDescriptorId);
					}
				}
			}

			// Assign attributes to service attributes.
			for (TPair<FSchemaCategoryId, FSchemaServiceDescriptorId>& ServiceDescriptor : ServiceDescriptors)
			{
				FSchemaCategoryDefinition& SchemaCategoryDefinition = *SchemaDefinition->Categories.Find(ServiceDescriptor.Get<0>());
				const FSchemaServiceDescriptor& SchemaServiceDescriptor = **KnownSchemaServiceDescriptors.Find(ServiceDescriptor.Get<1>());

				// todo: pack multiple attributes into a service attribute.
				// For the time being schema attributes and service attributes have a 1:1 relationship.

				// todo
			}

			// Build compatibility id.
			{
				const uint32 SchemaIdCrc = FCrc::TypeCrc32(SchemaDefinition->Id);
				if (SeenSchemaCrcs.Find(SchemaIdCrc) != nullptr)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Hash collision processing schema name, please rename."),
						*SchemaDefinition->Id.ToString().ToLower());
					ParsedSuccessfully = false;
				}
				SeenSchemaCrcs.Add(SchemaIdCrc);

				const uint64 CompatibilityIdUint = (static_cast<uint64>(SchemaIdCrc) << 32) | SchemaDataCrc;
				SchemaDefinition->CompatibilityId = *reinterpret_cast<const int64*>(&CompatibilityIdUint);
			}
		}
	}

	if (ParsedSuccessfully)
	{
		SchemaDefinitions = MoveTemp(ParsedSchemaDefinitions);
		return true;
	}
	else
	{
		return false;
	}
}

FSchemaCategoryInstance::FSchemaCategoryInstance(
	const FSchemaId& SchemaId,
	const FSchemaId& BaseSchemaId,
	const FSchemaCategoryId& CategoryId,
	const TSharedRef<const FSchemaRegistry>& Registry)
{
	if ((SchemaDefinition = Registry->GetDefinition(SchemaId)))
	{
		if (!Registry->IsSchemaChildOf(SchemaId, BaseSchemaId))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("SchemaCategoryInstance init error: Schema %s is not a child of %s"),
				*SchemaId.ToString().ToLower(),
				*BaseSchemaId.ToString().ToLower());
		}
		else
		{
			SchemaCategoryDefinition = SchemaDefinition->Categories.Find(CategoryId);
			if (SchemaCategoryDefinition == nullptr)
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("SchemaCategoryInstance init error: Unable to find category %s in schema %s"),
					*SchemaId.ToString().ToLower());
			}
		}
	}
	else
	{
		UE_LOG(LogOnlineSchema, Error, TEXT("SchemaCategoryInstance init error: Unable to find definition for schema %s"),
			*SchemaId.ToString().ToLower());
	}
}

TOnlineResult<FSchemaCategoryInstanceApplyApplicationChanges> FSchemaCategoryInstance::ApplyChanges(FSchemaCategoryInstanceApplyApplicationChanges::Params&& Changes)
{
	// todo: handle packing multiple attributes into a service attribute.

	// todo
	return TOnlineResult<FSchemaCategoryInstanceApplyApplicationChanges>(FSchemaCategoryInstanceApplyApplicationChanges::Result({}));
}

TOnlineResult<FSchemaCategoryInstanceApplyServiceChanges> FSchemaCategoryInstance::ApplyChanges(FSchemaCategoryInstanceApplyServiceChanges::Params&& Changes)
{
	// todo: handle unpacking a service attribute into multiple attributes.

	// todo
	return TOnlineResult<FSchemaCategoryInstanceApplyServiceChanges>(FSchemaCategoryInstanceApplyServiceChanges::Result({}));
}

bool FSchemaCategoryInstance::IsValid() const
{
	return SchemaCategoryDefinition != nullptr;
}

bool FSchemaCategoryInstance::IsValidAttributeData(const FSchemaAttributeId& Id, const FSchemaVariant& Data)
{
	if (ensure(IsValid()))
	{
		if (const FSchemaAttributeDefinition* AttributeDefinition = SchemaCategoryDefinition->SchemaAttributeDefinitions.Find(Id))
		{
			if (AttributeDefinition->Type == Data.GetType())
			{
				return true;
			}
			else
			{
				UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidAttributeData] Schema attribute %s.%s.%s set with invalid type %s. Expected type %s"),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower(),
					*LexToString(Data.GetType()),
					LexToString(AttributeDefinition->Type));
				return false;
			}
		}
		else
		{
			UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidAttributeData] Attribute %s not found in schema definition %s.%s"),
				*Id.ToString().ToLower(),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower());
			return false;
		}
	}
	else
	{
		UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidAttributeData] Unable to set attribute %s. Schema is not valid"),
			*Id.ToString().ToLower());
		return false;
	}
}

TSharedPtr<const FSchemaDefinition> FSchemaRegistry::GetDefinition(const FSchemaId& SchemaId) const
{
	const TSharedRef<const FSchemaDefinition>* Definition = SchemaDefinitions.Find(SchemaId);
	return Definition ? *Definition : TSharedPtr<const FSchemaDefinition>();
}

bool FSchemaRegistry::IsSchemaChildOf(const FSchemaId& SchemaId, const FSchemaId& ParentSchemaId) const
{
	const FSchemaId* FoundSchemaId = nullptr;
	if (const TSharedRef<const FSchemaDefinition>* Definition = SchemaDefinitions.Find(SchemaId))
	{
		FoundSchemaId = (* Definition)->ParentSchemaIds.Find(ParentSchemaId);
	}
	return FoundSchemaId != nullptr;
}

/* UE::Online */ }

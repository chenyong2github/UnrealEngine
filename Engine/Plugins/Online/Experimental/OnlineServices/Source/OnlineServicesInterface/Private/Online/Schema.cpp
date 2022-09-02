// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Schema.h"

#include "Algo/Find.h"
#include "Algo/ForEach.h"
#include "Algo/Reverse.h"
#include "Algo/Transform.h"
#include "Online/OnlineErrorDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogOnlineSchema, Log, All);

namespace UE::Online {

template <typename T>
T BuildFlagsFromArray(const TArray<T>& Input)
{
	T OutValue = T::None;
	Algo::ForEach(Input, [&](T FlagValue){ OutValue |= FlagValue; });
	return OutValue;
}

ESchemaServiceAttributeSupportedTypeFlags TranslateAttributeType(ESchemaAttributeType InType)
{
	switch (InType)
	{
	case ESchemaAttributeType::Bool:
		return ESchemaServiceAttributeSupportedTypeFlags::Bool;
	case ESchemaAttributeType::Int64:
		return ESchemaServiceAttributeSupportedTypeFlags::Int64;
	case ESchemaAttributeType::Double:
		return ESchemaServiceAttributeSupportedTypeFlags::Double;
	case ESchemaAttributeType::String:
		return ESchemaServiceAttributeSupportedTypeFlags::String;
	default:
		checkNoEntry();
		// Intentional fall-through.
	case ESchemaAttributeType::None:
		return ESchemaServiceAttributeSupportedTypeFlags::None;
	}
}

ESchemaServiceAttributeFlags TranslateAttributeFlags(ESchemaAttributeFlags InFlags)
{
	ESchemaServiceAttributeFlags OutFlags = ESchemaServiceAttributeFlags::None;
	OutFlags |= EnumHasAnyFlags(InFlags, ESchemaAttributeFlags::Public) ? ESchemaServiceAttributeFlags::Public : ESchemaServiceAttributeFlags::None;
	OutFlags |= EnumHasAnyFlags(InFlags, ESchemaAttributeFlags::Private) ? ESchemaServiceAttributeFlags::Private : ESchemaServiceAttributeFlags::None;
	OutFlags |= EnumHasAnyFlags(InFlags, ESchemaAttributeFlags::Searchable) ? ESchemaServiceAttributeFlags::Searchable : ESchemaServiceAttributeFlags::None;
	return OutFlags;
}

bool FSchemaRegistry::ParseConfig(const FSchemaRegistryDescriptorConfig& Config)
{
	bool ParsedSuccessfully = true;

	// Populate known service attributes
	TMap<FSchemaServiceAttributeId, const FSchemaServiceAttributeDescriptor*> KnownSchemaServiceAttributeDescriptors;
	for (const FSchemaServiceAttributeDescriptor& SchemaServiceAttributeDescriptor : Config.ServiceAttributeDescriptors)
	{
		// Check that schema descriptor id has not already been used.
		if (KnownSchemaServiceAttributeDescriptors.Contains(SchemaServiceAttributeDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema service attribute descriptor found: %s"), *SchemaServiceAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		KnownSchemaServiceAttributeDescriptors.Add(SchemaServiceAttributeDescriptor.Id, &SchemaServiceAttributeDescriptor);
	}

	// Verify and populate known service descriptors.
	TMap<FSchemaId, const FSchemaServiceDescriptor*> KnownSchemaServiceDescriptors;
	for (const FSchemaServiceDescriptor& SchemaServiceDescriptor : Config.ServiceDescriptors)
	{
		// Check that schema service descriptor id has not already been used.
		if (KnownSchemaServiceDescriptors.Contains(SchemaServiceDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema service descriptor found: %s"), *SchemaServiceDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		// Verify attributes
		TSet<FSchemaServiceAttributeId> SeenAttributes;
		for (const FSchemaServiceAttributeId& SchemaServiceAttributeId : SchemaServiceDescriptor.AttributeIds)
		{
			// Check that attribute ids are not reused.
			if (SeenAttributes.Contains(SchemaServiceAttributeId))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: Attribute id has already been used."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeId.ToString().ToLower());
				ParsedSuccessfully = false;
				continue;
			}
			SeenAttributes.Add(SchemaServiceAttributeId);

			const FSchemaServiceAttributeDescriptor** SchemaServiceAttributeDescriptorPtr = KnownSchemaServiceAttributeDescriptors.Find(SchemaServiceAttributeId);
			if (!SchemaServiceAttributeDescriptorPtr)
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: Attribute does not exist."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeId.ToString().ToLower());
				ParsedSuccessfully = false;
				continue;
			}
			const FSchemaServiceAttributeDescriptor& SchemaServiceAttributeDescriptor = **SchemaServiceAttributeDescriptorPtr;
			const ESchemaServiceAttributeSupportedTypeFlags SupportedTypes = BuildFlagsFromArray(SchemaServiceAttributeDescriptor.SupportedTypes);

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

			// Check for valid attribute flags
			const ESchemaServiceAttributeFlags ServiceAttributeFlags = BuildFlagsFromArray(SchemaServiceAttributeDescriptor.Flags);
			if (!EnumHasAnyFlags(ServiceAttributeFlags, ESchemaServiceAttributeFlags::Public | ESchemaServiceAttributeFlags::Private))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema service attribute %s.%s: Either Public or Private visibility must be set."),
					*SchemaServiceDescriptor.Id.ToString().ToLower(),
					*SchemaServiceAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
			}
		}

		KnownSchemaServiceDescriptors.Add(SchemaServiceDescriptor.Id, &SchemaServiceDescriptor);
	}

	// Populate known schema categories
	TMap<FSchemaCategoryId, const FSchemaCategoryDescriptor*> KnownSchemaCategoryDescriptors;
	for (const FSchemaCategoryDescriptor& SchemaCategoryDescriptor : Config.SchemaCategoryDescriptors)
	{
		// Check that schema descriptor id has not already been used.
		if (KnownSchemaCategoryDescriptors.Contains(SchemaCategoryDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema category descriptor found: %s"), *SchemaCategoryDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}
		KnownSchemaCategoryDescriptors.Add(SchemaCategoryDescriptor.Id, &SchemaCategoryDescriptor);

		// Check that schema service descriptor exists
		if (SchemaCategoryDescriptor.ServiceDescriptorId != FSchemaServiceDescriptorId()
			&& !KnownSchemaServiceDescriptors.Contains(SchemaCategoryDescriptor.ServiceDescriptorId))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s: Service descriptor id %s not found."),
				*SchemaCategoryDescriptor.Id.ToString().ToLower(),
				*SchemaCategoryDescriptor.ServiceDescriptorId.ToString().ToLower());
			ParsedSuccessfully = false;
		}
	}

	// Populate known schema. Verify what can be verified statically. Heirarchy validation is performed below.
	TMap<FSchemaId, const FSchemaDescriptor*> KnownSchemaDescriptors;
	for (const FSchemaDescriptor& SchemaDescriptor : Config.SchemaDescriptors)
	{
		// Check that schema descriptor id has not already been used.
		if (KnownSchemaDescriptors.Contains(SchemaDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema descriptor found: %s"), *SchemaDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}
		KnownSchemaDescriptors.Add(SchemaDescriptor.Id, &SchemaDescriptor);

		// Parent must declare at least one category
		if (SchemaDescriptor.ParentId == FSchemaId() && SchemaDescriptor.CategoryIds.IsEmpty())
		{
			// Error, base type must define at least one category
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Base schema must define at least one category."),
				*SchemaDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}

		// Children must not declare categories
		if (SchemaDescriptor.ParentId != FSchemaId() && !SchemaDescriptor.CategoryIds.IsEmpty())
		{
			// Error child cannot define categories
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Child schema cannot define categories."),
				*SchemaDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}

		TSet<FSchemaCategoryId> SeenCategoryIds;
		for (const FSchemaCategoryId& CategoryId : SchemaDescriptor.CategoryIds)
		{
			if (SeenCategoryIds.Contains(CategoryId))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Category id has already been used."),
					*SchemaDescriptor.Id.ToString().ToLower(),
					*CategoryId.ToString().ToLower());
				ParsedSuccessfully = false;
				continue;
			}
			SeenCategoryIds.Emplace(CategoryId);

			if (!KnownSchemaCategoryDescriptors.Contains(CategoryId))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Category does not exist."),
					*SchemaDescriptor.Id.ToString().ToLower(),
					*CategoryId.ToString().ToLower());
				ParsedSuccessfully = false;
			}
		}
	}

	// Populate known schema attributes
	TMap<FSchemaAttributeId, const FSchemaAttributeDescriptor*> KnownSchemaAttributeDescriptors;
	for (const FSchemaAttributeDescriptor& SchemaAttributeDescriptor : Config.SchemaAttributeDescriptors)
	{
		// Check that schema descriptor id has not already been used.
		if (KnownSchemaAttributeDescriptors.Contains(SchemaAttributeDescriptor.Id))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema attribute descriptor found: %s"), *SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}
		KnownSchemaAttributeDescriptors.Emplace(SchemaAttributeDescriptor.Id, &SchemaAttributeDescriptor);
		
		// Check that a type has been set
		if (SchemaAttributeDescriptor.Type == ESchemaAttributeType::None)
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: A valid type must be set."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}

		// Check that variable length data has a set maximum length.
		if (SchemaAttributeDescriptor.Type == ESchemaAttributeType::String && SchemaAttributeDescriptor.MaxSize <= 0)
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: A valid max size must be set for variable length data."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}

		// Check that group id is valid.
		if (SchemaAttributeDescriptor.UpdateGroupId < 0)
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: UpdateGroupId must be set to either 0 or a positive integer value."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}
		
		const ESchemaAttributeFlags AttributeFlags = BuildFlagsFromArray(SchemaAttributeDescriptor.Flags);

		if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::SchemaCompatibilityId))
		{
			if (SchemaAttributeDescriptor.Type != ESchemaAttributeType::Int64)
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: SchemaCompatibilityId field may only be set to Int64."),
					*SchemaAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
			}
		
			if (EnumHasAnyFlags(AttributeFlags, ESchemaAttributeFlags::Private))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: SchemaCompatibilityId attribute may not be private."),
					*SchemaAttributeDescriptor.Id.ToString().ToLower());
				ParsedSuccessfully = false;
			}
		}
		
		// Check that public or private visibility has been set and is valid.
		if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::Public | ESchemaAttributeFlags::Private))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: Public and Private visibility are mutually exclusive."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}
		if (!EnumHasAnyFlags(AttributeFlags, ESchemaAttributeFlags::Public | ESchemaAttributeFlags::Private))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: Either Public or Private visibility must be set."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}
		if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::Searchable | ESchemaAttributeFlags::Private))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s: Searchable attributes may not be private."),
				*SchemaAttributeDescriptor.Id.ToString().ToLower());
			ParsedSuccessfully = false;
		}
	}

	// Populate known schema category attributes 
	using FCategoryToAttributesMap = TMap<FSchemaCategoryId, TArray<FSchemaAttributeId>>;
	using FSchemaToCategoryAttributesMap = TMap<FSchemaId, FCategoryToAttributesMap>;
	FSchemaToCategoryAttributesMap KnownSchemaCategoryAttributes;
	for (const FSchemaCategoryAttributesDescriptor& SchemaCategoryAttributesDescriptor : Config.SchemaCategoryAttributeDescriptors)
	{
		// Check that the id has not already been used.
		FCategoryToAttributesMap* FoundSchema = KnownSchemaCategoryAttributes.Find(SchemaCategoryAttributesDescriptor.SchemaId);
		if (FoundSchema && FoundSchema->Contains(SchemaCategoryAttributesDescriptor.CategoryId))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Duplicate schema additional category attributes descriptor found: %s.%s"),
				*SchemaCategoryAttributesDescriptor.SchemaId.ToString().ToLower(),
				*SchemaCategoryAttributesDescriptor.CategoryId.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		// Check that the schema exists
		if (!KnownSchemaDescriptors.Contains(SchemaCategoryAttributesDescriptor.SchemaId))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid additional category attributes %s.%s: Schema does not exist"),
				*SchemaCategoryAttributesDescriptor.SchemaId.ToString().ToLower(),
				*SchemaCategoryAttributesDescriptor.CategoryId.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		// Check that the category exists
		if (!KnownSchemaCategoryDescriptors.Contains(SchemaCategoryAttributesDescriptor.CategoryId))
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("Invalid additional category attributes %s.%s: Category does not exist"),
				*SchemaCategoryAttributesDescriptor.SchemaId.ToString().ToLower(),
				*SchemaCategoryAttributesDescriptor.CategoryId.ToString().ToLower());
			ParsedSuccessfully = false;
			continue;
		}

		bool bAttributesParsedSuccessfully = true;
		TSet<FSchemaAttributeId> SeenAttributeIds;
		for (const FSchemaAttributeId& AttributeId : SchemaCategoryAttributesDescriptor.AttributeIds)
		{
			if (SeenAttributeIds.Contains(AttributeId))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid category attributes %s.%s: Attribute %s duplicated"),
					*SchemaCategoryAttributesDescriptor.SchemaId.ToString().ToLower(),
					*SchemaCategoryAttributesDescriptor.CategoryId.ToString().ToLower(),
					*AttributeId.ToString().ToLower());
				bAttributesParsedSuccessfully = false;
				continue;
			}
			SeenAttributeIds.Emplace(AttributeId);

			if (!KnownSchemaAttributeDescriptors.Contains(AttributeId))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Invalid category attributes %s.%s: Attribute %s does not exist"),
					*SchemaCategoryAttributesDescriptor.SchemaId.ToString().ToLower(),
					*SchemaCategoryAttributesDescriptor.CategoryId.ToString().ToLower(),
					*AttributeId.ToString().ToLower());
				bAttributesParsedSuccessfully = false;
				continue;
			}
		}
		if (!bAttributesParsedSuccessfully)
		{
			ParsedSuccessfully = false;
			continue;
		}

		if (!FoundSchema)
		{
			FoundSchema = &KnownSchemaCategoryAttributes.Emplace(SchemaCategoryAttributesDescriptor.SchemaId);
		}
		FoundSchema->Emplace(SchemaCategoryAttributesDescriptor.CategoryId, SchemaCategoryAttributesDescriptor.AttributeIds);
	}

	// Verify schema data
	for (const TPair<FSchemaId, const FSchemaDescriptor*>& SchemaDescriptorPair : KnownSchemaDescriptors)
	{
		const FSchemaDescriptor& SchemaDescriptorToVerify = *SchemaDescriptorPair.Value;

		// Stack of schemas, will push in tail->root order and then reverse to root->tail order.
		TArray<FSchemaId> SchemaHierarchy;

		// Verify hierarchy.
		const FSchemaDescriptor* TestSchemaDescriptor = &SchemaDescriptorToVerify;
		while (TestSchemaDescriptor)
		{
			// Check for circular dependencies in schema.
			if (SchemaHierarchy.Contains(TestSchemaDescriptor->Id))
			{
				UE_LOG(LogOnlineSchema, Error, TEXT("Circular parent dependency found in schema: %s"), *SchemaDescriptorToVerify.Id.ToString().ToLower());
				ParsedSuccessfully = false;
				break;
			}
			SchemaHierarchy.Push(TestSchemaDescriptor->Id);

			if (TestSchemaDescriptor->ParentId != FSchemaId())
			{
				// Move onto next layer in the hierarchy
				if (const FSchemaDescriptor** ParentDescriptor = KnownSchemaDescriptors.Find(TestSchemaDescriptor->ParentId))
				{
					TestSchemaDescriptor = *ParentDescriptor;
				}
				else
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: Parent schema %s does not exist."),
						*SchemaDescriptorToVerify.Id.ToString().ToLower(),
						*TestSchemaDescriptor->ParentId.ToString().ToLower());
					ParsedSuccessfully = false;
					break;
				}
			}
			else
			{
				TestSchemaDescriptor = nullptr;
			}
		}

		// Reverse the stack so it's in root->tail order
		Algo::Reverse(SchemaHierarchy);
		check(SchemaHierarchy.Num() > 0);
		const FSchemaId RootSchemaId = SchemaHierarchy[0];
		const FSchemaDescriptor& RootSchemaDescriptor = *KnownSchemaDescriptors.FindChecked(RootSchemaId);

		// Verify categories
		TSet<FSchemaServiceDescriptorId> SeenSchemaServiceDescriptorIds;
		for (const FSchemaCategoryId& SchemaCategoryId : RootSchemaDescriptor.CategoryIds)
		{
			const FSchemaCategoryDescriptor** SchemaCategoryDescriptorPtr = KnownSchemaCategoryDescriptors.Find(SchemaCategoryId);
			if (!SchemaCategoryDescriptorPtr)
			{
				// Already reported in schema population checks above.
				continue;
			}
			const FSchemaCategoryDescriptor& SchemaCategoryDescriptor = **SchemaCategoryDescriptorPtr;

			// Verify that service descriptor is valid.
			if (SchemaCategoryDescriptor.ServiceDescriptorId != FSchemaServiceDescriptorId())
			{
				// Verify that the service descriptor is not reused between categories.
				if (SeenSchemaServiceDescriptorIds.Find(SchemaCategoryDescriptor.ServiceDescriptorId) != nullptr)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema category %s.%s: Service descriptor id %s has already been used by another category."),
						*SchemaDescriptorToVerify.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.Id.ToString().ToLower(),
						*SchemaCategoryDescriptor.ServiceDescriptorId.ToString().ToLower());
					ParsedSuccessfully = false;
				}

				SeenSchemaServiceDescriptorIds.Add(SchemaCategoryDescriptor.ServiceDescriptorId);
			}

			// Verify category attributes.
			TSet<FSchemaServiceAttributeId> SeenAttributes;
			FSchemaAttributeId SeenSchemaCompatibilityAttributeId;
			TSet<FSchemaAttributeId> SchemaCategoryAttributeIds;
			// Iterate down the stack, checking attributes as we go
			for (const FSchemaId SchemaHierarchyLayerId : SchemaHierarchy)
			{
				const FCategoryToAttributesMap* FoundSchemaCategories = KnownSchemaCategoryAttributes.Find(SchemaHierarchyLayerId);
				const TArray<FSchemaAttributeId>* FoundCategoryAttributes = FoundSchemaCategories ? FoundSchemaCategories->Find(SchemaCategoryId) : nullptr;
				if (!FoundCategoryAttributes)
				{
					// This layer doesn't add any attribs for this category.
					continue;
				}

				for (const FSchemaAttributeId& SchemaAttributeId : *FoundCategoryAttributes)
				{
					// Check that attribute ids are not reused.
					if (SeenAttributes.Find(SchemaAttributeId))
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: Attribute id has already been used."),
							*SchemaDescriptorToVerify.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*SchemaAttributeId.ToString().ToLower());
						ParsedSuccessfully = false;
						continue;
					}
					SeenAttributes.Add(SchemaAttributeId);

					const FSchemaAttributeDescriptor** SchemaAttributeDescriptorPtr = KnownSchemaAttributeDescriptors.Find(SchemaAttributeId);
					if (!SchemaAttributeDescriptorPtr)
					{
						// Already reported in the category attribute population checks above.
						continue;
					}
					const FSchemaAttributeDescriptor& SchemaAttributeDescriptor = **SchemaAttributeDescriptorPtr;
					const ESchemaAttributeFlags AttributeFlags = BuildFlagsFromArray(SchemaAttributeDescriptor.Flags);

					// Check that searchable attributes only exist in a base schema.
					if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::Searchable) && SchemaHierarchyLayerId != RootSchemaId)
					{
						UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: Searchable fields may only exist in the base schema."),
							*SchemaDescriptorToVerify.Id.ToString().ToLower(),
							*SchemaCategoryDescriptor.Id.ToString().ToLower(),
							*SchemaAttributeDescriptor.Id.ToString().ToLower());
						ParsedSuccessfully = false;
					}

					// Check that schema compatibility attribute only exists in a base schema and used only once and that it is the Int64 type.
					if (EnumHasAllFlags(AttributeFlags, ESchemaAttributeFlags::SchemaCompatibilityId))
					{
						if (SchemaHierarchyLayerId != RootSchemaId)
						{
							UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: SchemaCompatibilityId field may only exist in the base schema."),
								*SchemaDescriptorToVerify.Id.ToString().ToLower(),
								*SchemaCategoryDescriptor.Id.ToString().ToLower(),
								*SchemaAttributeDescriptor.Id.ToString().ToLower());
							ParsedSuccessfully = false;
						}

						if (SeenSchemaCompatibilityAttributeId != FSchemaId())
						{
							UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema attribute %s.%s.%s: SchemaCompatibilityId field has already been used by attribute %s."),
								*SchemaDescriptorToVerify.Id.ToString().ToLower(),
								*SchemaCategoryDescriptor.Id.ToString().ToLower(),
								*SchemaAttributeDescriptor.Id.ToString().ToLower(),
								*SeenSchemaCompatibilityAttributeId.ToString().ToLower());
							ParsedSuccessfully = false;
						}
						else
						{
							SeenSchemaCompatibilityAttributeId = SchemaAttributeDescriptor.Id;
						}
					}
				}
			}
		}
	}

	// Build definitions only when there are no parsing errors.
	TMap<FSchemaId, TSharedRef<const FSchemaDefinition>> ParsedSchemaDefinitions;
	TMap<int64, TSharedRef<const FSchemaDefinition>> ParsedSchemaDefinitionsByCompatibilityId;
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

			// Get the hierarchy from root->leaf
			TArray<FSchemaId> SchemaHierarchy;
			SchemaHierarchy.Emplace(SchemaDefinition->Id);
			for (const FSchemaDescriptor** IterSchemaDescriptor = KnownSchemaDescriptors.Find(SchemaDescriptor.ParentId);
				IterSchemaDescriptor != nullptr;
				IterSchemaDescriptor = KnownSchemaDescriptors.Find((*IterSchemaDescriptor)->ParentId))
			{
				const FSchemaId& IterSchemaId = (*IterSchemaDescriptor)->Id;
				SchemaHierarchy.Emplace(IterSchemaId);

				// Populate parent info.
				SchemaDefinition->ParentSchemaIds.Add(IterSchemaId);
				SchemaDataCrc = FCrc::TypeCrc32(IterSchemaId, SchemaDataCrc);
			}
			Algo::Reverse(SchemaHierarchy);
			check(SchemaHierarchy.Num() > 0);
			const FSchemaId RootSchemaId = SchemaHierarchy[0];
			const FSchemaDescriptor& RootSchemaDescriptor = *KnownSchemaDescriptors.FindChecked(RootSchemaId);

			// Add the category definitions.
			for (const FSchemaCategoryId& CategoryId : RootSchemaDescriptor.CategoryIds)
			{
				FSchemaCategoryDefinition& SchemaCategoryDefinition = SchemaDefinition->Categories.Emplace(CategoryId);
				SchemaCategoryDefinition.Id = CategoryId;

				SchemaDataCrc = FCrc::TypeCrc32(CategoryId, SchemaDataCrc);
			}

			// Iterate the hierarchy from root -> leaf
			for (const FSchemaId& SchemaHierarchyLayerId : SchemaHierarchy)
			{
				const FSchemaDescriptor& CurrentSchemaDescriptor = *KnownSchemaDescriptors.FindChecked(SchemaHierarchyLayerId);

				// Populate attribute definitions for each category
				for (const FSchemaCategoryId& CurrentSchemaCategoryId : RootSchemaDescriptor.CategoryIds)
				{
					if (const FCategoryToAttributesMap* FoundSchemaCategoryAttributes = KnownSchemaCategoryAttributes.Find(CurrentSchemaDescriptor.Id))
					{
						if (const TArray<FSchemaAttributeId>* FoundCategoryAttributeIds = FoundSchemaCategoryAttributes->Find(CurrentSchemaCategoryId))
						{
							const FSchemaCategoryDescriptor& CurrentSchemaCategoryDescriptor = *KnownSchemaCategoryDescriptors.FindChecked(CurrentSchemaCategoryId);
							FSchemaCategoryDefinition& SchemaCategoryDefinition = SchemaDefinition->Categories.FindChecked(CurrentSchemaCategoryDescriptor.Id);

							for (const FSchemaAttributeId& CurrentSchemaAttributeId : *FoundCategoryAttributeIds)
							{
								check(!SchemaCategoryDefinition.SchemaAttributeDefinitions.Contains(CurrentSchemaAttributeId));

								const FSchemaAttributeDescriptor& CurrentSchemaAttributeDescriptor = *KnownSchemaAttributeDescriptors.FindChecked(CurrentSchemaAttributeId);

								FSchemaAttributeDefinition& AttributeDefinition = SchemaCategoryDefinition.SchemaAttributeDefinitions.Emplace(CurrentSchemaAttributeDescriptor.Id);
								AttributeDefinition.Id = CurrentSchemaAttributeDescriptor.Id;
								AttributeDefinition.Flags = BuildFlagsFromArray(CurrentSchemaAttributeDescriptor.Flags);
								AttributeDefinition.Type = CurrentSchemaAttributeDescriptor.Type;
								AttributeDefinition.MaxSize = CurrentSchemaAttributeDescriptor.MaxSize;

								if (EnumHasAnyFlags(AttributeDefinition.Flags, ESchemaAttributeFlags::SchemaCompatibilityId))
								{
									SchemaCategoryDefinition.SchemaCompatibilityAttributeId = AttributeDefinition.Id;
								}

								SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Id, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Flags, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.Type, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(AttributeDefinition.MaxSize, SchemaDataCrc);
							}
						}
					}
				}
			}

			// Assign attributes to service attributes for each category
			for (const FSchemaCategoryId& CategoryId : RootSchemaDescriptor.CategoryIds)
			{
				FSchemaCategoryDefinition& SchemaCategoryDefinition = SchemaDefinition->Categories.FindChecked(CategoryId);

				const FSchemaCategoryDescriptor& SchemaCategoryDescriptor = *KnownSchemaCategoryDescriptors.FindChecked(CategoryId);
				if (SchemaCategoryDescriptor.ServiceDescriptorId != FSchemaServiceDescriptorId())
				{
					const FSchemaServiceDescriptor& SchemaServiceDescriptor = *KnownSchemaServiceDescriptors.FindChecked(SchemaCategoryDescriptor.ServiceDescriptorId);

					// todo: pack multiple attributes into a service attribute.
					// For the time being schema attributes and service attributes have a 1:1 relationship.

					// Bucket service attributes for assignment
					TMap<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags> PublicAttributes;
					TMap<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags> PrivateAttributes;
					TMap<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags> SearchableAttributes;

					// Build out buckets.
					for (const FSchemaServiceAttributeId& SchemaServiceAttributeId : SchemaServiceDescriptor.AttributeIds)
					{
						const FSchemaServiceAttributeDescriptor& SchemaServiceAttributeDescriptor = *KnownSchemaServiceAttributeDescriptors.FindChecked(SchemaServiceAttributeId);
						const ESchemaServiceAttributeSupportedTypeFlags SupportedTypes = BuildFlagsFromArray(SchemaServiceAttributeDescriptor.SupportedTypes);
						const ESchemaServiceAttributeFlags ServiceAttributeFlags = BuildFlagsFromArray(SchemaServiceAttributeDescriptor.Flags);
						if (EnumHasAnyFlags(ServiceAttributeFlags, ESchemaServiceAttributeFlags::Public))
						{
							PublicAttributes.Add(&SchemaServiceAttributeDescriptor, SupportedTypes);
						}
						if (EnumHasAnyFlags(ServiceAttributeFlags, ESchemaServiceAttributeFlags::Private))
						{
							PrivateAttributes.Add(&SchemaServiceAttributeDescriptor, SupportedTypes);
						}
						if (EnumHasAnyFlags(ServiceAttributeFlags, ESchemaServiceAttributeFlags::Searchable))
						{
							SearchableAttributes.Add(&SchemaServiceAttributeDescriptor, SupportedTypes);
						}
					}

					// Assign schema attributes based on buckets.
					for (const FSchemaId& SchemaHierarchyLayerId : SchemaHierarchy)
					{
						const FCategoryToAttributesMap* FoundSchemaCategoryAttributes = KnownSchemaCategoryAttributes.Find(SchemaHierarchyLayerId);
						const TArray<FSchemaAttributeId>* FoundCategoryAttributeIds = FoundSchemaCategoryAttributes ? FoundSchemaCategoryAttributes->Find(CategoryId) : nullptr;
						if (!FoundCategoryAttributeIds)
						{
							continue;
						}

						for (const FSchemaAttributeId& SchemaAttributeId : *FoundCategoryAttributeIds)
						{
							FSchemaAttributeDefinition& SchemaAttributeDefinition = SchemaCategoryDefinition.SchemaAttributeDefinitions.FindChecked(SchemaAttributeId);

							// Find a service attribute which can handle the schema attribute.
							TMap<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags>* Bucket = nullptr;
							if (EnumHasAnyFlags(SchemaAttributeDefinition.Flags, ESchemaAttributeFlags::Public))
							{
								Bucket = EnumHasAnyFlags(SchemaAttributeDefinition.Flags, ESchemaAttributeFlags::Searchable) ? &SearchableAttributes : &PublicAttributes;
							}
							else
							{
								Bucket = &PrivateAttributes;
							}

							const TPair<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags>* SchemaServiceAttributeDescriptorPair = Algo::FindByPredicate(*Bucket,
								[SchemaAttributeDefinition](const TPair<const FSchemaServiceAttributeDescriptor*, ESchemaServiceAttributeSupportedTypeFlags>& ServiceAttributePair)
							{
								const FSchemaServiceAttributeDescriptor* SchemaServiceAttributeDescriptor = ServiceAttributePair.Get<0>();
								const ESchemaServiceAttributeSupportedTypeFlags SchemaServiceAttributeSupportedTypeFlags = ServiceAttributePair.Get<1>();
								return EnumHasAnyFlags(SchemaServiceAttributeSupportedTypeFlags, TranslateAttributeType(SchemaAttributeDefinition.Type)) &&
									SchemaAttributeDefinition.MaxSize <= SchemaServiceAttributeDescriptor->MaxSize;
							});
							const FSchemaServiceAttributeDescriptor* FoundServiceAttributeDescriptor = SchemaServiceAttributeDescriptorPair ? SchemaServiceAttributeDescriptorPair->Get<0>() : nullptr;

							if (FoundServiceAttributeDescriptor)
							{
								// Todo: Reuse descriptor for assignment until it is full.

								// Remove found descriptor from buckets.
								PublicAttributes.Remove(FoundServiceAttributeDescriptor);
								PrivateAttributes.Remove(FoundServiceAttributeDescriptor);
								SearchableAttributes.Remove(FoundServiceAttributeDescriptor);

								// Create service attribute definition.
								FSchemaServiceAttributeDefinition& SchemaServiceAttributeDefinition = SchemaCategoryDefinition.ServiceAttributeDefinitions.Add(FoundServiceAttributeDescriptor->Id);
								SchemaServiceAttributeDefinition.Id = FoundServiceAttributeDescriptor->Id;
								SchemaServiceAttributeDefinition.Type = TranslateAttributeType(SchemaAttributeDefinition.Type);
								SchemaServiceAttributeDefinition.Flags = TranslateAttributeFlags(SchemaAttributeDefinition.Flags);
								SchemaServiceAttributeDefinition.MaxSize = FoundServiceAttributeDescriptor->MaxSize;
								SchemaServiceAttributeDefinition.SchemaAttributeIds.Add(SchemaAttributeDefinition.Id);
								SchemaAttributeDefinition.ServiceAttributeId = SchemaServiceAttributeDefinition.Id;

								if (EnumHasAnyFlags(SchemaAttributeDefinition.Flags, ESchemaAttributeFlags::SchemaCompatibilityId))
								{
									SchemaCategoryDefinition.SchemaCompatibilityServiceAttributeId = SchemaServiceAttributeDefinition.Id;
								}

								// Add service attribute definition to data crc.
								SchemaDataCrc = FCrc::TypeCrc32(SchemaServiceAttributeDefinition.Id, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(SchemaServiceAttributeDefinition.Type, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(SchemaServiceAttributeDefinition.Flags, SchemaDataCrc);
								SchemaDataCrc = FCrc::TypeCrc32(SchemaServiceAttributeDefinition.MaxSize, SchemaDataCrc);

								Algo::ForEach(SchemaServiceAttributeDefinition.SchemaAttributeIds, [&SchemaDataCrc](const FSchemaAttributeId& Id) { SchemaDataCrc = FCrc::TypeCrc32(Id, SchemaDataCrc); });
							}
							else
							{
								UE_LOG(LogOnlineSchema, Error, TEXT("Failed to find service attribute to fit schema attribute %s.%s.%s."),
									*SchemaDefinition->Id.ToString().ToLower(),
									*SchemaCategoryDefinition.Id.ToString().ToLower(),
									*SchemaAttributeDefinition.Id.ToString().ToLower());
								ParsedSuccessfully = false;
							}
						}
					}

					SchemaDataCrc = FCrc::TypeCrc32(SchemaCategoryDefinition.SchemaCompatibilityAttributeId, SchemaDataCrc);
					SchemaDataCrc = FCrc::TypeCrc32(SchemaCategoryDefinition.SchemaCompatibilityServiceAttributeId, SchemaDataCrc);
				}
			}

			// Build compatibility id from the schema and data CRCs.
			{
				const uint32 SchemaIdCrc = FCrc::TypeCrc32(SchemaDefinition->Id);
				if (SeenSchemaCrcs.Find(SchemaIdCrc) != nullptr)
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("Invalid schema %s: CRC collision processing schema name, please rename the schema to avoid collision."),
						*SchemaDefinition->Id.ToString().ToLower());
					ParsedSuccessfully = false;
				}
				SeenSchemaCrcs.Add(SchemaIdCrc);

				const uint64 CompatibilityIdUint = (static_cast<uint64>(SchemaIdCrc) << 32) | SchemaDataCrc;
				SchemaDefinition->CompatibilityId = *reinterpret_cast<const int64*>(&CompatibilityIdUint);
			}

			ParsedSchemaDefinitionsByCompatibilityId.Add(SchemaDefinition->CompatibilityId, SchemaDefinition);
		}
	}

	if (ParsedSuccessfully)
	{
		SchemaDefinitionsById = MoveTemp(ParsedSchemaDefinitions);
		SchemaDefinitionsByCompatibilityId = MoveTemp(ParsedSchemaDefinitionsByCompatibilityId);
		return true;
	}
	else
	{
		return false;
	}
}

FSchemaCategoryInstance::FSchemaCategoryInstance(
	const FSchemaId& DerivedSchemaId,
	const FSchemaId& BaseSchemaId,
	const FSchemaCategoryId& CategoryId,
	const TSharedRef<const FSchemaRegistry>& SchemaRegistry)
	: SchemaRegistry(SchemaRegistry)
{
	bool bInitSuccess = true;

	if (InitializeSchemaDefinition(BaseSchemaId, CategoryId, &BaseSchemaDefinition, &BaseSchemaCategoryDefinition))
	{
		if (DerivedSchemaId == FSchemaId())
		{
			// Derived schema id is unknown. One of the below is true:
			// 1. All attributes are defined in the base schema.
			// 2. An attribute in the base schema is flagged as SchemaCompatibilityId to allow
			//    detection of the derived schema based on incoming service attribute data.
		}
		else
		{
			if (InitializeSchemaDefinition(DerivedSchemaId, CategoryId, &DerivedSchemaDefinition, &DerivedSchemaCategoryDefinition))
			{
				// When a derived schema is in use the base schema must have a field flagged as SchemaCompatibilityId.
				if (BaseSchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId == FSchemaServiceAttributeId())
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("[FSchemaCategoryInstance] init error: Base schema %s is missing a valid SchemaCompatibilityId field"),
						*BaseSchemaId.ToString().ToLower());
					bInitSuccess = false;
				}
				else if (!SchemaRegistry->IsSchemaChildOf(DerivedSchemaId, BaseSchemaId))
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("[FSchemaCategoryInstance] init error: Schema %s is not a child of %s"),
						*DerivedSchemaId.ToString().ToLower(),
						*BaseSchemaId.ToString().ToLower());
					bInitSuccess = false;
				}
			}
			else
			{
				// Unable to find derived schema when it was expected to be valid.
				bInitSuccess = false;
			}
		}
	}

	if (!bInitSuccess)
	{
		// Invalidate schema to fail initialization.
		BaseSchemaDefinition.Reset();
		BaseSchemaCategoryDefinition = nullptr;
		DerivedSchemaDefinition.Reset();
		DerivedSchemaCategoryDefinition = nullptr;
	}
}

TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta> FSchemaCategoryInstance::ApplyApplicationDelta(FSchemaCategoryInstanceApplyApplicationDelta::Params&& Params)
{
	// todo: handle packing multiple attributes into a service attribute.

	const FSchemaDefinition* SchemaDefinition = nullptr;
	const FSchemaCategoryDefinition* SchemaCategoryDefinition = nullptr;
	if (!GetSerializationSchema(&SchemaDefinition, &SchemaCategoryDefinition))
	{
		UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] Schema category definition not found."));
		return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidState());
	}

	// Configured service attributes must be present to serialize to service.
	if (SchemaCategoryDefinition->ServiceAttributeDefinitions.IsEmpty())
	{
		UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s: Schema category has not been configured for serialization."),
			*SchemaDefinition->Id.ToString().ToLower(),
			*SchemaCategoryDefinition->Id.ToString().ToLower());
		return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
	}

	FSchemaServiceChanges Changes;

	// Translate mutated attributes.
	for (TPair<FSchemaAttributeId, FSchemaVariant>& UpdatedAttributeData : Params.UpdatedAttributes)
	{
		const FSchemaAttributeId& UpdatedAttributeId = UpdatedAttributeData.Get<0>();
		FSchemaVariant& UpdatedAttributeValue = UpdatedAttributeData.Get<1>();

		if (const FSchemaAttributeDefinition* AttributeDefinition = SchemaCategoryDefinition->SchemaAttributeDefinitions.Find(UpdatedAttributeId))
		{
			// Schema compatibility id cannot be modified by application code.
			if (EnumHasAnyFlags(AttributeDefinition->Flags, ESchemaAttributeFlags::SchemaCompatibilityId))
			{
				UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: It is not valid to set SchemaCompatibilityId in an attribute update"),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower());
				return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
			}

			// Check that type matches.
			if (AttributeDefinition->Type != UpdatedAttributeValue.GetType())
			{
				UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: Attribute type %s does not match the expected type %s."),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower(),
					LexToString(UpdatedAttributeValue.GetType()),
					LexToString(AttributeDefinition->Type));
				return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
			}

			// Check that variable sized data fits within the service attribute.
			if (AttributeDefinition->Type == ESchemaAttributeType::String && UpdatedAttributeValue.GetString().Len() > AttributeDefinition->MaxSize)
			{
				UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: Variably sized attribute exceeds maximum defined length. %d > %d."),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower(),
					UpdatedAttributeValue.GetString().Len(),
					AttributeDefinition->MaxSize);
				return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
			}

			Changes.UpdatedAttributes.Add(AttributeDefinition->ServiceAttributeId, {AttributeDefinition->ServiceAttributeId, AttributeDefinition->Flags, MoveTemp(UpdatedAttributeValue)});
		}
		else
		{
			UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: Attribute definition does not exist in schema category."),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower(),
				*UpdatedAttributeId.ToString().ToLower());
			return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
		}
	}

	// Translate removed attributes.
	for (FSchemaAttributeId& RemovedAttributeId : Params.RemovedAttributes)
	{
		if (const FSchemaAttributeDefinition* AttributeDefinition = SchemaCategoryDefinition->SchemaAttributeDefinitions.Find(RemovedAttributeId))
		{
			// Schema compatibility id cannot be modified by application code.
			if (EnumHasAnyFlags(AttributeDefinition->Flags, ESchemaAttributeFlags::SchemaCompatibilityId))
			{
				UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: It is not valid to set SchemaCompatibilityId in an attribute update"),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower());
				return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
			}
			else
			{
				Changes.RemovedAttributes.Add(AttributeDefinition->ServiceAttributeId);
			}
		}
		else
		{
			UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: Attribute definition does not exist in schema category."),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower(),
				*RemovedAttributeId.ToString().ToLower());
			return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidParams());
		}
	}

	// Check if SchemaCompatibilityId needs to be set.
	if (SchemaCategoryDefinition->SchemaCompatibilityAttributeId != FSchemaAttributeId() && LastSentSchemaCompatibilityId == 0)
	{
		const FSchemaAttributeDefinition* AttributeDefinition = SchemaCategoryDefinition->SchemaAttributeDefinitions.Find(SchemaCategoryDefinition->SchemaCompatibilityAttributeId);
		if (ensure(AttributeDefinition))
		{
			Changes.UpdatedAttributes.Add(AttributeDefinition->ServiceAttributeId, { AttributeDefinition->ServiceAttributeId, AttributeDefinition->Flags, SchemaDefinition->CompatibilityId });
			LastSentSchemaCompatibilityId = SchemaDefinition->CompatibilityId;
		}
		else
		{
			UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyApplicationDelta] %s.%s.%s: Failed to find attribute defnition for SchemaCompatibilityId field."),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->SchemaCompatibilityAttributeId.ToString().ToLower());
			return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(Errors::InvalidState());
		}
	}

	return TOnlineResult<FSchemaCategoryInstanceApplyApplicationDelta>(FSchemaCategoryInstanceApplyApplicationDelta::Result({MoveTemp(Changes)}));
}

TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot> FSchemaCategoryInstance::ApplyServiceSnapshot(FSchemaCategoryInstanceApplyServiceSnapshot::Params&& Params)
{
	// todo: handle unpacking a service attribute into multiple attributes.

	const FSchemaDefinition* SchemaDefinition = nullptr;
	const FSchemaCategoryDefinition* SchemaCategoryDefinition = nullptr;

	if (!GetSerializationSchema(&SchemaDefinition, &SchemaCategoryDefinition))
	{
		UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyServiceSnapshot] Schema category definition not found."));
		return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(Errors::InvalidState());
	}

	FSchemaApplicationChanges Changes;

	// Check for a schema change.
	TPair<FSchemaServiceAttributeId, FSchemaVariant>* SchemaCompatibilityAttributeData = Algo::FindByPredicate(Params.Attributes,
		[&SchemaCategoryDefinition](const TPair<FSchemaServiceAttributeId, FSchemaVariant>& AttributeData)
		{
			return AttributeData.Get<0>() == SchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId;
		});

	// Handle incoming schema change.
	if (SchemaCompatibilityAttributeData)
	{
		const FSchemaVariant& SchemaCompatibilityIdValue = SchemaCompatibilityAttributeData->Get<1>();

		if (TSharedPtr<const FSchemaDefinition> SwappedSchema = SchemaRegistry->GetDefinition(SchemaCompatibilityIdValue.GetInt64()))
		{
			if (!SchemaRegistry->IsSchemaChildOf(SwappedSchema->Id, BaseSchemaDefinition->Id))
			{
				UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Invalid schema swap. Incoming schema %s is not a child of %s."),
					*SchemaDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->Id.ToString().ToLower(),
					*SchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId.ToString().ToLower(),
					*SwappedSchema->Id.ToString().ToLower(),
					*BaseSchemaDefinition->Id.ToString().ToLower());
				return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(Errors::InvalidParams());
			}

			// Check that schema has actually changed.
			if (DerivedSchemaDefinition == nullptr || DerivedSchemaDefinition->Id != SwappedSchema->Id)
			{
				Changes.SchemaId.Emplace(SwappedSchema->Id);

				// Category must exist due to parent -> child relationship
				const FSchemaCategoryDefinition* SwappedSchemaCategoryDefinition = SwappedSchema->Categories.Find(SchemaCategoryDefinition->Id);
				if (ensure(SwappedSchemaCategoryDefinition))
				{
					UE_LOG(LogOnlineSchema, Verbose, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Derived schema changed from %s to %s."),
						*SchemaDefinition->Id.ToString().ToLower(),
						*SchemaCategoryDefinition->Id.ToString().ToLower(),
						*SchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId.ToString().ToLower(),
						DerivedSchemaDefinition ? *DerivedSchemaDefinition->Id.ToString().ToLower() : TEXT("nullptr"),
						*SwappedSchemaCategoryDefinition->Id.ToString().ToLower());

					// Set new schema to class member data.
					DerivedSchemaDefinition = SwappedSchema;
					DerivedSchemaCategoryDefinition = SwappedSchemaCategoryDefinition;

					// Update schema definitions for current update processing.
					SchemaDefinition = DerivedSchemaDefinition.Get();
					SchemaCategoryDefinition = SwappedSchemaCategoryDefinition;
				}
				else
				{
					UE_LOG(LogOnlineSchema, Error, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Failed to find expected category definition in incoming schema %s."),
						*SchemaDefinition->Id.ToString().ToLower(),
						*SchemaCategoryDefinition->Id.ToString().ToLower(),
						*SchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId.ToString().ToLower(),
						*SchemaCategoryDefinition->Id.ToString().ToLower());
					return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(Errors::InvalidParams());
				}
			}
		}
		else
		{
			UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Failed to find definition for SchemaCompatibilityId 0x%08" INT64_X_FMT "."),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->SchemaCompatibilityServiceAttributeId.ToString().ToLower(),
				SchemaCompatibilityIdValue.GetInt64());
			return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(Errors::InvalidParams());
		}
	}

	// Unpack service attributes.
	TMap<FSchemaAttributeId, FSchemaVariant> NewApplicationDataServiceSnapshot;
	for (TPair<FSchemaServiceAttributeId, FSchemaVariant>& SchemaServiceAttributeData : Params.Attributes)
	{
		const FSchemaServiceAttributeId& SchemaServiceAttributeId = SchemaServiceAttributeData.Get<0>();
		FSchemaVariant& SchemaServiceAttributeValue = SchemaServiceAttributeData.Get<1>();

		if (const FSchemaServiceAttributeDefinition* SchemaServiceAttributeDefinition =
			SchemaCategoryDefinition->ServiceAttributeDefinitions.Find(SchemaServiceAttributeId))
		{
			for (const FSchemaAttributeId& SchemaAttributeId : SchemaServiceAttributeDefinition->SchemaAttributeIds)
			{
				// todo: unpack attributes from service attribute.
				check(SchemaServiceAttributeDefinition->SchemaAttributeIds.Num() == 1);

				const FSchemaAttributeDefinition* SchemaAttributeDefinition = SchemaCategoryDefinition->SchemaAttributeDefinitions.Find(SchemaAttributeId);

				// Attribute definition must be valid due to successfully processing config.
				check(SchemaAttributeDefinition);

				// Schema changes are handled by surfacing a new schema id in the result object. Don't return SchemaCompatibilityId as an attribute to the application.
				if (EnumHasAnyFlags(SchemaAttributeDefinition->Flags, ESchemaAttributeFlags::SchemaCompatibilityId))
				{
					UE_LOG(LogOnlineSchema, Verbose, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Consuming SchemaCompatibility attribute data: %s."),
						*SchemaDefinition->Id.ToString().ToLower(),
						*SchemaCategoryDefinition->Id.ToString().ToLower(),
						*SchemaAttributeDefinition->Id.ToString().ToLower(),
						*SchemaServiceAttributeValue.ToLogString());
				}
				else
				{
					NewApplicationDataServiceSnapshot.Emplace(SchemaAttributeDefinition->Id, MoveTemp(SchemaServiceAttributeValue));
				}
			}
		}
		else
		{
			UE_LOG(LogOnlineSchema, Warning, TEXT("[ApplyServiceSnapshot] %s.%s.%s: Service attribute definition does not exist in schema category."),
				*SchemaDefinition->Id.ToString().ToLower(),
				*SchemaCategoryDefinition->Id.ToString().ToLower(),
				*SchemaServiceAttributeId.ToString().ToLower());
			return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(Errors::InvalidParams());
		}
	}

	// Process new snapshot against old snapshot to build application data changes.
	{
		// Find attributes that were added or changed.
		for (const TPair<FSchemaAttributeId, FSchemaVariant>& NewSnapshotAttributeData : NewApplicationDataServiceSnapshot)
		{
			const FSchemaServiceAttributeId& NewSnapshotAttributeId = NewSnapshotAttributeData.Get<0>();
			const FSchemaVariant& NewSnapshotAttributeValue = NewSnapshotAttributeData.Get<1>();

			if (FSchemaVariant* OldSnapshotAttributeValue = ApplicationDataServiceSnapshot.Find(NewSnapshotAttributeId))
			{
				Changes.ChangedAttributes.Add(NewSnapshotAttributeId, { MoveTemp(*OldSnapshotAttributeValue), NewSnapshotAttributeValue });
			}
			else
			{
				Changes.AddedAttributes.Add(NewSnapshotAttributeId, NewSnapshotAttributeValue);
			}
		}

		// Add removed attribute ids to changes.
		Algo::TransformIf(ApplicationDataServiceSnapshot, Changes.RemovedAttributes,
			[&NewApplicationDataServiceSnapshot](const TPair<FSchemaAttributeId, FSchemaVariant>& OldSnapshotAttributeData) -> bool
			{
				const FSchemaServiceAttributeId& OldSnapshotAttributeId = OldSnapshotAttributeData.Get<0>();
				return !NewApplicationDataServiceSnapshot.Contains(OldSnapshotAttributeId);
			},
			[](const TPair<FSchemaAttributeId, FSchemaVariant>& OldSnapshotAttributeData) -> FSchemaAttributeId
			{
				const FSchemaServiceAttributeId& OldSnapshotAttributeId = OldSnapshotAttributeData.Get<0>();
				return OldSnapshotAttributeId;
			});
	}

	ApplicationDataServiceSnapshot = MoveTemp(NewApplicationDataServiceSnapshot);
	return TOnlineResult<FSchemaCategoryInstanceApplyServiceSnapshot>(FSchemaCategoryInstanceApplyServiceSnapshot::Result({MoveTemp(Changes)}));
}

bool FSchemaCategoryInstance::IsValid() const
{
	return BaseSchemaCategoryDefinition != nullptr;
}

bool FSchemaCategoryInstance::IsValidBaseAttributeData(const FSchemaAttributeId& Id, const FSchemaVariant& Data)
{
	if (ensure(IsValid()))
	{
		if (const FSchemaAttributeDefinition* AttributeDefinition = BaseSchemaCategoryDefinition->SchemaAttributeDefinitions.Find(Id))
		{
			if (AttributeDefinition->Type == Data.GetType())
			{
				if (AttributeDefinition->Type == ESchemaAttributeType::String)
				{
					return Data.GetString().Len() <= AttributeDefinition->MaxSize;
				}
				else
				{
					return true;
				}
			}
			else
			{
				UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidBaseAttributeData] Schema attribute %s.%s.%s set with invalid type %s. Expected type %s"),
					*BaseSchemaDefinition->Id.ToString().ToLower(),
					*BaseSchemaCategoryDefinition->Id.ToString().ToLower(),
					*AttributeDefinition->Id.ToString().ToLower(),
					*LexToString(Data.GetType()),
					LexToString(AttributeDefinition->Type));
				return false;
			}
		}
		else
		{
			UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidBaseAttributeData] Attribute %s not found in schema definition %s.%s"),
				*Id.ToString().ToLower(),
				*BaseSchemaDefinition->Id.ToString().ToLower(),
				*BaseSchemaCategoryDefinition->Id.ToString().ToLower());
			return false;
		}
	}
	else
	{
		UE_LOG(LogOnlineSchema, Verbose, TEXT("[IsValidBaseAttributeData] Unable to set attribute %s. Schema is not valid"),
			*Id.ToString().ToLower());
		return false;
	}
}

bool FSchemaCategoryInstance::InitializeSchemaDefinition(
	const FSchemaId& SchemaId,
	const FSchemaCategoryId& CategoryId,
	TSharedPtr<const FSchemaDefinition>* OutSchemaDefinition,
	const FSchemaCategoryDefinition** OutSchemaCategoryDefinition)
{
	check(OutSchemaDefinition);
	check(OutSchemaCategoryDefinition);

	if (TSharedPtr<const FSchemaDefinition> SchemaDefinition = SchemaRegistry->GetDefinition(SchemaId))
	{
		if (const FSchemaCategoryDefinition* SchemaCategoryDefinition = SchemaDefinition->Categories.Find(CategoryId))
		{
			*OutSchemaDefinition = MoveTemp(SchemaDefinition);
			*OutSchemaCategoryDefinition = SchemaCategoryDefinition;
			return true;
		}
		else
		{
			UE_LOG(LogOnlineSchema, Error, TEXT("[InitializeSchemaDefinition] init error: Unable to find category %s in schema %s"),
				*CategoryId.ToString().ToLower(),
				*SchemaId.ToString().ToLower());
			return false;
		}
	}
	else
	{
		UE_LOG(LogOnlineSchema, Error, TEXT("[InitializeSchemaDefinition] init error: Unable to find definition for schema %s"),
			*SchemaId.ToString().ToLower());
		return false;
	}
}

bool FSchemaCategoryInstance::GetSerializationSchema(
	const FSchemaDefinition** OutSchemaDefinition,
	const FSchemaCategoryDefinition** OutSchemaCategoryDefinition) const
{
	check(OutSchemaDefinition);
	check(OutSchemaCategoryDefinition);

	if (IsValid())
	{
		if (DerivedSchemaCategoryDefinition)
		{
			*OutSchemaDefinition = DerivedSchemaDefinition.Get();
			*OutSchemaCategoryDefinition = DerivedSchemaCategoryDefinition;
		}
		else
		{
			*OutSchemaDefinition = BaseSchemaDefinition.Get();
			*OutSchemaCategoryDefinition = BaseSchemaCategoryDefinition;
		}

		return true;
	}
	else
	{
		return false;
	}
}

TSharedPtr<const FSchemaDefinition> FSchemaRegistry::GetDefinition(const FSchemaId& SchemaId) const
{
	const TSharedRef<const FSchemaDefinition>* Definition = SchemaDefinitionsById.Find(SchemaId);
	return Definition ? *Definition : TSharedPtr<const FSchemaDefinition>();
}

TSharedPtr<const FSchemaDefinition> FSchemaRegistry::GetDefinition(int64 CompatibilityId) const
{
	const TSharedRef<const FSchemaDefinition>* Definition = SchemaDefinitionsByCompatibilityId.Find(CompatibilityId);
	return Definition ? *Definition : TSharedPtr<const FSchemaDefinition>();
}

bool FSchemaRegistry::IsSchemaChildOf(const FSchemaId& SchemaId, const FSchemaId& ParentSchemaId) const
{
	const FSchemaId* FoundSchemaId = nullptr;
	if (const TSharedRef<const FSchemaDefinition>* Definition = SchemaDefinitionsById.Find(SchemaId))
	{
		FoundSchemaId = (* Definition)->ParentSchemaIds.Find(ParentSchemaId);
	}
	return FoundSchemaId != nullptr;
}

/* UE::Online */ }

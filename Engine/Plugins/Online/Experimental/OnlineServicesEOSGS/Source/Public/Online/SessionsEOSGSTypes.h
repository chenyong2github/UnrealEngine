// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Sessions.h"

#include "eos_sessions_types.h"

namespace UE::Online {

static FName EOSGS_ALLOW_NEW_MEMBERS = TEXT("EOSGS_ALLOW_NEW_MEMBERS");
static FName EOSGS_ALLOW_UNREGISTERED_PLAYERS = TEXT("EOSGS_ALLOW_UNREGISTERED_PLAYERS");
static FName EOSGS_ANTI_CHEAT_PROTECTED = TEXT("EOSGS_ANTI_CHEAT_PROTECTED");
static FName EOSGS_IS_DEDICATED_SERVER_SESSION = TEXT("EOSGS_IS_DEDICATED_SERVER_SESSION");
static FName EOSGS_PRESENCE_ENABLED = TEXT("EOSGS_PRESENCE_ENABLED");
static FName EOSGS_SCHEMA_NAME = TEXT("EOSGS_SCHEMA_NAME");
static FName EOSGS_SESSION_ID_OVERRIDE = TEXT("EOSGS_SESSION_ID_OVERRIDE");
static FName EOSGS_REGISTERED_PLAYERS = TEXT("EOSGS_REGISTERED_PLAYERS");
static FName EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT = TEXT("EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT");
static FName EOSGS_REGISTERED_PLAYER_IS_IN_SESSION = TEXT("EOSGS_REGISTERED_PLAYER_IS_IN_SESSION");

static FName EOSGS_BUCKET_ID = TEXT("EOSGS_BUCKET_ID");

EOS_EOnlineSessionPermissionLevel ToServiceType(const ESessionJoinPolicy& Value);
ESessionJoinPolicy FromServiceType(const EOS_EOnlineSessionPermissionLevel& Value);

EOS_ESessionAttributeAdvertisementType ToServiceType(const ESchemaAttributeVisibility& Value);
ESchemaAttributeVisibility FromServiceType(const EOS_ESessionAttributeAdvertisementType& Value);

EOS_EOnlineComparisonOp ToServiceType(const ESchemaAttributeComparisonOp& Value);

enum class ESessionAttributeConversionType
{
	ToService,
	FromService
};

template <ESessionAttributeConversionType>
class FSessionAttributeConverter
{
public:
};

template<>
class FSessionAttributeConverter<ESessionAttributeConversionType::ToService>
{
public:
	FSessionAttributeConverter(const FSchemaAttributeId& Key, const FSchemaVariant& Value);

	FSessionAttributeConverter(const TPair<FSchemaAttributeId, FSchemaVariant>& InData);

	const EOS_Sessions_AttributeData& GetAttributeData() const { return AttributeData; }

private:
	FTCHARToUTF8 KeyConverterStorage;
	TOptional<FTCHARToUTF8> ValueConverterStorage; // TOptional because we'll only use it if the FSessionVariant type is FString
	EOS_Sessions_AttributeData AttributeData;
};

template<>
class FSessionAttributeConverter<ESessionAttributeConversionType::FromService>
{
public:
	FSessionAttributeConverter(const EOS_Sessions_AttributeData& InData);

	const TPair<FSchemaAttributeId, FSchemaVariant>& GetAttributeData() const { return AttributeData; }

private:
	TPair<FSchemaAttributeId, FSchemaVariant> AttributeData;
};

/* UE::Online */ }
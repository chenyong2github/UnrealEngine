// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JwtGlobals.h"
#include "Dom/JsonObject.h"

class JWT_API FJsonWebToken
{
public:
	/**
	 * Creates a JWT from the provided string.
	 * The string must consist of 3 base64 url encoded parts: a header, payload, and signature.
	 * The parts must be split by a period character.
	 * The signature part is optional. If the signature is excluded, the string must still contain
	 * a period character in its place.
	 * Valid formats: "header.payload.signature" and "header.payload."
	 *
	 * @param InJsonWebTokenString The string to decode.
	 * @return An optional set with a FJsonWebToken if the JWT was generated successfully.
	 */
	static TOptional<FJsonWebToken> FromString(const FStringView InEncodedJsonWebToken);

	/**
	 * Creates a JWT from the provided string.
	 * The string must consist of 3 base64 url encoded parts: a header, payload, and signature.
	 * The parts must be split by a period character.
	 * The signature part is optional. If the signature is excluded, the string must still contain
	 * a period character in its place.
	 * Valid formats: "header.payload.signature" and "header.payload."
	 *
	 * @param InJsonWebTokenString The string to decode.
	 * @param OutJsonWebToken The generated JWT (FJsonWebToken instance), if input string was decoded successfully.
	 * @return True if JWT was generated successfully, otherwise false.
	 */
	static bool FromString(const FStringView InEncodedJsonWebToken, FJsonWebToken& OutJsonWebToken);

	/**
	 * Gets the type.
	 *
	 * @param OutValue The value to output on success.
	 * @return Whether the value exists and was successfully outputted.
	 */
	bool GetType(FString& OutValue) const;

	/**
	 * Gets the key id.
	 *
	 * @param OutValue The value to output on success.
	 * @return Whether the value exists and was successfully outputted.
	 */
	bool GetKeyId(FString& OutValue) const;

	/**
	 * Gets the algorithm that was used to construct the signature.
	 *
	 * @param OutValue The value to output on success.
	 * @return Whether the value exists and was successfully outputted.
	 */
	bool GetAlgorithm(FString& OutValue) const;

	/**
	 * Gets a claim by name.
	 * This method can be used to get custom claims that are not reserved as part of the JWT specification.
	 *
	 * @param InName The name of the claim.
	 * @param OutClaim The json value to output on success.
	 * @return Whether the claim exists and was successfully outputted.
	 */
	bool GetClaim(const FStringView InName, TSharedPtr<FJsonValue>& OutClaim) const;

	/**
	 * Gets a claim by name.
	 * This method can be used to get custom claims that are not reserved as part of the JWT specification.
	 *
	 * @param InName The name of the claim.
	 * @return The json value to output on success, or an invalid shared pointer on failure.
	 */
	template<EJson JsonType>
	TSharedPtr<FJsonValue> GetClaim(const FStringView InName) const
	{
		return Payload->GetField<JsonType>(FString(InName));
	}

	/**
	 * Gets the issuer (`iss`) claim from the payload, if present.
	 * 
	 * @param OutValue the issuer value, if successful.
	 * @return True on success (claim exists), otherwise false.
	 */
	bool GetIssuer(FString& OutValue) const;

	/**
	 * Gets the issued at (`iat`) claim from the payload, if present.
	 *
	 * @param OutValue the issued at value, if successful.
	 * @return True on success (claim exists), otherwise false.
	 */
	bool GetIssuedAt(double& OutValue) const;

	/**
	 * Gets the expiriation time (`exp`) claim from the payload, if present.
	 *
	 * @param OutValue the expiration time value, if successful.
	 * @return True on success (claim exists), otherwise false.
	 */
	bool GetExpiration(double& OutValue) const;

	/**
	 * Gets the subject (`sub`) claim from the payload, if present.
	 *
	 * @param OutValue the subject value, if successful.
	 * @return True on success (claim exists), otherwise false.
	 */
	bool GetSubject(FString& OutValue) const;

	/**
	 * Gets the audience (`aud`) claim from the payload, if present.
	 *
	 * @param OutValue the audience value, if successful.
	 * @return True on success (claim exists), otherwise false.
	 */
	bool GetAudience(FString& OutValue) const;

	/**
	 * Verifies the signature against the header and content, using the given public key info, which
	 * may be obtained from a JWK object.
	 * Assumes that input arrays are in little-endian byte order.
	 * The cryptographic algorithm used for verification is specified in this JsonWebToken's header.
	 *
	 * @see GetAlgorithm
	 *
	 * @param InKeyExponent The e (exponent) value of the public key.
	 * @param InKeyModulus The n (modulus) value of the public key.
	 * @return true if the header and payload were verified successfully (signature match), otherwise false.
	 */
	bool Verify(const TArrayView<const uint8> InKeyExponent, const TArrayView<const uint8> InKeyModulus) const;

	/**
	 * Verifies the signature against the header and content, using the given public key info, which
	 * may be obtained from a JWK object.
	 * If specified, will decode the key from Base64Url form, and/or convert to little-endian.
	 *
	 * @param InKeyExponent The e (exponent) value of the public key.
	 * @param InKeyModulus The n (modulus) value of the public key.
	 * @param bIsBase64UrlEncoded Set to true if InKeyExponent and InKeyModulus are Base64Url encoded (will decode).
	 * @param bIsBigEndian Set to true if InKeyExponent and InKeyModulus and in big-endian byte form (will reverse).
	 * @return true if the header and payload were verified successfully (signature match), otherwise false.
	 */
	bool Verify(const FStringView InKeyExponent, const FStringView InKeyModulus, const bool bIsBase64UrlEncoded, const bool bIsBigEndian) const;

	/**
	 * Verifies the signature against the header and content, using the given public key info, which
	 * may be obtained from a JWK object.
	 * Assumes Base64Url-encoded big-endian byte format for the key, as it would arrive from the web.
	 *
	 * @param InKeyExponent The e (exponent) value of the public key.
	 * @param InKeyModulus The n (modulus) value of the public key.
	 * @return true if the header and payload were verified successfully (signature match), otherwise false.
	 */
	bool Verify(const FStringView InKeyExponent, const FStringView InKeyModulus) const;

private:
	FJsonWebToken(const FStringView InEncodedJsonWebToken, const TSharedRef<FJsonObject>& InHeaderPtr,
		const TSharedRef<FJsonObject>& InPayloadPtr, const TOptional<TArray<uint8>>& InSignature);

	static void DumpJsonObject(const FJsonObject& InJsonObject);

	static TSharedPtr<FJsonObject> FromJson(const FString& InJsonStr);

	static TSharedPtr<FJsonObject> ParseEncodedJson(const FStringView InEncodedJson);

public:
	// JWT payload registered claim field names
	static const TCHAR *const CLAIM_ISSUER;
	static const TCHAR *const CLAIM_ISSUED_AT;
	static const TCHAR *const CLAIM_EXPIRATION;
	static const TCHAR *const CLAIM_SUBJECT;
	static const TCHAR *const CLAIM_AUDIENCE;

	// JWT header field names
	static const TCHAR* const HEADER_TYPE;
	static const TCHAR* const HEADER_KEY_ID;
	static const TCHAR* const HEADER_ALGORITHM;

	// JWT header expected values
	static const TCHAR* const TYPE_VALUE_JWT;

private:
	/** The full encoded JWT. */
	FString EncodedJsonWebToken;

	/** The decoded and parsed header. */
	TSharedRef<FJsonObject> Header;

	/** The decoded and parsed payload. */
	TSharedRef<FJsonObject> Payload;

	/** The decoded signature. */
	TOptional<TArray<uint8>> Signature;
};

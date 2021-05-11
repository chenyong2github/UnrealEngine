// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataPayload.h"
#include "IO/IoHash.h"
#include "Templates/TypeHash.h"

namespace UE::DerivedData
{

/** A key that uniquely identifies a build definition. */
struct FBuildKey
{
	FIoHash Hash;

	/** A key with a zero hash. */
	static const FBuildKey Empty;
};

inline const FBuildKey FBuildKey::Empty;

/** A key that uniquely identifies a payload within a build output. */
struct FBuildPayloadKey
{
	FBuildKey BuildKey;
	FPayloadId Id;

	/** A payload key with an empty build key and a null payload identifier. */
	static const FBuildPayloadKey Empty;
};

inline const FBuildPayloadKey FBuildPayloadKey::Empty;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const FBuildKey& A, const FBuildKey& B)
{
	return A.Hash == B.Hash;
}

inline bool operator!=(const FBuildKey& A, const FBuildKey& B)
{
	return A.Hash != B.Hash;
}

inline bool operator<(const FBuildKey& A, const FBuildKey& B)
{
	return A.Hash < B.Hash;
}

inline uint32 GetTypeHash(const FBuildKey& Key)
{
	return GetTypeHash(Key.Hash);
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FBuildKey& Key)
{
	return Builder << "Build/"_ASV << Key.Hash;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const FBuildPayloadKey& A, const FBuildPayloadKey& B)
{
	return A.BuildKey == B.BuildKey && A.Id == B.Id;
}

inline bool operator!=(const FBuildPayloadKey& A, const FBuildPayloadKey& B)
{
	return A.BuildKey != B.BuildKey || A.Id != B.Id;
}

inline bool operator<(const FBuildPayloadKey& A, const FBuildPayloadKey& B)
{
	const FBuildKey& KeyA = A.BuildKey;
	const FBuildKey& KeyB = B.BuildKey;
	return KeyA == KeyB ? A.Id < B.Id : KeyA < KeyB;
}

inline uint32 GetTypeHash(const FBuildPayloadKey& Key)
{
	return HashCombine(GetTypeHash(Key.BuildKey), GetTypeHash(Key.Id));
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FBuildPayloadKey& Key)
{
	return Builder << Key.BuildKey << CharType('/') << Key.Id;
}

} // UE::DerivedData

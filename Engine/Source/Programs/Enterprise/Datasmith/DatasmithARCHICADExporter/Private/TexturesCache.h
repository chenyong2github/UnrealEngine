// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"
#include "SyncContext.h"

#include "Model.hpp"

#include <map>
#include <set>

class FString;

BEGIN_NAMESPACE_UE_AC

class FTexturesCache
{
  public:
	// Handle textures used.
	class FTexturesCacheElem
	{
	  public:
		FTexturesCacheElem() {}

		GS::UniString TextureLabel; // Texture's name (Maybe not unique)
		GS::UniString TexturePath; // Path of the written texture (Unique)
		API_Guid	  Fingerprint = APINULLGuid; // The texture fingerprint as GUID
		double		  InvXSize = 1.0; // Used to compute uv
		double		  InvYSize = 1.0; // Used to compute uv
		bool bHasAlpha = false; // Texture has alpha channel (transparent, bump, diffuse, specular, ambient, surface)
		bool bMirrorX = false; // Mirror on X
		bool bMirrorY = false; // Mirror on Y
		bool bAlphaIsTransparence = false; // Texture use alpha channel for transparency
		bool bIsAvailable = false; // Texture can be loaded
		bool bUsed = false; // True if this texture is used
	};

	FTexturesCache();

	// Return the texture specified by the index. (Will throw an exception if index is invalid)
	const FTexturesCacheElem& GetTexture(const FSyncContext& InSyncContext, GS::Int32 InTextureIndex);

	void WriteTexture(const ModelerAPI::Texture& inACTexture, const GS::UniString& InPath, bool InIsFingerprint) const;

	size_t GetCount() const { return Textures.size(); }

  private:
	typedef std::map< GS::Int32, FTexturesCacheElem > MapTextureIndex2CacheElem;

	typedef std::set< FString > SetTexturesIds;
	MapTextureIndex2CacheElem	Textures;
	GS::UniString				AbsolutePath;
	GS::UniString				RelativePath;
	bool						bUseRelative;

	class LessUniStringPtr
	{
	  public:
		bool operator()(const GS::UniString* s1, const GS::UniString* s2) const { return *s1 < *s2; }
	};

	typedef std::set< const GS::UniString*, LessUniStringPtr > SetStrings;

	SetStrings	   TexturesNameSet;
	SetTexturesIds TexturesIdsSet;
};

END_NAMESPACE_UE_AC

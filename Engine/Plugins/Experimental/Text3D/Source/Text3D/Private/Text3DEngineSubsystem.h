// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Text3DPrivate.h"
#include "BevelType.h"
#include "ContourNode.h"
#include "Text3DEngineSubsystem.generated.h"


USTRUCT()
struct FCachedFontMeshes
{
	GENERATED_BODY()

public:
	FCachedFontMeshes();

	int32 GetCacheCount();
	TSharedPtr<int32> GetCacheCounter();

	UPROPERTY()
	TMap<uint32, class UStaticMesh*> Glyphs;

private:
	TSharedPtr<int32> CacheCounter;
};

USTRUCT()
struct FCachedFontData
{
	GENERATED_BODY()

public:
	FCachedFontData();
	~FCachedFontData();

	FT_Face GetFreeTypeFace();
	const FString& GetFontName();

	void LoadFreeTypeFace();
	void ClearFreeTypeFace();

	bool Cleanup();

	uint32 GetTypefaceFontDataHash();
	TSharedPtr<int32> GetCacheCounter();
	TSharedPtr<int32> GetMeshesCacheCounter(bool bOutline, float Extrude, float Bevel, EText3DBevelType BevelType, float BevelSegments);

	UStaticMesh* GetGlyphMesh(uint32 GlyphIndex, bool bOutline, float Extrude, float Bevel, EText3DBevelType BevelType, float BevelSegments);
	TSharedContourNode GetGlyphContours(uint32 GlyphIndex);

	UPROPERTY()
	class UFont* Font;

	UPROPERTY()
	TMap<uint32, FCachedFontMeshes> Meshes;

	TMap<uint32, TSharedContourNode> Glyphs;

private:
	FT_Face FreeTypeFace;
	FString FontName;
	TArray<uint8> Data;
	TSharedPtr<int32> CacheCounter;
	uint32 TypefaceFontDataHash;
};

UCLASS()
class TEXT3D_API UText3DEngineSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:	
	UText3DEngineSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;


	void Reset();
	void Cleanup();
	FCachedFontData& GetCachedFontData(class UFont* Font);

	UPROPERTY()
	class UMaterial* DefaultMaterial;

private:
	bool CleanupTimerCallback(float DeltaTime);

	UPROPERTY()
	TMap<uint32, FCachedFontData> CachedFonts;

	FDelegateHandle CleanupTickerHandle;
};

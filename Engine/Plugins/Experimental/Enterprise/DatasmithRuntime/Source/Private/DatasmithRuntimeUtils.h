// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneImporter.h"

#include "DirectLinkCommon.h"

#include "Misc/SecureHash.h"

class IDatasmithBaseMaterialElement;
class IDatasmithElement;
class IDatasmithMasterMaterialElement;
class IDatasmithMeshElement;
class IDatasmithTextureElement;
class IDatasmithUEPbrMaterialElement;
class FSceneImporter;
class UClass;
class UMaterial;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;

struct FTextureData;
struct FActorData;
struct FDatasmithMeshElementPayload;
struct FMeshDescription;

namespace DatasmithRuntime
{
	namespace EMaterialRequirements
	{
		enum Type
		{
			RequiresNothing = 0x00,
			RequiresNormals = 0x01,
			RequiresTangents = 0x02,
			RequiresAdjacency = 0x04,
		};
	};

	enum class EDSResizeTextureMode
	{
		NoResize,
		NearestPowerOfTwo,
		PreviousPowerOfTwo,
		NextPowerOfTwo
	};

	typedef TFunction<void(const FString&, int32)> FTextureCallback;

	FORCEINLINE uint32 GetTypeHash(const FMD5Hash& Hash)
	{
		return FCrc::MemCrc32(Hash.GetBytes(),Hash.GetSize());
	}

	extern const TCHAR* PbrTexturePropertyNames[6];

	extern void CalculateMeshesLightmapWeights(const TArray< FSceneGraphId >& MeshElementArray, const TMap< FSceneGraphId, TSharedPtr< IDatasmithElement > >& Elements, TMap< FSceneGraphId, float >& LightmapWeights);

	// Borrowed from UVGenerationUtils::GetNextOpenUVChannel(UStaticMesh* StaticMesh, int32 LODIndex)
	extern int32 GetNextOpenUVChannel(FMeshDescription& MeshDescription);

	// Borrowed from  UVGenerationUtils::SetupGeneratedLightmapUVResolution(UStaticMesh* StaticMesh, int32 LODIndex)
	extern int32 GenerateLightmapUVResolution(FMeshDescription& Mesh, int32 SrcLightmapIndex, int32 MinLightmapResolution);

	// Borrowed from FDatasmithStaticMeshImporter::ProcessCollision(UStaticMesh* StaticMesh, const TArray< FVector >& VertexPositions)
	extern void ProcessCollision(UStaticMesh* StaticMesh, FDatasmithMeshElementPayload& Payload);

	extern bool /*FDatasmithStaticMeshImporter::*/ShouldRecomputeNormals(const FMeshDescription& MeshDescription, int32 BuildRequirements);

	extern bool /*FDatasmithStaticMeshImporter::*/ShouldRecomputeTangents(const FMeshDescription& MeshDescription, int32 BuildRequirements);

	extern int32 ProcessMaterialElement(TSharedPtr< IDatasmithMasterMaterialElement > BaseMaterialElement, const TCHAR* Host, FTextureCallback TextureCallback);

	extern int32 ProcessMaterialElement(IDatasmithUEPbrMaterialElement* PbrMaterialElement, FTextureCallback TextureCallback);

	extern bool LoadMasterMaterial(UMaterialInstanceDynamic* MaterialInstance, TSharedPtr<IDatasmithMasterMaterialElement>& MaterialElement, const FString& HostString);

	extern bool LoadPbrMaterial(UMaterialInstanceDynamic* MaterialInstance, IDatasmithUEPbrMaterialElement* MaterialElement);

	extern void ImageReaderInitialize();

	extern bool GetTextureData(const TCHAR* Source, EDSResizeTextureMode Mode, uint32 MaxSize, bool bGenerateNormalMap, FTextureData& TextureData);

	class FAssetRegistry
	{
	public:
		static void RegisterMapping(uint32 SceneKey, TMap<FSceneGraphId, FAssetData>* AssetsMapping);

		static void UnregisterMapping(uint32 SceneKey);

		static void RegisterAssetData(UObject* Asset, uint32 SceneKey, FAssetData& AssetData);

		static int32 UnregisterAssetData(UObject* Asset, uint32 SceneKey, FSceneGraphId AssetId);

		static void UnregisteredAssetsData(UObject* Asset, uint32 SceneKey, TFunction<void(FAssetData& AssetData)> UpdateFunc);

		static void SetObjectCompletion(UObject* Asset, bool bIsCompleted);

		static bool IsObjectCompleted(UObject* Asset);

		static UObject* FindObjectFromHash(DirectLink::FElementHash ElementHash);

		/** @return true if some assets have been marked for deletion */
		static bool CleanUp();

	private:
		static TMap<DirectLink::FElementHash, TStrongObjectPtr<UObject>> RegistrationMap;
		static TMap<uint32, TMap<FSceneGraphId,FAssetData>*> SceneMappings;
	};

}

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#ifdef USE_KERNEL_IO_SDK

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "CoreTechTypes.h"

#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Misc/Paths.h"

struct FFileStatData;
struct FFileDescription;

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/kernel_io.h"
#include "kernel_io/kernel_io_error.h"
#include "kernel_io/kernel_io_type.h"
#include "kernel_io/attribute_io/attribute_enum.h"
#include "kernel_io/material_io/material_io.h"
#pragma warning(pop)

namespace CADLibrary
{
	namespace CoreTechFileReaderUtils
	{
		void ScaleUV(CT_OBJECT_ID FaceID, TArray<FVector2D>& TexCoordArray, float Scale);
		uint32 GetFaceTessellation(CT_OBJECT_ID FaceID, FTessellationData& Tessellation);
		void GetBodyTessellation(CT_OBJECT_ID BodyId, FBodyMesh& OutBodyMesh, TFunction<void(CT_OBJECT_ID, int32, FTessellationData&)> = TFunction<void(CT_OBJECT_ID, int32, FTessellationData&)>());
	}

	class CADINTERFACES_API FCoreTechFileReader
	{
	public:
		struct FContext
		{
			FContext(const FImportParameters& InImportParameters, const FString& InCachePath, FArchiveSceneGraph& InSceneGraphArchive, TArray<FString>& InWarningMessages, TArray<FBodyMesh>& InBodyMeshes)
				: ImportParameters(InImportParameters)
				, SceneGraphArchive(InSceneGraphArchive)
				, WarningMessages(InWarningMessages)
				, BodyMeshes(InBodyMeshes)
				, CachePath(InCachePath)
			{
			}

			FContext(const FContext& Other)
				: ImportParameters(Other.ImportParameters)
				, SceneGraphArchive(Other.SceneGraphArchive)
				, WarningMessages(Other.WarningMessages)
				, BodyMeshes(Other.BodyMeshes)
				, CachePath(Other.CachePath)
			{
			}

			const FImportParameters& ImportParameters;
			FArchiveSceneGraph& SceneGraphArchive;
			TArray<FString>& WarningMessages;
			TArray<FBodyMesh>& BodyMeshes;

			FString CachePath;
		};

		/**
		 * @param ImportParams Parameters that setting import data like mesh SAG...
		 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
		 * @param InCachePath Full path of the cache in which the data will be saved
		 */
		FCoreTechFileReader(const FContext& InContext, const FString& EnginePluginsPath = TEXT(""));

		ECoreTechParsingResult ProcessFile(const CADLibrary::FFileDescription& InCTFileDescription);
		
	private:
		bool ReadNode(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
		bool ReadInstance(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
		bool ReadComponent(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
		bool ReadBody(CT_OBJECT_ID NodeId, CT_OBJECT_ID ParentId, uint32 ParentMaterialHash, bool bNeedRepair);

		bool FindFile(FFileDescription& FileDescription);

		uint32 GetMaterialNum();
		void ReadMaterials();

		uint32 GetObjectMaterial(ICADArchiveObject& Object);
		FArchiveColor& FindOrAddColor(uint32 ColorHId);
		FArchiveMaterial& FindOrAddMaterial(CT_MATERIAL_ID MaterialId);
		void SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, FBodyMesh& BodyMesh, int32 FaceIndex);

		void ReadNodeMetaData(CT_OBJECT_ID NodeId, TMap<FString, FString>& OutMetaData);
		void GetStringMetaDataValue(CT_OBJECT_ID NodeId, const TCHAR* InMetaDataName, FString& OutMetaDataValue);

		CT_FLAGS SetCoreTechImportOption();
		void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

	protected:
		CADLibrary::FFileDescription FileDescription;

		FContext Context;
	};
} // ns CADLibrary

#endif // USE_KERNEL_IO_SDK

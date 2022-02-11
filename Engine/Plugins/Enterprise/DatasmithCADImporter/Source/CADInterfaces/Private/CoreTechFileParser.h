// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#ifdef USE_KERNEL_IO_SDK

#include "CADData.h"
#include "CADFileData.h"
#include "CADFileParser.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "CoreTechTypes.h"

#include "CoreTechBridge.h"

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

namespace CADKernel
{
	class FBody;
	class FBodyMesh;
	class FFaceMesh;
	class FModelMesh;
}

namespace CADLibrary
{
	namespace CoreTechFileParserUtils
	{
		void ScaleUV(CT_OBJECT_ID FaceID, TArray<FVector2D>& TexCoordArray, float Scale);
		uint32 GetFaceTessellation(CT_OBJECT_ID FaceID, FTessellationData& Tessellation);
		void GetBodyTessellation(CT_OBJECT_ID BodyId, FBodyMesh& OutBodyMesh, TFunction<void(CT_OBJECT_ID, int32, FTessellationData&)> = TFunction<void(CT_OBJECT_ID, int32, FTessellationData&)>());
	}

	namespace CADKernelUtils
	{
		void GetBodyTessellation(const TSharedRef<CADKernel::FModelMesh>& ModelMesh, const TSharedRef<CADKernel::FBody>& Body, FBodyMesh& OutBodyMesh, uint32 DefaultMaterialHash, TFunction<void(FObjectDisplayDataId, FObjectDisplayDataId, int32)> SetFaceMainMaterial);

		uint32 GetFaceTessellation(const TSharedRef<CADKernel::FFaceMesh>& FaceMesh, FBodyMesh& OutBodyMesh);
	}


	class CADINTERFACES_API FCoreTechFileParser : public ICADFileParser
	{
	public:

		/**
		 * @param 
		 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
		 */
		FCoreTechFileParser(FCADFileData& InCADData, const FString& EnginePluginsPath = TEXT(""));

		virtual ECADParsingResult Process() override;

	private:
		bool ReadNode(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
		bool ReadInstance(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
		bool ReadComponent(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);

		bool BuildStaticMeshDataWithKio(CT_OBJECT_ID NodeId, CT_OBJECT_ID ParentId, uint32 ParentMaterialHash);
		bool BuildStaticMeshData(CT_OBJECT_ID NodeId, CT_OBJECT_ID ParentId, uint32 ParentMaterialHash);
		
		void ReadAndSewBodies(const TArray<CT_OBJECT_ID>& Bodies, CT_OBJECT_ID ParentId, uint32 ParentMaterialHash, TArray<FCadId>& OutChildren);
		void BuildStaticMeshData(CADKernel::FSession& CADKernelSession, CADKernel::FBody& CADKernelBody, CT_OBJECT_ID ParentId, uint32 ParentMaterialHash);

		uint32 GetMaterialNum();
		void ReadMaterials();

		uint32 GetObjectMaterial(FCADArchiveObject& Object);
		FArchiveColor& FindOrAddColor(uint32 ColorHId);
		FArchiveMaterial& FindOrAddMaterial(CT_MATERIAL_ID MaterialId);
		void SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, FBodyMesh& BodyMesh, int32 FaceIndex);

		void ReadNodeMetaData(CT_OBJECT_ID NodeId, TMap<FString, FString>& OutMetaData);
		void GetStringMetaDataValue(CT_OBJECT_ID NodeId, const TCHAR* InMetaDataName, FString& OutMetaDataValue);

		CT_FLAGS SetCoreTechImportOption();
		void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

		FCADFileData& CADFileData;
		FFileDescriptor& FileDescription;
		int32 LastHostIdUsed;
	};
} // CADLibrary

#endif // USE_KERNEL_IO_SDK

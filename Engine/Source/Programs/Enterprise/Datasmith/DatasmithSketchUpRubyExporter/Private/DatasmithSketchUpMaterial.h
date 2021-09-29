// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"

#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/material.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"


class IDatasmithBaseMaterialElement;

namespace DatasmithSketchUp
{
	class FExportContext;

	class FNodeOccurence;
	class FEntitiesGeometry;
	class FMaterial;
	class FMaterialOccurrence;
	class FTexture;

	// Associates SketchUp material with its Datasmith occurrences
	// A SketchUp material can have two instances in Datasmith scene - when it's directly applied to a face and when inherited from Component
	// Why two instances - directly applied materials have their texture scaling baked into Face UVs by SketchUp, inherited material needs to scale UVs 
	class FMaterial : FNoncopyable
	{

	public:
		static FMaterialIDType const DEFAULT_MATERIAL_ID;
		static FMaterialIDType const INHERITED_MATERIAL_ID;

		FMaterial(SUMaterialRef InMaterialRef);

		static TSharedPtr<FMaterial> Create(FExportContext& Context, SUMaterialRef InMaterialRef);

		static TSharedPtr<FMaterialOccurrence> CreateDefaultMaterial(FExportContext& Context);
		static TSharedRef<IDatasmithBaseMaterialElement> CreateDefaultMaterialElement(FExportContext& Context);

		// Convert the SketchUp sRGB color to a Datasmith linear color.
		static FLinearColor ConvertColor(const SUColor& C, bool bAlphaUsed = false);


		// Indicate that this material is used as directly applied on a mesh
		FMaterialOccurrence& RegisterGeometry(FEntitiesGeometry*);
		void UnregisterGeometry(FEntitiesGeometry*);

		// Indicate that this material is used as directly applied on an instance occurrence
		// Note - this is not per 'instance' as every instance can be in separate place in scene multiple times possibly resulting in different inherited materials
		FMaterialOccurrence& RegisterInstance(FNodeOccurence*);
		void UnregisterInstance(FNodeOccurence* NodeOccurrence);


		void Invalidate(FExportContext& Context);
		void UpdateTexturesUsage(FExportContext& Context);
		void Update(FExportContext& Context); // create datasmith elements for material occurrences
		void Remove(FExportContext& Context);

		bool IsUsed()
		{
			return MeshesMaterialDirectlyAppliedTo.Num() || NodesMaterialInheritedBy.Num();
		}

		FTexture* GetTexture()
		{
			return Texture;
		}

	private:

		SUMaterialRef MaterialRef;

		int32 EntityId;

		FTexture* Texture = nullptr;

		// Material can be directly applied to a face in SketchUp
		TSharedPtr<FMaterialOccurrence> MaterialDirectlyAppliedToMeshes;
		TSet<FEntitiesGeometry*> MeshesMaterialDirectlyAppliedTo;

		// In case face has Default material assigned it inherits material set to it's parent(in general - first non-Default material in ancestors chain)
		TSharedPtr<FMaterialOccurrence> MaterialInheritedByNodes;
		TSet<FNodeOccurence*> NodesMaterialInheritedBy;

		uint8 bInvalidated : 1;

		friend class FMaterialOccurrence;
	};

	class FMaterialOccurrence : FNoncopyable
	{
	public:

		TSharedPtr<IDatasmithBaseMaterialElement> DatasmithElement;

		const TCHAR* GetName();

		void RemoveDatasmithElement(FExportContext& Context);

	};


}

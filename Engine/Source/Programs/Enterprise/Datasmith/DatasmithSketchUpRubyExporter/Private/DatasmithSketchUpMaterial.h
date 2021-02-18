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

		// Indicate that this material is used as directly applied on a mesh
		FMaterialOccurrence& RegisterGeometry(FEntitiesGeometry*);
		void UnregisterGeometry(FEntitiesGeometry*);

		// Indicate that this material is used as directly applied on an instance occurrence
		// Note - this is not per 'instance' as every instance can be in separate place in scene multiple times possibly resulting in different inherited materials
		FMaterialOccurrence& RegisterInstance(FNodeOccurence*);

		void Update(FExportContext& Context); // create datasmith elements for material occurrences

		bool IsUsed()
		{
			return MeshesMaterialDirectlyAppliedTo.Num() || NodesMaterialInheritedBy.Num();
		}

	private:

		SUMaterialRef MaterialRef;

		int32 EntityId; // Sketchup Material entity Id is used as a slot Id on datasmith meshes

		// Material can be directly applied to a face in SketchUp
		TSharedPtr<FMaterialOccurrence> MaterialDirectlyAppliedToMeshes;
		TSet<FEntitiesGeometry*> MeshesMaterialDirectlyAppliedTo;

		// In case face has Default material assigned it inherits material set to it's parent(in general - first non-Default material in ancestors chain)
		TSharedPtr<FMaterialOccurrence> MaterialInheritedByNodes;
		TSet<FNodeOccurence*> NodesMaterialInheritedBy;

		friend class FMaterialOccurrence;

	};

	class FMaterialOccurrence : FNoncopyable
	{
	public:

		FMaterialOccurrence(const TSharedPtr<IDatasmithBaseMaterialElement>& InDatasmithElement) 
			: DatasmithElement(InDatasmithElement)
		{}

		TSharedPtr<IDatasmithBaseMaterialElement> DatasmithElement;

		const TCHAR* GetName();
	};


}

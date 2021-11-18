// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshVertexColorFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshVertexColorFunctions"


static FLinearColor CombineColors(FLinearColor ExistingColor, FLinearColor NewColor, FGeometryScriptColorFlags Flags)
{
	ExistingColor.R = (Flags.bRed) ? ExistingColor.R : NewColor.R;
	ExistingColor.G = (Flags.bBlue) ? ExistingColor.G : NewColor.G;
	ExistingColor.B = (Flags.bGreen) ? ExistingColor.B : NewColor.B;
	ExistingColor.A = (Flags.bAlpha) ? ExistingColor.A : NewColor.A;
	return ExistingColor;
}

UDynamicMesh* UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshConstantVertexColor(
	UDynamicMesh* TargetMesh,
	FLinearColor Color,
	FGeometryScriptColorFlags Flags,
	bool bClearExisting,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshConstantVertexColor_InvalidInput", "SetMeshConstantVertexColor: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		bool bCreated = false;
		if (EditMesh.HasAttributes() == false)
		{
			EditMesh.EnableAttributes();
			bCreated = true;
		}
		if (EditMesh.Attributes()->HasPrimaryColors() == false)
		{
			EditMesh.Attributes()->EnablePrimaryColors();
			bCreated = true;
		}
		FDynamicMeshColorOverlay* Colors = EditMesh.Attributes()->PrimaryColors();
		if (bClearExisting && bCreated == false)
		{
			Colors->ClearElements();
		}
		if (Colors->ElementCount() == 0)
		{
			TArray<int32> ElemIDs;
			ElemIDs.SetNum(EditMesh.MaxVertexID());
			for (int32 VertexID : EditMesh.VertexIndicesItr())
			{
				ElemIDs[VertexID] = Colors->AppendElement(FVector4f(Color.R, Color.G, Color.B, Color.A));
			}
			for (int32 TriangleID : EditMesh.TriangleIndicesItr())
			{
				FIndex3i Triangle = EditMesh.GetTriangle(TriangleID);
				Colors->SetTriangle(TriangleID, FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]) );
			}
		}
		else
		{
			for (int32 ElementID : Colors->ElementIndicesItr())
			{
				FLinearColor Existing = ToLinearColor(Colors->GetElement(ElementID));
				FLinearColor NewColor = CombineColors(Existing, Color, Flags);
				Colors->SetElement(ElementID, ToVector4<float>(NewColor));
			}
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshPerVertexColors(
	UDynamicMesh* TargetMesh,
	FGeometryScriptColorList VertexColorList,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshPerVertexColors_InvalidMesh", "SetMeshPerVertexColors: TargetMesh is Null"));
		return TargetMesh;
	}
	if (VertexColorList.List.IsValid() == false || VertexColorList.List->Num() == 0)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshPerVertexColors_InvalidList", "SetMeshPerVertexColors: List is empty"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		const TArray<FLinearColor>& VertexColors = *VertexColorList.List;
		if (VertexColors.Num() < EditMesh.MaxVertexID())
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SetMeshPerVertexColors_IncorrectCount", "SetMeshPerVertexColors: size of provided VertexColorList is smaller than MaxVertexID of Mesh"));
		}
		else
		{
			if (EditMesh.HasAttributes() == false)
			{
				EditMesh.EnableAttributes();
			}
			if (EditMesh.Attributes()->HasPrimaryColors() == false)
			{
				EditMesh.Attributes()->EnablePrimaryColors();
			}
			FDynamicMeshColorOverlay* Colors = EditMesh.Attributes()->PrimaryColors();
			Colors->ClearElements();
			TArray<int32> ElemIDs;
			ElemIDs.SetNum(EditMesh.MaxVertexID());
			for (int32 VertexID : EditMesh.VertexIndicesItr())
			{
				//const FLinearColor& FromColor = VertexColors[VertexID];
				//FColor SRGBFColor = FromColor.ToFColorSRGB();
				//FLinearColor Color = SRGBFColor.ReinterpretAsLinear();
				const FLinearColor& Color = VertexColors[VertexID];
				ElemIDs[VertexID] = Colors->AppendElement(FVector4f(Color.R, Color.G, Color.B, Color.A));
			}
			for (int32 TriangleID : EditMesh.TriangleIndicesItr())
			{
				FIndex3i Triangle = EditMesh.GetTriangle(TriangleID);
				Colors->SetTriangle(TriangleID, FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]) );
			}
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

#undef LOCTEXT_NAMESPACE
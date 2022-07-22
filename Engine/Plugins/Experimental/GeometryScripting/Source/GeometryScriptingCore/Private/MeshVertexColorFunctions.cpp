// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshVertexColorFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "Util/ColorConstants.h"

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








UDynamicMesh* UGeometryScriptLibrary_MeshVertexColorFunctions::GetMeshPerVertexColors(
	UDynamicMesh* TargetMesh, 
	FGeometryScriptColorList& ColorList, 
	bool& bIsValidColorSet,
	bool& bHasVertexIDGaps,
	bool bBlendSplitVertexValues)
{
	ColorList.Reset();
	TArray<FLinearColor>& Colors = *ColorList.List;
	bHasVertexIDGaps = false;
	bIsValidColorSet = false;
	if (TargetMesh)
	{
		TargetMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
		{
			Colors.Init(FLinearColor::Black, ReadMesh.MaxVertexID());
			bHasVertexIDGaps = ! ReadMesh.IsCompactV();

			if (ReadMesh.HasAttributes() && ReadMesh.Attributes()->HasPrimaryColors() )
			{
				const FDynamicMeshColorOverlay* ColorOverlay = ReadMesh.Attributes()->PrimaryColors();

				if (bBlendSplitVertexValues)
				{
					TArray<int32> ColorCounts;
					ColorCounts.Init(0, ReadMesh.MaxVertexID());
					for (int32 tid : ReadMesh.TriangleIndicesItr())
					{
						if (ColorOverlay->IsSetTriangle(tid))
						{
							FIndex3i TriV = ReadMesh.GetTriangle(tid);
							FVector4f A, B, C;
							ColorOverlay->GetTriElements(tid, A, B, C);
							Colors[TriV.A] += ToLinearColor(A);
							ColorCounts[TriV.A]++;
							Colors[TriV.B] += ToLinearColor(B);
							ColorCounts[TriV.B]++;
							Colors[TriV.C] += ToLinearColor(C);
							ColorCounts[TriV.C]++;
						}
					}

					for (int32 k = 0; k < ColorCounts.Num(); ++k)
					{
						if (ColorCounts[k] > 1)
						{
							Colors[k] *= 1.0f / (float)ColorCounts[k];
						}
					}
				}
				else
				{
					for (int32 tid : ReadMesh.TriangleIndicesItr())
					{
						if (ColorOverlay->IsSetTriangle(tid))
						{
							FIndex3i TriV = ReadMesh.GetTriangle(tid);
							FVector4f A, B, C;
							ColorOverlay->GetTriElements(tid, A, B, C);
							Colors[TriV.A] = ToLinearColor(A);
							Colors[TriV.B] = ToLinearColor(B);
							Colors[TriV.C] = ToLinearColor(C);
						}
					}
				}
				
				bIsValidColorSet = true;
			}
		});
	}

	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshVertexColorFunctions::ConvertMeshVertexColorsSRGBToLinear(
	UDynamicMesh* TargetMesh,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ConvertMeshVertexColorsSRGBToLinear_InvalidInput", "ConvertMeshVertexColorsSRGBToLinear: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		if (EditMesh.HasAttributes() == false || EditMesh.Attributes()->HasPrimaryColors() == false)
		{
			return;
		}

		FDynamicMeshColorOverlay* Colors = EditMesh.Attributes()->PrimaryColors();
		if (Colors->ElementCount() > 0)
		{
			for (int32 ElementID : Colors->ElementIndicesItr())
			{
				FVector4f Existing = Colors->GetElement(ElementID);
				LinearColors::SRGBToLinear(Existing);
				Colors->SetElement(ElementID, Existing);
			}
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	
	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshVertexColorFunctions::ConvertMeshVertexColorsLinearToSRGB(
	UDynamicMesh* TargetMesh,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ConvertMeshVertexColorsLinearToSRGB_InvalidInput", "ConvertMeshVertexColorsLinearToSRGB: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		if (EditMesh.HasAttributes() == false || EditMesh.Attributes()->HasPrimaryColors() == false)
		{
			return;
		}

		FDynamicMeshColorOverlay* Colors = EditMesh.Attributes()->PrimaryColors();
		if (Colors->ElementCount() > 0)
		{
			for (int32 ElementID : Colors->ElementIndicesItr())
			{
				FVector4f Existing = Colors->GetElement(ElementID);
				LinearColors::LinearToSRGB(Existing);
				Colors->SetElement(ElementID, Existing);
			}
		}
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	
	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreTechHelper.h"


#ifdef CAD_LIBRARY
#include "CoreTechTypes.h"

#include "CADData.h"
#include "CoreTechFileParser.h"
#include "DatasmithMaterialElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "Math.h"
#include "MeshDescription.h"
#include "MeshOperator.h"
#include "Misc/FileHelper.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "CADData.h"

typedef uint32 TriangleIndex[3];

namespace CADLibrary
{
// Ref. GPureMeshInterface

struct FVertexData
{
	float Z;
	int32 Index;
	FVector Coordinates;
	bool bIsMerged;
	FVertexID VertexID;
	FVertexID SymVertexID;

	/** Default constructor. */
	FVertexData() {}

	/** Initialization constructor. */
	FVertexData(int32 InIndex, const FVector& V)
	{
		Z = 0.30f * V.X + 0.33f * V.Y + 0.37f * V.Z;
		Index = InIndex;
		Coordinates = V;
		bIsMerged = false;
		VertexID = FVertexID::Invalid;
	}
};

struct FCompareVertexZ
{
	FORCEINLINE bool operator()(FVertexData const& A, FVertexData const& B) const { return A.Z < B.Z; }
};

// Verify the 3 input indices are not defining a degenerated triangle and fill up the corresponding FVertexIDs
bool IsTriangleDegenerated(const int32_t* Indices, const TArray<FVertexID>& RemapVertexPosition, FVertexID VertexIDs[3])
{
	if (Indices[0] == Indices[1] || Indices[0] == Indices[2] || Indices[1] == Indices[2])
	{
		return true;
	}

	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		VertexIDs[Corner] = RemapVertexPosition[Indices[Corner]];
	}

	return (VertexIDs[0] == VertexIDs[1] || VertexIDs[0] == VertexIDs[2] || VertexIDs[1] == VertexIDs[2]);
}

void FillVertexPosition(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, int32 TriangleCount, TArray<FTessellationData>& FaceTessellationSet, FMeshDescription& MeshDescription)
{
	TVertexAttributesRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	// Create a list of vertex Z/index pairs
	TArray<FVertexData> VertexDataSet;
	VertexDataSet.Reserve(TriangleCount * 3);

	FVector Position;
	uint32 GlobalVertexCount = 0;
	for (FTessellationData& CTTessellation : FaceTessellationSet)
	{
		CTTessellation.StartVertexIndex = GlobalVertexCount;
		for (uint32 VertexIndex = 0; VertexIndex < CTTessellation.VertexCount; ++VertexIndex, ++GlobalVertexCount)
		{
			CopyValue(CTTessellation.VertexArray.GetData(), VertexIndex * 3, CTTessellation.SizeOfVertexType, true, Position);
			Position *= ImportParams.ScaleFactor;  // convert Position unit into cm according to scaleFactor
			VertexDataSet.Emplace(GlobalVertexCount, Position);
		}
	}
	VertexDataSet.SetNum(GlobalVertexCount);

	// Sort the vertices by z value
	VertexDataSet.Sort(FCompareVertexZ());

	TArray<uint32> NewIndexOf;
	NewIndexOf.SetNumZeroed(GlobalVertexCount);

	TArray<int32> IndexOfCoincidentNode;
	IndexOfCoincidentNode.SetNumZeroed(GlobalVertexCount);

	int32 VertexCount = 0;
	// Search for duplicates, quickly!
	for (int32 i = 0; i < VertexDataSet.Num(); i++)
	{
		NewIndexOf[VertexDataSet[i].Index] = i;
		if (VertexDataSet[i].bIsMerged)
		{
			continue;
		}

		VertexDataSet[i].bIsMerged = true;
		int32 Index_i = VertexDataSet[i].Index;
		IndexOfCoincidentNode[Index_i] = Index_i;

		// only need to search forward, since we add pairs both ways
		for (int32 j = i + 1; j < VertexDataSet.Num(); j++)
		{
			if (FMath::Abs(VertexDataSet[j].Z - VertexDataSet[i].Z) > KINDA_SMALL_NUMBER)
			{
				break; // can't be any more duplicated
			}

			const FVector& PositionA = VertexDataSet[i].Coordinates;
			const FVector& PositionB = VertexDataSet[j].Coordinates;

			if (PositionA.Equals(PositionB, KINDA_SMALL_NUMBER))
			{
				VertexDataSet[j].bIsMerged = true;
				IndexOfCoincidentNode[VertexDataSet[j].Index] = Index_i;
			}
		}
		VertexCount++;
	}


	// if Symmetric mesh, the symmetric side of the mesh have to be generated
	FMatrix SymmetricMatrix;
	bool bIsSymmetricMesh = MeshParameters.bIsSymmetric;
	if (bIsSymmetricMesh)
	{
		SymmetricMatrix = FDatasmithUtils::GetSymmetricMatrix(MeshParameters.SymmetricOrigin, MeshParameters.SymmetricNormal);
	}


	// Make MeshDescription.VertexPositions and VertexID
	MeshDescription.ReserveNewVertices(VertexCount);
	uint32 GlobalVertexIndex = 0;

	for (uint32 VertexIndex = 0; VertexIndex < GlobalVertexCount; ++VertexIndex)
	{
		uint32 RealIndex = VertexDataSet[VertexIndex].Index;
		if (IndexOfCoincidentNode[RealIndex] != RealIndex)
		{
			continue;
		}

		FVertexID VertexID = MeshDescription.CreateVertex();
		VertexPositions[VertexID] = FDatasmithUtils::ConvertVector((FDatasmithUtils::EModelCoordSystem) ImportParams.ModelCoordSys, VertexDataSet[VertexIndex].Coordinates);
		VertexDataSet[VertexIndex].VertexID = VertexID;
	}

	if (bIsSymmetricMesh)
	{
		for (uint32 VertexIndex = 0; VertexIndex < GlobalVertexCount; ++VertexIndex)
		{
			uint32 RealIndex = VertexDataSet[VertexIndex].Index;
			if (IndexOfCoincidentNode[RealIndex] != RealIndex)
			{
				continue;
			}

			FVertexID VertexID = MeshDescription.CreateVertex();
			VertexPositions[VertexID] = FDatasmithUtils::ConvertVector((FDatasmithUtils::EModelCoordSystem) ImportParams.ModelCoordSys, VertexDataSet[VertexIndex].Coordinates);
			VertexPositions[VertexID] = SymmetricMatrix.TransformPosition(VertexPositions[VertexID]);

			VertexDataSet[VertexIndex].SymVertexID = VertexID;
		}
	}

	// For each face, for each vertex set VertexId
	GlobalVertexIndex = 0;
	for (FTessellationData& CTTessellation : FaceTessellationSet)
	{
		CTTessellation.VertexIdSet.SetNum(CTTessellation.VertexCount);
		for (uint32 VertexIndex = 0; VertexIndex < CTTessellation.VertexCount; ++VertexIndex, ++GlobalVertexIndex)
		{
			uint32 NewIndex = NewIndexOf[IndexOfCoincidentNode[GlobalVertexIndex]];
			CTTessellation.VertexIdSet[VertexIndex] = VertexDataSet[NewIndex].VertexID.GetValue();
		}
	}

	if (bIsSymmetricMesh)
	{

		GlobalVertexIndex = 0;
		for (FTessellationData& CTTessellation : FaceTessellationSet)
		{
			CTTessellation.SymVertexIdSet.SetNum(CTTessellation.VertexCount);
			for (uint32 VertexIndex = 0; VertexIndex < CTTessellation.VertexCount; ++VertexIndex, ++GlobalVertexIndex)
			{
				uint32 NewIndex = NewIndexOf[IndexOfCoincidentNode[GlobalVertexIndex]];
				CTTessellation.SymVertexIdSet[VertexIndex] = VertexDataSet[NewIndex].SymVertexID.GetValue();;
			}
		}
	}
}

void UpdatePolygonGroup(TMap<uint32, FPolygonGroupID>& MaterialToPolygonGroupMapping, TPolygonGroupAttributesRef<FName>& PolygonGroupImportedMaterialSlotNames, FMeshDescription& MeshDescription)
{
	if (MeshDescription.PolygonGroups().Num() > 0)
	{
		for (FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
		{
			FName initialSlotName = PolygonGroupImportedMaterialSlotNames[PolygonGroupID];
			uint32 MaterialHash = StaticCast<uint32>(FCString::Atoi64(*initialSlotName.ToString()));
			MaterialToPolygonGroupMapping.Add(MaterialHash, PolygonGroupID);
		}
	}

	for (auto& Material : MaterialToPolygonGroupMapping)
	{
		if (Material.Value == FPolygonGroupID::Invalid)
		{
			uint32 MaterialHash = Material.Key;
			FName ImportedSlotName = *LexToString<uint32>(MaterialHash);

			FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[PolyGroupID] = ImportedSlotName;
			Material.Value = PolyGroupID;
		}
	}
}

bool FillMesh(const FMeshParameters& MeshParameters, const FImportParameters& ImportParams, TArray<FTessellationData>& FaceTessellations, TMap<uint32, FPolygonGroupID> MaterialToPolygonGroupMapping, FMeshDescription& MeshDescription)
{
	const int32 UVChannel = 0;
	const int32 TriangleCount = 3;
	const TriangleIndex Clockwise = { 0, 1, 2 };
	const TriangleIndex CounterClockwise = { 0, 2, 1 };


	TArray<FVertexInstanceID> TriangleVertexInstanceIDs;
	TriangleVertexInstanceIDs.SetNum(TriangleCount);

	TArray<FVertexInstanceID> MeshVertexInstanceIDs;
	TArray<uint32> CTFaceIndex;  // new CT face index to remove degenerated face

	// Gather all array data
	FStaticMeshAttributes Attributes(MeshDescription);
	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	if ( !VertexInstanceNormals.IsValid() || !VertexInstanceTangents.IsValid() || !VertexInstanceBinormalSigns.IsValid() || !VertexInstanceColors.IsValid() || !VertexInstanceUVs.IsValid() || !PolygonGroupImportedMaterialSlotNames.IsValid() )
	{
		return false;
	}

	VertexInstanceUVs.SetNumIndices(1);

	int32 NbStep = 1;
	if (MeshParameters.bIsSymmetric)
	{
		NbStep = 2;
	}

	for (int32 Step = 0; Step < NbStep; ++Step)
	{
		// Swap mesh if needed
		const TriangleIndex& Orientation = (!MeshParameters.bNeedSwapOrientation == (bool)Step) ? CounterClockwise : Clockwise;

		for (FTessellationData& Tessellation : FaceTessellations)
		{
			// Get the polygonGroup
			const FPolygonGroupID* PolygonGroupID = MaterialToPolygonGroupMapping.Find(Tessellation.ColorName);
			if (PolygonGroupID == nullptr)
			{
				continue;
			}

			//int32 TriangleCount = IndicesCount / 3;
			int32 VertexIDs[3];
			FVector Temp3D = { 0, 0, 0 };
			FVector2D TexCoord2D = { 0, 0 };
			int32_t IndicesVertex[3];

			MeshVertexInstanceIDs.SetNum(Tessellation.IndexCount);
			CTFaceIndex.Reserve(Tessellation.IndexCount);
			CTFaceIndex.SetNum(0);

			TArray<int32>& VertexIdSet = (Step == 0) ? Tessellation.VertexIdSet : Tessellation.SymVertexIdSet;

			// build each valid face i.e. 3 different indexes
			for (uint32 Index = 0, NewIndex = 0; Index < Tessellation.IndexCount; Index += 3)
			{
				CopyValue(Tessellation.IndexArray.GetData(), Index, Tessellation.SizeOfIndexType, IndicesVertex);
				VertexIDs[Orientation[0]] = VertexIdSet[IndicesVertex[0]];
				VertexIDs[Orientation[1]] = VertexIdSet[IndicesVertex[1]];
				VertexIDs[Orientation[2]] = VertexIdSet[IndicesVertex[2]];

				// Verify the 3 input indices are not defining a degenerated triangle
				if (VertexIDs[0] == VertexIDs[1] || VertexIDs[0] == VertexIDs[2] || VertexIDs[1] == VertexIDs[2])
				{
					continue;
				}

				CTFaceIndex.Add(IndicesVertex[0]);
				CTFaceIndex.Add(IndicesVertex[1]);
				CTFaceIndex.Add(IndicesVertex[2]);

				TriangleVertexInstanceIDs[0] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID) VertexIDs[0]);
				TriangleVertexInstanceIDs[1] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID) VertexIDs[1]);
				TriangleVertexInstanceIDs[2] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID) VertexIDs[2]);

				// Add the triangle as a polygon to the mesh description
				const FPolygonID NewPolygonID = MeshDescription.CreatePolygon(*PolygonGroupID, TriangleVertexInstanceIDs);
			}

			// finalization of the mesh by setting colors, tangents, bi-normals, UV
			// for (uint32 Index = 0; Index <: CTFaceIndex)  // pour toutes les index des faces non degeneres de CT

			for (int32 IndexFace = 0; IndexFace < CTFaceIndex.Num(); IndexFace += 3)
			{
				for (int32 Index = 0; Index < TriangleCount; Index++)
				{
					FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[IndexFace + Orientation[Index]];

					VertexInstanceColors[VertexInstanceID] = FLinearColor::White;
					VertexInstanceTangents[VertexInstanceID] = FVector(ForceInitToZero);
					VertexInstanceBinormalSigns[VertexInstanceID] = 0.0f;
				}
			}

			if (Tessellation.TexCoordArray.Num())
			{
				for (int32 IndexFace = 0; IndexFace < CTFaceIndex.Num(); IndexFace += 3)
				{
					for (int32 Index = 0; Index < TriangleCount; Index++)
					{
						FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[IndexFace + Orientation[Index]];
						CopyValue(Tessellation.TexCoordArray.GetData(), CTFaceIndex[IndexFace + Index] * 2, Tessellation.SizeOfTexCoordType, false, Temp3D);
						VertexInstanceUVs.Set(VertexInstanceID, UVChannel, FVector2D(Temp3D));
					}
				}
			}

			if (Tessellation.NormalCount == 1)
			{
				CopyValue(Tessellation.NormalArray.GetData(), 0, Tessellation.SizeOfNormalType, true, Temp3D);
				Temp3D = FDatasmithUtils::ConvertVector((FDatasmithUtils::EModelCoordSystem) ImportParams.ModelCoordSys, Temp3D).GetSafeNormal();
				for (int32 Index = 0; Index < CTFaceIndex.Num(); Index++)
				{
					FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[Index];
					VertexInstanceNormals[VertexInstanceID] = Temp3D;
				}
			}
			else
			{
				for (int32 IndexFace = 0; IndexFace < CTFaceIndex.Num(); IndexFace += 3)
				{
					for (int32 Index = 0; Index < 3; Index++)
					{
						FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[IndexFace + Orientation[Index]];
						CopyValue(Tessellation.NormalArray.GetData(), CTFaceIndex[IndexFace + Index] * 3, Tessellation.SizeOfNormalType, true, Temp3D);
						VertexInstanceNormals[VertexInstanceID] = FDatasmithUtils::ConvertVector((FDatasmithUtils::EModelCoordSystem) ImportParams.ModelCoordSys, Temp3D).GetSafeNormal();
					}
				}
			}

			// compute normals
			if (Step)
			{
				FMatrix SymmetricMatrix;
				SymmetricMatrix = FDatasmithUtils::GetSymmetricMatrix(MeshParameters.SymmetricOrigin, MeshParameters.SymmetricNormal);
				for (int32 Index = 0; Index < CTFaceIndex.Num(); Index ++)
				{
					VertexInstanceNormals[MeshVertexInstanceIDs[Index]] = SymmetricMatrix.TransformVector(VertexInstanceNormals[MeshVertexInstanceIDs[Index]]);;
				}
			}

			if (MeshParameters.bNeedSwapOrientation)
			{
				for (int32 Index = 0; Index < CTFaceIndex.Num(); Index++)
				{
					VertexInstanceNormals[MeshVertexInstanceIDs[Index]] = VertexInstanceNormals[MeshVertexInstanceIDs[Index]] * -1.f;
				}
			}
		}
	}
	return true;
}

bool ConvertCTBodySetToMeshDescription(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, FBodyMesh& Body, FMeshDescription& MeshDescription)
{
	// Ref. CreateMesh(UDatasmithCADImportOptions* CADOptions, CTMesh& Mesh)
	MeshDescription.EdgeAttributes().RegisterAttribute<bool>(MeshAttribute::Edge::IsUVSeam, 1, false);

	// in a closed big mesh VertexCount ~ TriangleCount / 2, EdgeCount ~ 1.5* TriangleCount
	MeshDescription.ReserveNewVertexInstances(Body.TriangleCount*3);
	MeshDescription.ReserveNewPolygons(Body.TriangleCount);
	MeshDescription.ReserveNewEdges(Body.TriangleCount*3);

	// CoreTech is generating position duplicates. make sure to remove them before filling the mesh description
	TArray<FVertexID> RemapVertexPosition;
	FillVertexPosition(ImportParams, MeshParameters, Body.TriangleCount, Body.Faces, MeshDescription);

	TMap<uint32, FPolygonGroupID> MaterialToPolygonGroupMapping;
	for (const FTessellationData& FaceTessellation : Body.Faces)
	{
		// we assume that face has only color
		MaterialToPolygonGroupMapping.Add(FaceTessellation.ColorName, FPolygonGroupID::Invalid);
	}

	// Add the mesh's materials as polygon groups
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	UpdatePolygonGroup(MaterialToPolygonGroupMapping, PolygonGroupImportedMaterialSlotNames, MeshDescription);

	if (!FillMesh(MeshParameters, ImportParams, Body.Faces, MaterialToPolygonGroupMapping, MeshDescription))
	{
		return false;
	}

	// Orient mesh
	MeshOperator::OrientMesh(MeshDescription);

	// Build edge meta data
	FStaticMeshOperations::DetermineEdgeHardnessesFromVertexInstanceNormals(MeshDescription);

	return MeshDescription.Polygons().Num() > 0;
}

double Distance(CT_COORDINATE Point1, CT_COORDINATE Point2)
{
	return sqrt((Point2.xyz[0] - Point1.xyz[0]) * (Point2.xyz[0] - Point1.xyz[0]) + (Point2.xyz[1] - Point1.xyz[1]) * (Point2.xyz[1] - Point1.xyz[1]) + (Point2.xyz[2] - Point1.xyz[2]) * (Point2.xyz[2] - Point1.xyz[2]));
}

template<typename UVType>
void ScaleUV(CT_OBJECT_ID FaceID, FTessellationData& Tessellation, UVType Scale)
{
	UVType VMin, VMax, UMin, UMax;
	VMin = UMin = HUGE_VALF;
	VMax = UMax = -HUGE_VALF;
	UVType* UVSet = (UVType*)Tessellation.TexCoordArray;
	for (uint32 Index = 0, UVCoord = 0; Index < Tessellation.VertexCount; ++Index, UVCoord += 2)
	{
		UMin = FMath::Min(UVSet[UVCoord + 0], UMin);
		UMax = FMath::Max(UVSet[UVCoord + 0], UMax);
		VMin = FMath::Min(UVSet[UVCoord + 1], VMin);
		VMax = FMath::Max(UVSet[UVCoord + 1], VMax);
	}

	double PuMin, PuMax, PvMin, PvMax;
	PuMin = PvMin = HUGE_VALF;
	PuMax = PvMax = -HUGE_VALF;

	// fast UV min max
	CT_FACE_IO::AskUVminmax(FaceID, PuMin, PuMax, PvMin, PvMax);

	const uint32 NbIsoCurves = 7;

	// Compute Point grid on the restricted surface defined by [PuMin, PuMax], [PvMin, PvMax]
	CT_OBJECT_ID SurfaceID;
	CT_ORIENTATION Orientation;
	CT_FACE_IO::AskSurface(FaceID, SurfaceID, Orientation);

	CT_OBJECT_TYPE SurfaceType;
	CT_SURFACE_IO::AskType(SurfaceID, SurfaceType);

	UVType DeltaU = (PuMax - PuMin) / (NbIsoCurves - 1);
	UVType DeltaV = (PvMax - PvMin) / (NbIsoCurves - 1);
	UVType U = PuMin, V = PvMin;

	CT_COORDINATE NodeMatrix[121];

	for (int32 IndexI = 0; IndexI < NbIsoCurves; IndexI++)
	{
		for (int32 IndexJ = 0; IndexJ < NbIsoCurves; IndexJ++)
		{
			CT_SURFACE_IO::Evaluate(SurfaceID, U, V, NodeMatrix[IndexI*NbIsoCurves +IndexJ]);
			V += DeltaV;
		}
		U += DeltaU;
		V = PvMin;
	}

	// Compute length of 7 iso V line
	UVType LengthU[NbIsoCurves];
	UVType LengthUMin = HUGE_VAL;
	UVType LengthUMax = 0;
	UVType LengthUMed = 0;

	for (int32 IndexJ = 0; IndexJ < NbIsoCurves; IndexJ++)
	{
		LengthU[IndexJ] = 0;
		for (int32 IndexI = 0; IndexI < (NbIsoCurves - 1); IndexI++)
		{
			LengthU[IndexJ] += Distance(NodeMatrix[IndexI * NbIsoCurves + IndexJ], NodeMatrix[(IndexI + 1) * NbIsoCurves + IndexJ]);
		}
		LengthUMed += LengthU[IndexJ];
		LengthUMin = FMath::Min(LengthU[IndexJ], LengthUMin);
		LengthUMax = FMath::Max(LengthU[IndexJ], LengthUMax);
	}
	LengthUMed /= NbIsoCurves;
	LengthUMed = LengthUMed * 2 / 3 + LengthUMax / 3;

	// Compute length of 7 iso U line
	UVType LengthV[NbIsoCurves];
	UVType LengthVMin = HUGE_VAL;
	UVType LengthVMax = 0;
	UVType LengthVMed = 0;

	for (int32 IndexI = 0; IndexI < NbIsoCurves; IndexI++)
	{
		LengthV[IndexI] = 0;
		for (int32 IndexJ = 0; IndexJ < (NbIsoCurves - 1); IndexJ++)
		{
			LengthV[IndexI] += Distance(NodeMatrix[IndexI * NbIsoCurves + IndexJ], NodeMatrix[IndexI * NbIsoCurves + IndexJ + 1]);
		}
		LengthVMed += LengthV[IndexI];
		LengthVMin = FMath::Min(LengthV[IndexI], LengthVMin);
		LengthVMax = FMath::Max(LengthV[IndexI], LengthVMax);
	}
	LengthVMed /= NbIsoCurves;
	LengthVMed = LengthVMed * 2 / 3 + LengthVMax / 3;

	switch (SurfaceType)
	{
	case CT_CONE_TYPE:
	case CT_CYLINDER_TYPE:
	case CT_SPHERE_TYPE:
		Swap(LengthUMed, LengthVMed);
		break;
	case CT_S_REVOL_TYPE:
	case CT_TORUS_TYPE:
		// Need swap ?
		// Swap(LengthUMed, LengthVMed);
		break;
	case CT_S_NURBS_TYPE:
	case CT_PLANE_TYPE:
	case CT_S_OFFSET_TYPE:
	case CT_S_RULED_TYPE:
	case CT_TABULATED_RULED_TYPE:
	case CT_S_LINEARTRANSFO_TYPE:
	case CT_S_NONLINEARTRANSFO_TYPE:
	case CT_S_BLEND_TYPE:
	default:
		break;
	}

	// scale the UV map
	// 0.1 define UV in cm and not in mm
	UVType VScale = Scale * LengthVMed * 1 / (VMax - VMin) / 100;
	UVType UScale = Scale * LengthUMed * 1 / (UMax - UMin) / 100;

	for (uint32 Index = 0, UVCoord = 0; Index < Tessellation.VertexCount; ++Index, UVCoord += 2)
	{
		UVSet[UVCoord + 0] *= UScale;
		UVSet[UVCoord + 1] *= VScale;
	}
}

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial()
{
	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(TEXT("0"));
	MaterialElement->SetLabel(TEXT("DefaultCADImportMaterial"));

	FLinearColor LinearColor = FLinearColor::FromPow22Color(FColor(200, 200, 200, 255));
	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Base Color"));
	ColorExpression->GetColor() = LinearColor;
	MaterialElement->GetBaseColor().SetExpression(ColorExpression);
	MaterialElement->SetParentLabel(TEXT("M_DatasmithCAD"));

	return MaterialElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(const FColor& InColor)
{
	FString Name = FString::FromInt(BuildColorName(InColor));
	FString Label = FString::Printf(TEXT("color_%02x%02x%02x%02x"), InColor.R, InColor.G, InColor.B, InColor.A);

	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	MaterialElement->SetLabel(*Label);

	FLinearColor LinearColor = FLinearColor::FromPow22Color(InColor);

	IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
	ColorExpression->SetName(TEXT("Base Color"));
	ColorExpression->GetColor() = LinearColor;

	MaterialElement->GetBaseColor().SetExpression(ColorExpression);

	if (LinearColor.A < 1.0f)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);

		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = LinearColor.A;
		Scalar->SetName(TEXT("Opacity Level"));

		MaterialElement->GetOpacity().SetExpression(Scalar);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithCADTransparent"));
	}
	else
	{
		MaterialElement->SetParentLabel(TEXT("M_DatasmithCAD"));
	}

	return MaterialElement;
}

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(FCADMaterial& InMaterial, TSharedRef<IDatasmithScene> Scene)
{
	FString Name = FString::FromInt(BuildMaterialName(InMaterial));

	// Take the Material diffuse color and connect it to the BaseColor of a UEPbrMaterial
	TSharedRef<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(*Name);
	FString MaterialLabel(InMaterial.MaterialName);
	if (MaterialLabel.IsEmpty())
	{
		MaterialLabel = TEXT("Material");
	}
	MaterialElement->SetLabel(*MaterialLabel);

	// Set a diffuse color if there's nothing in the BaseColor
	if (MaterialElement->GetBaseColor().GetExpression() == nullptr)
	{
		FLinearColor LinearColor = FLinearColor::FromPow22Color(InMaterial.Diffuse);

		IDatasmithMaterialExpressionColor* ColorExpression = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionColor>();
		ColorExpression->SetName(TEXT("Base Color"));
		ColorExpression->GetColor() = LinearColor;

		MaterialElement->GetBaseColor().SetExpression(ColorExpression);
	}

	if (InMaterial.Transparency > 0.0f)
	{
		MaterialElement->SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
		IDatasmithMaterialExpressionScalar* Scalar = MaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
		Scalar->GetScalar() = InMaterial.Transparency;
		Scalar->SetName(TEXT("Opacity Level"));
		MaterialElement->GetOpacity().SetExpression(Scalar);
		MaterialElement->SetParentLabel(TEXT("M_DatasmithCADTransparent"));
	}
	else
	{
		MaterialElement->SetParentLabel(TEXT("M_DatasmithCAD"));
	}

	return MaterialElement;
}

uint32 GetSize(CT_TESS_DATA_TYPE type)
{
	switch (type)
	{
	case CT_TESS_USE_DEFAULT:
		return sizeof(uint32);
	case CT_TESS_UBYTE:
		return sizeof(uint8_t);
	case CT_TESS_BYTE:
		return sizeof(int8_t);
	case CT_TESS_USHORT:
		return sizeof(int16_t);
	case CT_TESS_SHORT:
		return sizeof(uint16_t);
	case CT_TESS_UINT:
		return sizeof(uint32);
	case CT_TESS_INT:
		return sizeof(int32);
	case CT_TESS_ULONG:
		return sizeof(uint64);
	case CT_TESS_LONG:
		return sizeof(int64);
	case CT_TESS_FLOAT:
		return sizeof(float);
	case CT_TESS_DOUBLE:
		return sizeof(double);
	}
	return 0;
}

CT_IO_ERROR Tessellate(CT_OBJECT_ID MainObjectId, const FImportParameters& ImportParams, FMeshDescription& MeshDesc, FMeshParameters& MeshParameters)
{
	CheckedCTError Result;

	CT_LIST_IO Objects;
	Result = CT_COMPONENT_IO::AskChildren(MainObjectId, Objects);
	if (!Result)
		return Result;

	SetCoreTechTessellationState(ImportParams);

	FString FullPath;
	FString CachePath;
	FCoreTechFileParser Parser = FCoreTechFileParser(FullPath, CachePath, ImportParams);

	FBodyMesh BodyMesh;
	BodyMesh.BodyID = 1;

	while (CT_OBJECT_ID BodyId = Objects.IteratorIter())
	{
		Parser.GetBodyTessellation(BodyId, BodyMesh, ImportParams, 0);
	}

	bool bTessellated = ConvertCTBodySetToMeshDescription(ImportParams, MeshParameters, BodyMesh, MeshDesc);

	CheckedCTError ConversionResult;
	if (!bTessellated)
	{
		ConversionResult.RaiseOtherError("Error during mesh conversion");
	}

	return ConversionResult;
}

}

#endif // CAD_LIBRARY
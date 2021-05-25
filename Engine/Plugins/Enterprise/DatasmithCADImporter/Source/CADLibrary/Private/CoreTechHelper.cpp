// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreTechHelper.h"


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

	void FillVertexPosition(const FImportParameters& ImportParams, const FMeshParameters& MeshParameters, FBodyMesh& Body, FMeshDescription& MeshDescription)
	{
		int32 TriangleCount = Body.TriangleCount;
		TArray<FTessellationData>& FaceTessellationSet = Body.Faces;

		TVertexAttributesRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

		// Create a list of vertex Z/index pairs
		TArray<FVertexData> VertexDataSet;
		VertexDataSet.Reserve(TriangleCount * 3);

		FVector Position;
		uint32 GlobalVertexCount = 0;
		for (FTessellationData& CTTessellation : FaceTessellationSet)
		{
			CTTessellation.StartVertexIndex = GlobalVertexCount;
			for (const FVector& Vertex : CTTessellation.VertexArray)
			{
				VertexDataSet.Emplace(GlobalVertexCount, Vertex * ImportParams.ScaleFactor);
				++GlobalVertexCount;
			}
		}
		VertexDataSet.SetNum(GlobalVertexCount);

		// Sort the vertices by z value
		VertexDataSet.Sort(FCompareVertexZ());

		TArray<int32> NewIndexOf;
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

			const FVector& PositionA = VertexDataSet[i].Coordinates;

			// only need to search forward, since we add pairs both ways
			for (int32 j = i + 1; j < VertexDataSet.Num(); j++)
			{
				if (FMath::Abs(VertexDataSet[j].Z - VertexDataSet[i].Z) > KINDA_SMALL_NUMBER)
				{
					break; // can't be any more duplicated
				}

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
			int32 RealIndex = VertexDataSet[VertexIndex].Index;

			// Vertex is outside bbox
			if (RealIndex < 0)
			{
				continue;
			}

			if (IndexOfCoincidentNode[RealIndex] != RealIndex)
			{
				continue;
			}

			FVertexID VertexID = MeshDescription.CreateVertex();
			VertexPositions[VertexID] = FDatasmithUtils::ConvertVector(ImportParams.ModelCoordSys, VertexDataSet[VertexIndex].Coordinates);
			VertexDataSet[VertexIndex].VertexID = VertexID;
		}

		if (bIsSymmetricMesh)
		{
			for (uint32 VertexIndex = 0; VertexIndex < GlobalVertexCount; ++VertexIndex)
			{
				int32 RealIndex = VertexDataSet[VertexIndex].Index;

				// Vertex is outside bbox
				if (RealIndex < 0)
				{
					continue;
				}

				if (IndexOfCoincidentNode[RealIndex] != RealIndex)
				{
					continue;
				}

				FVertexID VertexID = MeshDescription.CreateVertex();
				VertexPositions[VertexID] = FDatasmithUtils::ConvertVector(ImportParams.ModelCoordSys, VertexDataSet[VertexIndex].Coordinates);
				VertexPositions[VertexID] = SymmetricMatrix.TransformPosition(VertexPositions[VertexID]);

				VertexDataSet[VertexIndex].SymVertexID = VertexID;
			}
		}

		// For each face, for each vertex set VertexId
		GlobalVertexIndex = 0;
		for (FTessellationData& CTTessellation : FaceTessellationSet)
		{
			CTTessellation.VertexIdSet.SetNum(CTTessellation.VertexArray.Num());
			for (int32 VertexIndex = 0; VertexIndex < CTTessellation.VertexArray.Num(); ++VertexIndex, ++GlobalVertexIndex)
			{
				int32 NewIndex = NewIndexOf[IndexOfCoincidentNode[GlobalVertexIndex]];
				CTTessellation.VertexIdSet[VertexIndex] = VertexDataSet[NewIndex].VertexID.GetValue();
			}
		}

		if (bIsSymmetricMesh)
		{

			GlobalVertexIndex = 0;
			for (FTessellationData& CTTessellation : FaceTessellationSet)
			{
				CTTessellation.SymVertexIdSet.SetNum(CTTessellation.VertexArray.Num());
				for (int32 VertexIndex = 0; VertexIndex < CTTessellation.VertexArray.Num(); ++VertexIndex, ++GlobalVertexIndex)
				{
					uint32 NewIndex = NewIndexOf[IndexOfCoincidentNode[GlobalVertexIndex]];
					CTTessellation.SymVertexIdSet[VertexIndex] = VertexDataSet[NewIndex].SymVertexID.GetValue();
				}
			}
		}
	}


	// PolygonAttributes name used into modeling tools (ExtendedMeshAttribute::PolyTriGroups)
	const FName PolyTriGroups("PolyTriGroups");

	// Copy of FMeshDescriptionBuilder::EnablePolyGroups()
	TPolygonAttributesRef<int32> EnableCADPatchGroups(FMeshDescription& MeshDescription)
	{
		TPolygonAttributesRef<int32> PatchGroups = MeshDescription.PolygonAttributes().GetAttributesRef<int32>(PolyTriGroups);
		if (PatchGroups.IsValid() == false)
		{
			MeshDescription.PolygonAttributes().RegisterAttribute<int32>(PolyTriGroups, 1, 0, EMeshAttributeFlags::AutoGenerated);
			PatchGroups = MeshDescription.PolygonAttributes().GetAttributesRef<int32>(PolyTriGroups);
			check(PatchGroups.IsValid());
		}
		return PatchGroups;
	}

	 /**
	  * Polygon group is an attribute of polygons.As long as the mesh description is empty(no polygon), polygon group cannot be defined.
	  * The work around used is to create PolygonGroups and to set them a PolygonGroupAttributes.
	  * To get the existing polygon groups, for each created polygonGroups the PolygonGroupId is got(see GetExistingPatches)
	  * Warning: CopyPatchGroups is call in FCoreTechRetessellate_Impl::ApplyOnOneAsset(CoreTechRetessellateAction.cpp) only if the option 
	  * RetessellateOptions.RetessellationRule equal EDatasmithCADRetessellationRule::SkipDeletedSurfaces
	  */
	void CopyPatchGroups(FMeshDescription& MeshSource, FMeshDescription& MeshDestination)
	{
		TPolygonGroupAttributesRef<int32> PatchGroups = MeshDestination.PolygonGroupAttributes().GetAttributesRef<int32>(PolyTriGroups);
		if (PatchGroups.IsValid() == false)
		{
			MeshDestination.PolygonGroupAttributes().RegisterAttribute<int32>(PolyTriGroups, 1, 0, EMeshAttributeFlags::AutoGenerated);
			PatchGroups = MeshDestination.PolygonGroupAttributes().GetAttributesRef<int32>(PolyTriGroups);
			check(PatchGroups.IsValid());
		}

		TSet<int32> PatchIdSet;
		TPolygonAttributesRef<int32> ElementToGroups = MeshSource.PolygonAttributes().GetAttributesRef<int32>(PolyTriGroups);
		for (const FPolygonID TriangleID : MeshSource.Polygons().GetElementIDs())
		{
			int32 PatchId = ElementToGroups[TriangleID];

			bool bIsAlreadyInSet = false;
			PatchIdSet.Add(PatchId, &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				const FPolygonGroupID PolygonID = MeshDestination.CreatePolygonGroup();
				PatchGroups[PolygonID] = PatchId;
			}
		}
	}

	/**
	 * See CopyPatchGroups
	 */
	void GetExistingPatches(FMeshDescription& MeshDestination, TSet<int32>& OutPatchIdSet)
	{
		TPolygonGroupAttributesRef<int32> PatchGroups = MeshDestination.PolygonGroupAttributes().GetAttributesRef<int32>(PolyTriGroups);
		if (PatchGroups.IsValid() == false)
		{
			return;
		}

		for (const FPolygonGroupID PolygonGroupID : MeshDestination.PolygonGroups().GetElementIDs())
		{
			int32 PatchId = PatchGroups[PolygonGroupID];
			if (PatchId > 0) 
			{
				OutPatchIdSet.Add(PatchId);
			}
		}
	}

	bool FillMesh(const FMeshParameters& MeshParameters, const FImportParameters& ImportParams, TArray<FTessellationData>& FaceTessellations, FMeshDescription& MeshDescription)
	{
		const int32 UVChannel = 0;
		const int32 TriangleCount = 3;
		const TriangleIndex Clockwise = { 0, 1, 2 };
		const TriangleIndex CounterClockwise = { 0, 2, 1 };
		const int32 InvalidID = FVertexID::Invalid.GetValue();


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

		if (!VertexInstanceNormals.IsValid() || !VertexInstanceTangents.IsValid() || !VertexInstanceBinormalSigns.IsValid() || !VertexInstanceColors.IsValid() || !VertexInstanceUVs.IsValid() || !PolygonGroupImportedMaterialSlotNames.IsValid())
		{
			return false;
		}

		// Find all the materials used
		TMap<uint32, FPolygonGroupID> MaterialToPolygonGroupMapping;
		for (const FTessellationData& FaceTessellation : FaceTessellations)
		{
			// we assume that face has only color
			MaterialToPolygonGroupMapping.Add(FaceTessellation.ColorName, FPolygonGroupID::Invalid);
		}

		// Add to the mesh, a polygon groups per material
		for (auto& Material : MaterialToPolygonGroupMapping)
		{
			uint32 MaterialHash = Material.Key;
			FName ImportedSlotName = *LexToString<uint32>(MaterialHash);

			FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[PolyGroupID] = ImportedSlotName;
			Material.Value = PolyGroupID;
		}

		VertexInstanceUVs.SetNumIndices(1);

		int32 NbStep = 1;
		if (MeshParameters.bIsSymmetric)
		{
			NbStep = 2;
		}

		TSet<int32> PatchIdSet;
		GetExistingPatches(MeshDescription, PatchIdSet);
		bool bImportOnlyAlreadyPresent = (bool) PatchIdSet.Num();

		TPolygonAttributesRef<int32> PatchGroups = EnableCADPatchGroups(MeshDescription);
		for (int32 Step = 0; Step < NbStep; ++Step)
		{
			// Swap mesh if needed
			const TriangleIndex& Orientation = (!MeshParameters.bNeedSwapOrientation == (bool)Step) ? CounterClockwise : Clockwise;

			for (FTessellationData& Tessellation : FaceTessellations)
			{
				if (bImportOnlyAlreadyPresent && !PatchIdSet.Contains(Tessellation.PatchId))
				{
					continue;
				}

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

				MeshVertexInstanceIDs.SetNum(Tessellation.IndexArray.Num());
				CTFaceIndex.Reserve(Tessellation.IndexArray.Num());
				CTFaceIndex.SetNum(0);

				TArray<int32>& VertexIdSet = (Step == 0) ? Tessellation.VertexIdSet : Tessellation.SymVertexIdSet;

				// build each valid face i.e. 3 different indexes
				for (int32 Index = 0, NewIndex = 0; Index < Tessellation.IndexArray.Num(); Index += 3)
				{
					VertexIDs[Orientation[0]] = VertexIdSet[Tessellation.IndexArray[Index + 0]];
					VertexIDs[Orientation[1]] = VertexIdSet[Tessellation.IndexArray[Index + 1]];
					VertexIDs[Orientation[2]] = VertexIdSet[Tessellation.IndexArray[Index + 2]];

					if (VertexIDs[0] == InvalidID || VertexIDs[1] == InvalidID || VertexIDs[2] == InvalidID)
					{
						continue;
					}

					// Verify the 3 input indices are not defining a degenerated triangle
					if (VertexIDs[0] == VertexIDs[1] || VertexIDs[0] == VertexIDs[2] || VertexIDs[1] == VertexIDs[2])
					{
						continue;
					}

					CTFaceIndex.Add(Tessellation.IndexArray[Index + 0]);
					CTFaceIndex.Add(Tessellation.IndexArray[Index + 1]);
					CTFaceIndex.Add(Tessellation.IndexArray[Index + 2]);

					TriangleVertexInstanceIDs[0] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID)VertexIDs[0]);
					TriangleVertexInstanceIDs[1] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID)VertexIDs[1]);
					TriangleVertexInstanceIDs[2] = MeshVertexInstanceIDs[NewIndex++] = MeshDescription.CreateVertexInstance((FVertexID)VertexIDs[2]);

					// Add the triangle as a polygon to the mesh description
					const FPolygonID PolygonID = MeshDescription.CreatePolygon(*PolygonGroupID, TriangleVertexInstanceIDs);
					// Set patch id attribute
					PatchGroups[PolygonID] = Tessellation.PatchId;
				}

				// finalization of the mesh by setting colors, tangents, bi-normals, UV
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
							VertexInstanceUVs.Set(VertexInstanceID, UVChannel, Tessellation.TexCoordArray[CTFaceIndex[IndexFace + Index]]);
						}
					}
				}

				if (!Step)
				{
					FDatasmithUtils::ConvertVectorArray(ImportParams.ModelCoordSys, Tessellation.NormalArray);
					for (FVector& Normal : Tessellation.NormalArray)
					{
						Normal = Normal.GetSafeNormal();
					}
				}

				if (Tessellation.NormalArray.Num() == 1)
				{
					Temp3D = Tessellation.NormalArray[0];
					for (int32 Index = 0; Index < CTFaceIndex.Num(); Index++)
					{
						FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[Index];
						VertexInstanceNormals[VertexInstanceID] = Temp3D;
					}
				}
				else
				{
					for (FVector& Normal : Tessellation.NormalArray)
					{
						Normal = Normal.GetSafeNormal();
					}

					for (int32 IndexFace = 0; IndexFace < CTFaceIndex.Num(); IndexFace += 3)
					{
						for (int32 Index = 0; Index < 3; Index++)
						{
							FVertexInstanceID VertexInstanceID = MeshVertexInstanceIDs[IndexFace + Orientation[Index]];
							VertexInstanceNormals[VertexInstanceID] = Tessellation.NormalArray[CTFaceIndex[IndexFace + Index]];
						}
					}
				}

				// compute normals
				if (Step)
				{
					FMatrix SymmetricMatrix;
					SymmetricMatrix = FDatasmithUtils::GetSymmetricMatrix(MeshParameters.SymmetricOrigin, MeshParameters.SymmetricNormal);
					for (int32 Index = 0; Index < CTFaceIndex.Num(); Index++)
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
		// Ref. CreateMesh(UDatasmithCADImportOptions* CADOptions, FCTMesh& Mesh)
		MeshDescription.EdgeAttributes().RegisterAttribute<bool>(MeshAttribute::Edge::IsUVSeam, 1, false);

		// in a closed big mesh VertexCount ~ TriangleCount / 2, EdgeCount ~ 1.5* TriangleCount
		MeshDescription.ReserveNewVertexInstances(Body.TriangleCount * 3);
		MeshDescription.ReserveNewPolygons(Body.TriangleCount);
		MeshDescription.ReserveNewEdges(Body.TriangleCount * 3);

		// CoreTech is generating position duplicates. make sure to remove them before filling the mesh description
		TArray<FVertexID> RemapVertexPosition;
		FillVertexPosition(ImportParams, MeshParameters, Body, MeshDescription);

		if (!FillMesh(MeshParameters, ImportParams, Body.Faces, MeshDescription))
		{
			return false;
		}

		// Orient mesh
		MeshOperator::OrientMesh(MeshDescription);

		// Build edge meta data
		FStaticMeshOperations::DetermineEdgeHardnessesFromVertexInstanceNormals(MeshDescription);

		return MeshDescription.Polygons().Num() > 0;
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

		FLinearColor LinearColor = FLinearColor::FromSRGBColor(InColor);

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

	bool Tessellate(uint64 MainObjectId, const FImportParameters& ImportParams, FMeshDescription& MeshDesc, FMeshParameters& MeshParameters)
	{
		CTKIO_SetCoreTechTessellationState(ImportParams);

		FBodyMesh BodyMesh;
		BodyMesh.BodyID = 1;

		CTKIO_GetTessellation(MainObjectId, BodyMesh, false);

		if (BodyMesh.Faces.Num() == 0)
		{
			return false;
		}

		if (!ConvertCTBodySetToMeshDescription(ImportParams, MeshParameters, BodyMesh, MeshDesc))
		{
			ensureMsgf(false, TEXT("Error during mesh conversion"));
			return false;
		}

		return true;
	}

	bool LoadFile(const FString& FileName, FMeshDescription& MeshDescription, const FImportParameters& ImportParameters, FMeshParameters& MeshParameters)
	{
		FCoreTechSessionBase Session(TEXT("CoreTechMeshLoader::LoadFile"));
		if (!Session.IsSessionValid())
		{
			return false;
		}

		uint64 MainObjectID;
		CTKIO_ChangeUnit(ImportParameters.MetricUnit);
		if(!CTKIO_LoadModel(*FileName, MainObjectID, 0x00020000 /* CT_LOAD_FLAGS_READ_META_DATA */))
		{
			// Something wrong happened during the load, abort
			return false;
		}

		if (ImportParameters.StitchingTechnique != StitchingNone)
		{
			CTKIO_Repair(MainObjectID, StitchingSew);
		}

		return Tessellate(MainObjectID, ImportParameters, MeshDescription, MeshParameters);
	}

}

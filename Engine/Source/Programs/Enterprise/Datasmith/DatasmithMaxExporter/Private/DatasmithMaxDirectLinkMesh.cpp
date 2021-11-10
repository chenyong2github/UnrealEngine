// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithMesh.h"
#include "DatasmithUtils.h"
#include "DatasmithMaxClassIDs.h"
#include "DatasmithMaxMeshExporter.h"
#include "DatasmithMaxSceneExporter.h"
#include "DatasmithMaxAttributes.h"

#include "Logging/LogMacros.h"


#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "max.h"

	#include "modstack.h"
	#include "iparamb2.h"
	#include "MeshNormalSpec.h"
MAX_INCLUDES_END

namespace DatasmithMaxDirectLink
{

class FNullView : public View
{
public:
	FNullView()
	{
		worldToView.IdentityMatrix(); screenW = 640.0f; screenH = 480.0f;
	}

	virtual Point2 ViewToScreen(Point3 p) override
	{
		return Point2(p.x, p.y);
	}
};


FRenderMeshForConversion GetMeshForGeomObject(INode* Node, Object* Obj)
{
	// todo: baseline exporter uses GetBaseObject which takes result of EvalWorldState
	// and searched down DerivedObject pipeline(by taking GetObjRef) 
	// This is STRANGE as EvalWorldState shouldn't return DerivedObject in the first place(it should return result of pipeline evaluation)

	GeomObject* GeomObj = dynamic_cast<GeomObject*>(Obj);

	FNullView View;
	TimeValue Time = GetCOREInterface()->GetTime();
	BOOL bNeedsDelete;
	Mesh* RenderMesh = GeomObj->GetRenderMesh(Time, Node, View, bNeedsDelete);

	return FRenderMeshForConversion(Node, RenderMesh, bNeedsDelete);
}

FRenderMeshForConversion GetMeshForNode(INode* Node, FTransform Pivot)
{
	if (!Node)
	{
		return FRenderMeshForConversion();
	}
	BOOL bNeedsDelete;
	Mesh* RenderMesh = GetMeshFromRenderMesh(Node, bNeedsDelete, GetCOREInterface()->GetTime());
	return FRenderMeshForConversion(Node, RenderMesh, bNeedsDelete, Pivot);
}

/** Convert Max to UE coordinates, handle scene master unit
 * @param MaxTransform      source transform
 * @param UnitMultiplier    Master scene unit
 */
FTransform FTransformFromMatrix3(const Matrix3& MaxTransform, float UnitMultiplier)
{
	FVector Translation;
	FQuat Rotation;
	FVector Scale;
	FDatasmithMaxSceneExporter::MaxToUnrealCoordinates( MaxTransform, Translation, Rotation, Scale, UnitMultiplier);
	return FTransform( Rotation, Translation, Scale );
}

FRenderMeshForConversion GetMeshForCollision(INode* Node)
{
	// source: FDatasmithMaxMeshExporter::ExportMesh
	FDatasmithConverter Converter;
	bool bIsCollisionFromDatasmithAttributes;
	TOptional<FDatasmithMaxStaticMeshAttributes> DatasmithAttributes = FDatasmithMaxStaticMeshAttributes::ExtractStaticMeshAttributes(Node);
	INode* CollisionNode = FDatasmithMaxMeshExporter::GetCollisionNode(Node, DatasmithAttributes ? &DatasmithAttributes.GetValue() : nullptr, bIsCollisionFromDatasmithAttributes);
	FTransform CollisionPivot;
	if (CollisionNode)
	{

		TSet<uint16> CollisionSupportedChannels;

		bool bBakePivot = false; // todo: bake collision pivot if render mesh pivot is baked

		FTransform ColliderPivot = FDatasmithMaxSceneExporter::GetPivotTransform(CollisionNode, Converter.UnitToCentimeter);

		if (bIsCollisionFromDatasmithAttributes)
		{
			if (!bBakePivot)
			{
				FTransform RealPivot = FDatasmithMaxSceneExporter::GetPivotTransform(Node, Converter.UnitToCentimeter);
				ColliderPivot = ColliderPivot * RealPivot.Inverse();
			}
			CollisionPivot = ColliderPivot;
		}
		else
		{

			TimeValue Now = GetCOREInterface()->GetTime();
			FTransform FTransformFromMatrix3(const Matrix3& MaxTransform, float UnitMultiplier); // todo: move to header and rename(F is for class!)
			FTransform NodeWTM = FTransformFromMatrix3(Node->GetNodeTM(Now), Converter.UnitToCentimeter);
			FTransform ColliderNodeWTM = FTransformFromMatrix3(CollisionNode->GetNodeTM(Now), Converter.UnitToCentimeter);

			// if object-offset has been baked into the mesh data, we want collision mesh data in the mesh's node space
			//   MeshVert_Node = RealPivot * max_vert_data
			//   MeshVert_world = NodeWTM * MeshVert_Node
			// Collision mesh vertices in world space:
			//   CollVert_node = ColliderPivot * CollVert_obj
			//   CollVert_world = ColliderNodeWTM * CollVert_node
			//   CollVert_mesh = NodeWTM-1 * CollVert_world
			FTransform BakedTransform = ColliderPivot * ColliderNodeWTM * NodeWTM.Inverse();

			if (!bBakePivot)
			{
				// if object-offset has not been baked, we want collision mesh data in the mesh's object space
				FTransform RealPivot = FDatasmithMaxSceneExporter::GetPivotTransform(Node, Converter.UnitToCentimeter);
				BakedTransform = BakedTransform * RealPivot.Inverse();
			}

			CollisionPivot = BakedTransform;
		}
	}
	return GetMeshForNode(CollisionNode, CollisionPivot);
}



// todo: copied from baseline plugin(it has dependencies on converters that are not static in FDatasmithMaxMeshExporter)
void FillDatasmithMeshFromMaxMesh(FDatasmithMesh& DatasmithMesh, Mesh& MaxMesh, INode* ExportedNode, bool bForceSingleMat, TSet<uint16>& SupportedChannels, const TCHAR* MeshName, FTransform Pivot)
{
	FDatasmithConverter Converter;

	const int NumFaces = MaxMesh.getNumFaces();
	const int NumVerts = MaxMesh.getNumVerts();

	DatasmithMesh.SetVerticesCount(NumVerts);
	DatasmithMesh.SetFacesCount(NumFaces);

	// Vertices
	for (int i = 0; i < NumVerts; i++)
	{
		Point3 Point = MaxMesh.getVert(i);

		FVector Vertex = Converter.toDatasmithVector(Point);
		Vertex = Pivot.TransformPosition(Vertex); // Bake object-offset in the mesh data when possible

		DatasmithMesh.SetVertex(i, Vertex.X, Vertex.Y, Vertex.Z);
	}

	// Vertex Colors
	if (MaxMesh.curVCChan == 0 && MaxMesh.numCVerts > 0)
	{
		// Default vertex color channel
		for (int32 i = 0; i < NumFaces; i++)
		{
			TVFace& Face = MaxMesh.vcFace[i];
			DatasmithMesh.SetVertexColor(i * 3, Converter.toDatasmithColor(MaxMesh.vertCol[Face.t[0]]));
			DatasmithMesh.SetVertexColor(i * 3 + 1, Converter.toDatasmithColor(MaxMesh.vertCol[Face.t[1]]));
			DatasmithMesh.SetVertexColor(i * 3 + 2, Converter.toDatasmithColor(MaxMesh.vertCol[Face.t[2]]));
		}
	}

	// UVs
	TMap<int32, int32> UVChannelsMap;
	TMap<uint32, int32> HashToChannel;
	bool bIsFirstUVChannelValid = true;

	for (int32 i = 1; i <= MaxMesh.getNumMaps(); ++i)
	{
		if (MaxMesh.mapSupport(i) == BOOL(true) && MaxMesh.getNumMapVerts(i) > 0)
		{
			DatasmithMesh.AddUVChannel();
			const int32 UVChannelIndex = DatasmithMesh.GetUVChannelsCount() - 1;
			const int32 UVsCount = MaxMesh.getNumMapVerts(i);

			DatasmithMesh.SetUVCount(UVChannelIndex, UVsCount);

			UVVert* Vertex = MaxMesh.mapVerts(i);

			for (int32 j = 0; j < UVsCount; ++j)
			{
				const UVVert& MaxUV = Vertex[j];
				DatasmithMesh.SetUV(UVChannelIndex, j, MaxUV.x, 1.f - MaxUV.y);
			}

			TVFace* Faces = MaxMesh.mapFaces(i);
			for (int32 j = 0; j < MaxMesh.getNumFaces(); ++j)
			{
				DatasmithMesh.SetFaceUV(j, UVChannelIndex, Faces[j].t[0], Faces[j].t[1], Faces[j].t[2]);
			}

			if (UVChannelIndex == 0)
			{
				//Verifying that the UVs are properly unfolded, which is required to calculate the tangent in unreal.
				bIsFirstUVChannelValid = FDatasmithMeshUtils::IsUVChannelValid(DatasmithMesh, UVChannelIndex);
			}

			uint32 Hash = DatasmithMesh.GetHashForUVChannel(UVChannelIndex);
			int32* PointerToChannel = HashToChannel.Find(Hash);

			if (PointerToChannel)
			{
				// Remove the channel because there is another one that is identical
				DatasmithMesh.RemoveUVChannel();

				// Map the user-specified UV Channel (in 3dsmax) to the actual UV channel that will be exported to Unreal
				UVChannelsMap.Add(i - 1, *PointerToChannel);
			}
			else
			{
				// Map the user-specified UV Channel (in 3dsmax) to the actual UV channel that will be exported to Unreal
				UVChannelsMap.Add(i - 1, UVChannelIndex);
				HashToChannel.Add(Hash, UVChannelIndex);
			}
		}
	}

	if (!bIsFirstUVChannelValid)
	{
		//DatasmithMaxLogger::Get().AddGeneralError(*FString::Printf(TEXT("%s's UV channel #1 contains degenerated triangles, this can cause issues in Unreal. It is recommended to properly unfold and flatten exported UV data.")
		//	, static_cast<const TCHAR*>(ExportedNode->GetName())));
	}

	if (MeshName != nullptr)
	{
		//MeshNamesToUVChannels.Add(MeshName, MoveTemp(UVChannelsMap));
	}

	// Faces
	for (int i = 0; i < NumFaces; i++)
	{
		// Create polygons. Assign texture and texture UV indices.
		// all faces of the cube have the same texture

		Face& MaxFace = MaxMesh.faces[i];
		int MaterialId = bForceSingleMat ? 0 : MaxFace.getMatID();

		SupportedChannels.Add(MaterialId);

		//Max's channel UI is not zero-based, so we register an incremented ChannelID for better visual consistency after importing in Unreal.
		DatasmithMesh.SetFace(i, MaxFace.getVert(0), MaxFace.getVert(1), MaxFace.getVert(2), MaterialId + 1);
		DatasmithMesh.SetFaceSmoothingMask(i, (uint32)MaxFace.getSmGroup());
	}

	//Normals

	MaxMesh.SpecifyNormals();
	MeshNormalSpec* Normal = MaxMesh.GetSpecifiedNormals();
	Normal->MakeNormalsExplicit(false);
	Normal->CheckNormals();

	Matrix3 RotationMatrix;
	RotationMatrix.IdentityMatrix();
	Quat ObjectOffsetRotation = ExportedNode->GetObjOffsetRot();
	RotateMatrix(RotationMatrix, ObjectOffsetRotation);

	Point3 Point;

	for (int i = 0; i < NumFaces; i++)
	{
		Point = Normal->GetNormal(i, 0).Normalize() * RotationMatrix;
		FVector NormalVector = Converter.toDatasmithVector(Point);
		DatasmithMesh.SetNormal(i * 3, NormalVector.X, NormalVector.Y, NormalVector.Z);

		Point = Normal->GetNormal(i, 1).Normalize() * RotationMatrix;
		NormalVector = Converter.toDatasmithVector(Point);
		DatasmithMesh.SetNormal(i * 3 + 1, NormalVector.X, NormalVector.Y, NormalVector.Z);

		Point = Normal->GetNormal(i, 2).Normalize() * RotationMatrix;
		NormalVector = Converter.toDatasmithVector(Point);
		DatasmithMesh.SetNormal(i * 3 + 2, NormalVector.X, NormalVector.Y, NormalVector.Z);
	}
}

bool CreateDatasmithMeshFromMaxMesh(FDatasmithMesh& DatasmithMesh, INode* Node, const TCHAR* MeshName, const FRenderMeshForConversion& RenderMesh, TSet<uint16>& SupportedChannels)
{
	bool bResult = false;
	if (RenderMesh.GetMesh()->getNumFaces())
	{
		// Copy mesh to clean it before filling Datasmith mesh from it
		Mesh CachedMesh;
		CachedMesh.DeepCopy(RenderMesh.GetMesh(), TOPO_CHANNEL | GEOM_CHANNEL | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL);

		CachedMesh.DeleteIsoVerts();
		CachedMesh.RemoveDegenerateFaces();
		CachedMesh.RemoveIllegalFaces();

		// Need to invalidate/rebuild strips/edges after topology change(removing bad verts/faces)
		CachedMesh.InvalidateStrips();
		CachedMesh.BuildStripsAndEdges();

		if (CachedMesh.getNumFaces() > 0)
		{
			// todo: pivot
			FillDatasmithMeshFromMaxMesh(DatasmithMesh, CachedMesh, Node, false, SupportedChannels, MeshName, RenderMesh.GetPivot());

			bResult = true; // Set to true, don't care what ExportToUObject does here - we need to move it to a thread anyway
		}
		CachedMesh.FreeAll();
	}
	return bResult;
}



// todo: paralelize calls to ExportToUObject 
bool ConvertMaxMeshToDatasmith(ISceneTracker& Scene, TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement, INode* Node, const TCHAR* MeshName, const FRenderMeshForConversion& RenderMesh, TSet<uint16>& SupportedChannels, const FRenderMeshForConversion& CollisionMesh)
{
	// Reset old mesh
	if (DatasmithMeshElement)
	{
		// todo: potential mesh reuse - when DatasmithMeshElement allows to reset materials(as well as other params)
		Scene.ReleaseMeshElement(DatasmithMeshElement);
	}

	FDatasmithMesh DatasmithMesh;
	if (!CreateDatasmithMeshFromMaxMesh(DatasmithMesh, Node, MeshName, RenderMesh, SupportedChannels))
	{
		return false;
	}

	DatasmithMeshElement = FDatasmithSceneFactory::CreateMesh(MeshName);

	FDatasmithMesh* DatasmithCollisionMeshPtr = nullptr;
	FDatasmithMesh DatasmithCollisionMesh;
	if (CollisionMesh.IsValid())
	{
		if (CreateDatasmithMeshFromMaxMesh(DatasmithCollisionMesh, CollisionMesh.GetNode(), nullptr, CollisionMesh, SupportedChannels))
		{
			DatasmithCollisionMeshPtr = &DatasmithCollisionMesh;
		}
	}

	Scene.AddMeshElement(DatasmithMeshElement, DatasmithMesh, DatasmithCollisionMeshPtr);
	return true;
}

// todo: copied from DatasmithMaxMeshExporter.cpp

Object* GetBaseObject(INode* Node, TimeValue Time)
{
	ObjectState ObjState = Node->EvalWorldState(Time);
	Object* Obj = ObjState.obj;

	if (Obj)
	{
		SClass_ID SuperClassID;
		SuperClassID = Obj->SuperClassID();
		while (SuperClassID == GEN_DERIVOB_CLASS_ID)
		{
			Obj = ((IDerivedObject*)Obj)->GetObjRef();
			SuperClassID = Obj->SuperClassID();
		}
	}

	return Obj;
}
#define VRAY_PROXY_DISPLAY_AS_MESH	4	// Value to set on a VRay Mesh Proxy to get the mesh

int SetObjectParamValue(Object *Obj, const FString& ParamName, int DesiredValue)
{
	bool bFoundDisplayValue = false;
	int PrevDisplayValue = DesiredValue; // Display value to see mesh
	int NumParamBlocks = Obj->NumParamBlocks();
	for (short BlockIndex = 0; BlockIndex < NumParamBlocks && !bFoundDisplayValue; ++BlockIndex)
	{
		IParamBlock2* ParamBlock2 = Obj->GetParamBlockByID(BlockIndex);
		ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();
		for (int ParamIndex = 0; ParamIndex < ParamBlockDesc->count; ++ParamIndex)
		{
			ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[ParamIndex];
			if (FCString::Stricmp(ParamDefinition.int_name, *ParamName) == 0)
			{
				PrevDisplayValue = ParamBlock2->GetInt(ParamDefinition.ID, GetCOREInterface()->GetTime());
				if (PrevDisplayValue != DesiredValue)
				{
					ParamBlock2->SetValue(ParamDefinition.ID, GetCOREInterface()->GetTime(), DesiredValue);
				}
				bFoundDisplayValue = true;
				break;
			}
		}
		ParamBlock2->ReleaseDesc();
	}

	return PrevDisplayValue;
}


Mesh* GetMeshFromRenderMesh(INode* Node, BOOL& bNeedsDelete, TimeValue CurrentTime)
{
	Object* Obj = GetBaseObject(Node, CurrentTime);
	const Class_ID& ObjectClassID = Obj->ClassID();
	const FString VRayProxyParamName(TEXT("display"));
	const FString BodyObjectViewportMeshParamName(TEXT("RenderViewportMeshRA"));
	int PreviousMeshDisplayValue = 0;

	if (ObjectClassID == VRAYPROXY_CLASS_ID)
	{
		// Need the high resolution render mesh associated with the VRay Mesh Proxy for the export
		PreviousMeshDisplayValue = SetObjectParamValue(Obj, VRayProxyParamName, VRAY_PROXY_DISPLAY_AS_MESH);
	}
	else if(ObjectClassID == BODYOBJECT_CLASS_ID)
	{
		// Need to make sure we are using the viewport mesh on BodyObject, otherwise the RenderMesh gives a tessellated low resolution mesh.
		PreviousMeshDisplayValue = SetObjectParamValue(Obj, BodyObjectViewportMeshParamName, 1);
	}

	GeomObject* GeomObj = static_cast<GeomObject*>(Obj);
	if (GeomObj == nullptr)
	{
		return nullptr;
	}

	FNullView View;
	Mesh* RenderMesh = GeomObj->GetRenderMesh(CurrentTime, Node, View, bNeedsDelete);

	// Restore display state if different from mesh display
	if (ObjectClassID == VRAYPROXY_CLASS_ID && PreviousMeshDisplayValue != VRAY_PROXY_DISPLAY_AS_MESH)
	{
		SetObjectParamValue(Obj, VRayProxyParamName, PreviousMeshDisplayValue);
	}
	else if(ObjectClassID == BODYOBJECT_CLASS_ID && PreviousMeshDisplayValue != 1)
	{
		SetObjectParamValue(Obj, BodyObjectViewportMeshParamName, PreviousMeshDisplayValue);
	}

	return RenderMesh;
}


}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN

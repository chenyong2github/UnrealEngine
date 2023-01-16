// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshKinematicInitializationNodes.h"

#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/FleshCollectionUtility.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Engine/SkeletalMesh.h"
#include "Dataflow/DataflowInputOutput.h"
#include "GeometryCollection/Facades/CollectionKinematicBindingFacade.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "GeometryCollection/Facades/CollectionVertexBoneWeightsFacade.h"
#include "GeometryCollection/TransformCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosFleshKinematicInitializationNodes)

//DEFINE_LOG_CATEGORY_STATIC(FKinematicInitializationNodesLog, Log, All);

namespace Dataflow
{
	void RegisterChaosFleshKinematicInitializationNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FKinematicInitializationDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FKinematicTetrahedralBindingsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSetVerticesKinematicDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBinVerticesDataflowNode);
	}
}

void FKinematicTetrahedralBindingsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);
		TManagedArray<FIntVector4>* Tetrahedron = InCollection.FindAttribute<FIntVector4>(FTetrahedralCollection::TetrahedronAttribute, FTetrahedralCollection::TetrahedralGroup);
		TManagedArray<FVector3f>* Vertex = InCollection.FindAttribute<FVector3f>("Vertex", "Vertices");

		TObjectPtr<USkeletalMesh> SkeletalMesh = GetValue<TObjectPtr<USkeletalMesh>>(Context, &SkeletalMeshIn);
		if (SkeletalMesh && Tetrahedron && Vertex)
		{
			//parse exclusion list to find bones to skip
			TArray<FString> StrArray;
			ExclusionList.ParseIntoArray(StrArray, *FString(" "));				

			int32 NumTets = Tetrahedron->Num();
			TArray<FTransform> ComponentPose;
			Dataflow::Animation::GlobalTransforms(SkeletalMesh->GetRefSkeleton(), ComponentPose);
			for (int32 b = 0; b < SkeletalMesh->GetRefSkeleton().GetNum(); b++)
			{
				bool Skip = false;
				FString BoneName = SkeletalMesh->GetRefSkeleton().GetBoneName(b).ToString();
				for (FString Elem : StrArray)
				{
					if (BoneName.Contains(Elem))
					{
						Skip = true;
						break;
					}
				}
				if (Skip)
					continue;

				FVector3f BonePosition(ComponentPose[b].GetTranslation());
				int32 ParentIndex=SkeletalMesh->GetRefSkeleton().GetParentIndex(b);
				
				if (ParentIndex != INDEX_NONE) 
				{
					FVector3f ParentPosition(ComponentPose[ParentIndex].GetTranslation());
					FVector3f RayDir = ParentPosition - BonePosition;
					Chaos::FReal Length = RayDir.Length();
					RayDir.Normalize();

					if (Length > Chaos::FReal(1e-8)) 
					{
						TSet<int32> BoneVertSet;
						for (int32 t = 0; t < NumTets; t++) 
						{
							int32 i = (*Tetrahedron)[t][0];
							int32 j = (*Tetrahedron)[t][1];
							int32 k = (*Tetrahedron)[t][2];
							int32 l = (*Tetrahedron)[t][3];

							TArray<Chaos::TVec3<Chaos::FRealSingle>> InVertices;
							InVertices.SetNum(4);
							InVertices[0][0] = (*Vertex)[i].X; InVertices[0][1] = (*Vertex)[i].Y; InVertices[0][2] = (*Vertex)[i].Z;
							InVertices[1][0] = (*Vertex)[j].X; InVertices[1][1] = (*Vertex)[j].Y; InVertices[1][2] = (*Vertex)[j].Z;
							InVertices[2][0] = (*Vertex)[k].X; InVertices[2][1] = (*Vertex)[k].Y; InVertices[2][2] = (*Vertex)[k].Z;
							InVertices[3][0] = (*Vertex)[l].X; InVertices[3][1] = (*Vertex)[l].Y; InVertices[3][2] = (*Vertex)[l].Z;
							Chaos::FConvex ConvexTet(InVertices, Chaos::FReal(0));
							Chaos::FReal OutTime;
							Chaos::FVec3 OutPosition, OutNormal;
							int32 OutFaceIndex;
							bool KeepTet = ConvexTet.Raycast(BonePosition, RayDir, Length, Chaos::FReal(0), OutTime, OutPosition, OutNormal, OutFaceIndex);
							if (KeepTet) 
							{
								BoneVertSet.Add(i); BoneVertSet.Add(j); BoneVertSet.Add(k); BoneVertSet.Add(l);
							}
						}

						TArray<int32> BoundVerts = BoneVertSet.Array();
						TArray<float> BoundWeights;
						BoundWeights.Init(float(1), BoundVerts.Num());
						
						//get local coords of bound verts
						typedef GeometryCollection::Facades::FKinematicBindingFacade FKinematics;
						FKinematics Kinematics(InCollection); Kinematics.DefineSchema();
						if (Kinematics.IsValid())
						{
							FKinematics::FBindingKey Binding = Kinematics.SetBoneBindings(b, BoundVerts, BoundWeights);
							TManagedArray<TArray<FVector3f>>& LocalPos = InCollection.AddAttribute<TArray<FVector3f>>("LocalPosition", Binding.GroupName);
							Kinematics.AddKinematicBinding(Binding);

							auto FloatVert = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };
							auto DoubleVert = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
							LocalPos[Binding.Index].SetNum(BoundVerts.Num());
							for (int32 i = 0; i < BoundVerts.Num(); i++)
							{
								FVector3f Temp = (*Vertex)[BoundVerts[i]];
								LocalPos[Binding.Index][i] = FloatVert(ComponentPose[b].InverseTransformPosition(DoubleVert(Temp)));
							}
						}
					}
				}
			}
			GeometryCollection::Facades::FVertexBoneWeightsFacade(InCollection).AddBoneWeightsFromKinematicBindings();
		}

		SetValue<DataType>(Context, InCollection, &Collection);
	}
}


void FKinematicInitializationDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);

		if (TObjectPtr<USkeletalMesh> SkeletalMesh = GetValue<TObjectPtr<USkeletalMesh>>(Context, &SkeletalMeshIn))
		{
			int32 Index = GetValue<int32>(Context, &BoneIndexIn);
			if (Index != INDEX_NONE)
			{
				if (TManagedArray<FVector3f>* Vertices = InCollection.FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup))
				{
					TArray<int32> BoundVerts;
					TArray<float> BoundWeights;
					if (FindInput(&VertexIndicesIn) && FindInput(&VertexIndicesIn)->GetConnection())
					{
						for (int32 SelectionIndex : GetValue<TArray<int32>>(Context, &VertexIndicesIn))
						{
							if (0 <= SelectionIndex && SelectionIndex < Vertices->Num())
							{
								BoundVerts.Add(SelectionIndex);
							}
						}
						BoundWeights.Init(1.0, BoundVerts.Num());
					}
					else
					{
						TArray<FTransform> ComponentPose;
						Dataflow::Animation::GlobalTransforms(SkeletalMesh->GetRefSkeleton(), ComponentPose);
						if (0 < Index && Index < ComponentPose.Num())
						{
							FVector3f BonePosition(ComponentPose[Index].GetTranslation());

							int NumVertices = Vertices->Num();
							for (int i = Vertices->Num() - 1; i > 0; i--)
							{
								if ((BonePosition - (*Vertices)[i]).Length() < Radius)
								{
									BoundVerts.Add(i);
									BoundWeights.Add(1.0);
								}
							}
						}
					}
					GeometryCollection::Facades::FKinematicBindingFacade Kinematics(InCollection);
					Kinematics.AddKinematicBinding(Kinematics.SetBoneBindings(Index, BoundVerts, BoundWeights));
				}
			}
		}
		SetValue<DataType>(Context, InCollection, &Collection);
	}
}

void FSetVerticesKinematicDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);
		TArray<int32> BoundVerts;
		TArray<float> BoundWeights;
		if (FindInput(&VertexIndicesIn) && FindInput(&VertexIndicesIn)->GetConnection())
		{
			if (TManagedArray<FVector3f>* Vertices = InCollection.FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup))
			{

				for (int32 SelectionIndex : GetValue<TArray<int32>>(Context, &VertexIndicesIn))
				{
					if (0 <= SelectionIndex && SelectionIndex < Vertices->Num())
					{
						BoundVerts.Add(SelectionIndex);
					}
				}
				BoundWeights.Init(1.0, BoundVerts.Num());	
			}
		} 
		else
		{
			if (TManagedArray<FVector3f>* Vertices = InCollection.FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup))
			{
				float MaxZ = -FLT_MAX;
				int MaxIndex = INDEX_NONE;
				for (int i = 0; i < Vertices->Num(); i++)
				{
					if ((*Vertices)[i].Z > MaxZ)
					{
						MaxZ = (*Vertices)[i].Z;
						MaxIndex = i;
					}
				}
				if (MaxIndex != INDEX_NONE)
				{
					BoundVerts.Add(MaxIndex);
					BoundWeights.Add(1.f);
				}
			}
			
		}
		if (BoundVerts.Num() > 0)
		{
			GeometryCollection::Facades::FKinematicBindingFacade Kinematics(InCollection);
			Kinematics.AddKinematicBinding(Kinematics.SetBoneBindings(INDEX_NONE, BoundVerts, BoundWeights));
		}
		SetValue<DataType>(Context, InCollection, &Collection);
	}
}


void FBinVerticesDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&VertexIndicesOut))
	{	
		TArray<int32> VertexArray;
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FString TempFileName = Filename;
		TempFileName = TempFileName.Replace(TEXT("\\"), TEXT("/"));
		if (TUniquePtr<FFleshCollection> FleshCollection = ChaosFlesh::ImportTetFromFile(TempFileName))
		{	
			TManagedArray<FVector3f>* VerticesBin = FleshCollection->FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
			TManagedArray<FVector3f>* VerticesCollection = InCollection.FindAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);
			if (VerticesBin && VerticesCollection)
			{
				for (int32 i = 0; i < VerticesBin->Num(); i++)
				{
					for (int32 j = 0; j < VerticesCollection->Num(); ++j)
					{
						if (((*VerticesBin)[i] - (*VerticesCollection)[j]).Length() < Tolerance)
						{
							VertexArray.Emplace(j);
							break;
						}
					}
					ensureMsgf(VertexArray.Num() == i + 1, TEXT("Some vertex is not binned or binned repeatedly at current tolerance"));
				}
			}
		}
		Out->SetValue<DataType>(VertexArray, Context);
	}
}

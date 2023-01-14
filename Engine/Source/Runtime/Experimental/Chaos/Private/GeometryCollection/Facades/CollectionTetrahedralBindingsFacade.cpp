// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionTetrahedralBindingsFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace GeometryCollection::Facades
{
	// Groups 
	const FName FTetrahedralBindings::MeshBindingsGroupName = "MeshBindings";

	// Attributes
	const FName FTetrahedralBindings::MeshIdAttributeName = "MeshId";
	//const FName FTetrahedralBindings::MeshLODAttributeName = "MeshLOD";

	const FName FTetrahedralBindings::ParentsAttributeName = "Parents";
	const FName FTetrahedralBindings::WeightsAttributeName = "Weights";
	const FName FTetrahedralBindings::OffsetsAttributeName = "Offsets";

	FTetrahedralBindings::FTetrahedralBindings(FManagedArrayCollection& InCollection)
		: MeshIdAttribute(InCollection, MeshIdAttributeName, MeshBindingsGroupName)
		, Parents(nullptr)
		, Weights(nullptr)
		, Offsets(nullptr)
	{}

	FTetrahedralBindings::FTetrahedralBindings(const FManagedArrayCollection& InCollection)
		: MeshIdAttribute(InCollection, MeshIdAttributeName, MeshBindingsGroupName)
		, Parents(nullptr)
		, Weights(nullptr)
		, Offsets(nullptr)
	{}

	FTetrahedralBindings::~FTetrahedralBindings()
	{
		delete Parents; Parents = nullptr;
		delete Weights; Weights = nullptr;
		delete Offsets; Offsets = nullptr;
	}

	void FTetrahedralBindings::DefineSchema(/*const FName* MeshId, const int32 LOD*/)
	{
		check(!IsConst());
		TManagedArray<FString>& MeshIdValues = 
			MeshIdAttribute.IsValid() ? MeshIdAttribute.Modify() : MeshIdAttribute.Add();
	/*	if (MeshId)
		{
			// Generate the group name, and if it doesn't exist, store it in the table of 
			// available bindings groups.
			FName GroupName = GenerateMeshGroupName(*MeshId, LOD);
			if (!MeshIdValues.Contains(GroupName.ToString()))
			{
				const int32 Idx = MeshIdAttribute.AddElements(1);
				MeshIdValues[Idx] = GroupName.ToString();

				// This is a new group, so create the bindings arrays.
				AddBindingsGroup(GroupName);
			}
			else
			{
				// This is an existing group, so find the existing bindings arrays.
				ReadBindingsGroup(GroupName);
			}

		}
		*/
	}

	bool FTetrahedralBindings::IsValid() const
	{
		return MeshIdAttribute.IsValid() && 
			(Parents && Parents->IsValid()) && 
			(Weights && Weights->IsValid()) && 
			(Offsets && Offsets->IsValid());
	}

	FName FTetrahedralBindings::GenerateMeshGroupName(
		const int32 TetMeshIdx,
		const FName& MeshId,
		const int32 LOD)
	{
		FString Str = FString::Printf(TEXT("TetrahedralBindings:TetMeshIdx:%d:%s:%d"), TetMeshIdx, *MeshId.ToString(), LOD);
		return FName(Str.Len(), *Str);
	}

	bool FTetrahedralBindings::ContainsBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD) const
	{
		return ContainsBindingsGroup(GenerateMeshGroupName(TetMeshIdx, MeshId, LOD));
	}

	bool FTetrahedralBindings::ContainsBindingsGroup(const FName& GroupName) const
	{
		check(MeshIdAttribute.IsValid());
		const TManagedArray<FString>* MeshIdValues =
			MeshIdAttribute.IsValid() ? MeshIdAttribute.Find() : nullptr;
		return MeshIdValues ? MeshIdValues->Contains(GroupName.ToString()) : false;
	}

	void FTetrahedralBindings::AddBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD)
	{
		AddBindingsGroup(GenerateMeshGroupName(TetMeshIdx, MeshId, LOD));
	}

	void FTetrahedralBindings::AddBindingsGroup(const FName& GroupName)
	{
		if (ContainsBindingsGroup(GroupName))
		{
			ReadBindingsGroup(GroupName);
			return;
		}

		check(!IsConst());
		const int32 Idx = MeshIdAttribute.AddElements(1);
		MeshIdAttribute.Modify()[Idx] = GroupName.ToString();

		delete Parents; Parents = nullptr;
		delete Weights; Weights = nullptr;
		delete Offsets; Offsets = nullptr;
		FManagedArrayCollection& Collection = *MeshIdAttribute.GetCollection();
		Parents = new TManagedArrayAccessor<FIntVector4>(Collection, ParentsAttributeName, GroupName);
		Weights = new TManagedArrayAccessor<FVector4f>(Collection, WeightsAttributeName, GroupName);
		Offsets = new TManagedArrayAccessor<FVector3f>(Collection, OffsetsAttributeName, GroupName);
		Parents->Add(ManageArrayAccessor::EPersistencePolicy::MakePersistent, FGeometryCollection::VerticesGroup);
		Weights->Add(ManageArrayAccessor::EPersistencePolicy::MakePersistent);
		Offsets->Add(ManageArrayAccessor::EPersistencePolicy::MakePersistent);
	}

	bool FTetrahedralBindings::ReadBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD)
	{
		return ReadBindingsGroup(GenerateMeshGroupName(TetMeshIdx, MeshId, LOD));
	}

	bool FTetrahedralBindings::ReadBindingsGroup(const FName& GroupName)
	{
		check(MeshIdAttribute.IsValid());
		delete Parents; Parents = nullptr;
		delete Weights; Weights = nullptr;
		delete Offsets; Offsets = nullptr;
		if (MeshIdAttribute.Find()->Contains(GroupName.ToString()))
		{
			return false;
		}
		// This is an existing group, so find the existing bindings arrays.
		FManagedArrayCollection& Collection = *MeshIdAttribute.GetCollection();
		Parents = new TManagedArrayAccessor<FIntVector4>(Collection, ParentsAttributeName, GroupName);
		Weights = new TManagedArrayAccessor<FVector4f>(Collection, WeightsAttributeName, GroupName);
		Offsets = new TManagedArrayAccessor<FVector3f>(Collection, OffsetsAttributeName, GroupName);
		check(Parents->IsValid());
		check(Weights->IsValid());
		check(Offsets->IsValid());
		return Parents->IsValid() && Weights->IsValid() && Offsets->IsValid();
	}

	void FTetrahedralBindings::RemoveBindingsGroup(const int32 TetMeshIdx, const FName& MeshId, const int32 LOD)
	{
		RemoveBindingsGroup(GenerateMeshGroupName(TetMeshIdx, MeshId, LOD));
	}

	void FTetrahedralBindings::RemoveBindingsGroup(const FName& GroupName)
	{
		check(!IsConst());
		TManagedArray<FString>& MeshIdValues = MeshIdAttribute.Modify();
		int32 Idx = MeshIdValues.Find(GroupName.ToString());
		if (Idx != INDEX_NONE)
		{
			TArray<int32> Indices;
			Indices.Add(Idx);
			MeshIdValues.RemoveElements(Indices);
		}

		FManagedArrayCollection& Collection = *MeshIdAttribute.GetCollection();
		if (Parents)
		{
			Parents->Remove();
		}
		if (Weights)
		{
			Weights->Remove();
		}
		if (Offsets)
		{
			Offsets->Remove();
		}
		// Only drop the group if it's empty at this point?
		if (Collection.NumAttributes(GroupName) == 0)
		{
			Collection.RemoveGroup(GroupName);
		}
	}

	void 
	FTetrahedralBindings::SetBindingsData(
		const TArray<FIntVector4>& ParentsIn,
		const TArray<FVector4f>& WeightsIn,
		const TArray<FVector3f>& OffsetsIn)
	{
		check(!IsConst());
		check(IsValid());
		check((ParentsIn.Num() == WeightsIn.Num()) && (ParentsIn.Num() == OffsetsIn.Num()));

		const int32 Num = ParentsIn.Num();
		const int32 CurrNum = Parents->Num();//Collection.NumElements(CurrGroupName);
		Parents->AddElements(Num - CurrNum); // Resizes the group
		TManagedArray<FIntVector4>& ParentsValues = Parents->Modify();
		TManagedArray<FVector4f>& WeightsValues = Weights->Modify();
		TManagedArray<FVector3f>& OffsetsValues = Offsets->Modify();
		for (int32 i = 0; i < Num; i++)
		{
			ParentsValues[i] = ParentsIn[i];
			WeightsValues[i] = WeightsIn[i];
			OffsetsValues[i] = OffsetsIn[i];
		}
	}


};



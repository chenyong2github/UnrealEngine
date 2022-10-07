// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/Facades/CollectionSelectionFacade.h"

namespace GeometryCollection::Facades
{

	/**
	* FVertexBoneWeightsFacade
	* 
	* Defines common API for storing a vertex weights bound to a bone. This mapping is from the 
	* the vertex to the bone index. The FSelectionFacad will store the mapping from the BoneIndex
	* to the vertex. 
	* Usage:
	*    FVertexBoneWeightsFacade::AddBoneWeights(this, FSelectionFacade(this) );
	* 
	* Then arrays can be accessed later by:
	*	const TManagedArray< TArray<int32> >* BoneIndices = FVertexBoneWeightsFacade::GetBoneIndices(this);
	*	const TManagedArray< TArray<float> >* BoneWeights = FVertexBoneWeightsFacade::GetBoneWeights(this);
	*
	* The following attributes are created on the collection:
	* 
	*	- FindAttribute<TArray<int32>>(FVertexSetInterface::IndexAttribute, FGeometryCollection::VerticesGroup);
	*	- FindAttribute<TArray<float>>(FVertexSetInterface::WeightAttribute, FGeometryCollection::VerticesGroup);
	* 
	*/
	class CHAOS_API FVertexBoneWeightsFacade
	{
		FManagedArrayCollection* Self;

	public:

		// Attributes
		static const FName IndexAttribute;
		static const FName WeightAttribute;

		/**
		* FVertexBoneWeightsFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on.
		*/
		FVertexBoneWeightsFacade(FManagedArrayCollection* InSelf);

		/**
		*  Create the facade.
		*/
		static void DefineSchema(FManagedArrayCollection* Collection);
		void DefineSchema() { return DefineSchema(Self); }


		/**
		*  Is the Facade defined on the collection?
		*/
		static bool HasFacade(const FManagedArrayCollection* Collection);
		bool HasFacade() { return HasFacade(Self); }

		/**
		* Using a FSelectionFacade::FSelectionKey, create indexes from the vertices to the driving bones. 
		* @param FManagedArrayCollection : Collection
		* @param FSelectionFacade::FSelectionKey : Key for weights in the FSelectionFacade
		*/
		static void AddBoneWeightsFromKinematicBindings(FManagedArrayCollection* Collection);
		void AddBoneWeightsFromKinematicBindings() { AddBoneWeightsFromKinematicBindings(Self); }

		/**
		* Return the vertex bone indices from the collection. Null if not initialized. 
		* @param FManagedArrayCollection : Collection
		*/
		static const TManagedArray< TArray<int32> >* GetBoneIndices(const FManagedArrayCollection* Collection);
		const TManagedArray< TArray<int32> >* GetBoneIndices() const { return GetBoneIndices(Self); }


		/**
		* Return the vertex bone weights from the collection. Null if not initialized.
		* @param FManagedArrayCollection : Collection
		*/
		static const TManagedArray< TArray<float> >* GetBoneWeights(const FManagedArrayCollection* Collection);
		const TManagedArray< TArray<float> >* GetBoneWeights() const { return GetBoneWeights(Self); }


	};

}
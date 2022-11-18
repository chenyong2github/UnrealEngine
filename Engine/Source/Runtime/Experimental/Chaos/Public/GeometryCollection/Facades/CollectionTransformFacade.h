// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayAccessor.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace GeometryCollection::Facades
{
	/**
	 * Provides an API to read and manipulate hierarchy in a managed array collection
	 */
	class CHAOS_API FTransformFacade
	{
		FManagedArrayCollection& Self;

	public:

		// Group
		static const FName TransformGroup;

		// Attributes
		static const FName ParentAttribute;
		static const FName ChildrenAttribute;
		static const FName TransformAttribute;


		FTransformFacade(FManagedArrayCollection& InCollection);
		FTransformFacade(const FManagedArrayCollection& InCollection);

		/** Create the facade attributes. */
		void DefineSchema() {}

		/** Valid if parent and children arrays are available */
		bool IsValid() const;

		/** Is the facade defined constant. */
		bool IsConst() const { return Parent.IsConst(); }

		/** Get the root index */
		TSet<int32> GetRootIndices() const;

		/**
		* Return the parent indices from the collection. Null if not initialized.
		*/
		const TManagedArray< int32 >* GetParents() const { return Parent.Find(); }

		/**
		* Return the child indicesfrom the collection. Null if not initialized.
		*/
		const TManagedArray< TSet<int32> >* FindChildren() const { return Children.Find(); }

		/**
		* Return the child indicesfrom the collection. Null if not initialized.
		*/
		const TManagedArray< FTransform >* FindTransforms() const { return Transform.Find(); }

	private:
		TManagedArrayAccessor<int32>		Parent;
		TManagedArrayAccessor<TSet<int32>>	Children;
		TManagedArrayAccessor<FTransform>	Transform;
	};
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Containers/Array.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace GeometryCollection::Facades
{

	/**
	* FTransformSource
	* 
	* Defines common API for storing owners of a transform hierarchy. For example, to store
	* the component that generated a transform hierirchy, use the function :
	*    Key = this->AddTransformSource(<Skeleton Asset Name>, <Skeleton Asset Guid>, {<Root transform indices in collection>});
	* 
	*    Key = this->AddTransformSource("", GUID, {1,5,7});
	*
	* The source root indices can be queries later using the name and guid:
	*	TSet<int32> SourceRoots = this->GetSourceTransformRoots("", GUID);
	*
	* The following groups are created on the collection based on which API is called. 
	* 
	*	<Group> = FTransformSource::TransformSourceGroup
	*	- FindAttribute<FString>(FTransformSource::SourceNameAttribute, <Group>);
	*	- FindAttribute<Guid>(FTransformSource::SourceGuidAttribute, <Group>);
	*	- FindAttribute<TSet<int32>>(FTransformSource::SourceRootsAttribute, <Group>);
	* 
	*/
	class CHAOS_API FTransformSource
 	{
		FManagedArrayCollection* Self;

	public:

		// groups
		static const FName TransformSourceGroup;

		// Attributes
		static const FName SourceNameAttribute;
		static const FName SourceGuidAttribute;
		static const FName SourceRootsAttribute;

		/**
		* FSelectionFacade Constuctor
		* @param VertixDependencyGroup : GroupName the index attribute is dependent on. 
		*/
		FTransformSource(FManagedArrayCollection* InSelf);

		/**
		*  Create the facade.
		*/
		static void DefineSchema(FManagedArrayCollection* Self);

		/**
		*  Is the Facade defined on the collection?
		*/
		static bool HasFacade(const FManagedArrayCollection* Collection);

		/**
		* Add a transform root mapping.
		* @param Name : Name of the owner of the transform set.
		* @param Guid : Guid of the owner of the transform set.
		* @param Roots : Root indices of the transform set.
		*/
		static void AddTransformSource(FManagedArrayCollection* Collection, const FString& Name, const FString& Guid, const TSet<int32>& Roots);

		/**
		* Query for root indices.  
		* @param Name : Name of the owner of the transform set.  
		* @param Guid : Guid of the owner of the transform set.
		*/
		static TSet<int32> GetTransformSource(const FManagedArrayCollection* Collection, const FString& Name, const FString& Guid);


	};

}
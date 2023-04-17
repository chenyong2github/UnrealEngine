// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace GeometryCollection::Facades
{

	/**
	 * Provides an API for connection-graph related attributes
	 */
	class CHAOS_API FCollectionConnectionGraphFacade
	{
	public:
		FCollectionConnectionGraphFacade(FManagedArrayCollection& InCollection);
		FCollectionConnectionGraphFacade(const FManagedArrayCollection& InCollection);

		/** Does the collection support the facade. */
		bool IsValid() const;

		/** Is the facade defined constant. */
		bool IsConst() const
		{
			return ConnectionEdgeStartAttribute.IsConst();
		}

		/** Create the facade attributes. */
		void DefineSchema();

		/** Remove the attributes */
		void ClearAttributes();

		/** Connect two bones */
		void ConnectWithContact(int32 BoneA, int32 BoneB, float ContactArea);

		/** Connect two bones */
		void Connect(int32 BoneA, int32 BoneB);

		/** Enable or disable the Contact Area attribute */
		void EnableContactAreas(bool bEnable, float DefaultContactArea = 1.0f);

		/** Reserve space for a number of additional connections */
		void ReserveAdditionalConnections(int32 NumAdditionalConnections);

		/**  Connections between bones that have the same parent in the hierarchy */
		UE_DEPRECATED(5.3, "We have switched to an edge array connection representation. Please use the accessor functions (GetConnection, NumConnections, etc) to access the arrays of edge data.")
		TManagedArrayAccessor<TSet<int32>> ConnectionsAttribute;

		// Get the transform indices for the ConnectionIndex
		TPair<int32, int32> GetConnection(int32 ConnectionIndex) const;
		// Get the contact area for the ConnectionIndex
		float GetConnectionContactArea(int32 ConnectionIndex) const;

		bool HasContactAreas() const;

		// Number of connection edges
		int32 NumConnections() const;

		// Verifies the connections indices are valid indices into the Collection's Transform group
		bool HasValidConnections() const;

		// Remove all edge connections, but keep the connection attributes
		void ResetConnections();

	protected:
		 TManagedArrayAccessor<int32> ConnectionEdgeStartAttribute;
		 TManagedArrayAccessor<int32> ConnectionEdgeEndAttribute;
		 TManagedArrayAccessor<float> ConnectionEdgeContactAttribute;

#if UE_BUILD_DEBUG
		/** Optional Parent array for validating connections in debug */
		TManagedArrayAccessor<int32> ParentAttribute;
#endif 
	};

}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/PrimitiveComponent.h"
#include "IndexTypes.h"
#include "InteractiveToolStorableSelection.h"

#include "GroupTopologyStorableSelection.generated.h"

struct FCompactMaps;
struct FGroupTopologySelection;
class FGroupTopology;
class UPrimitiveComponent;
class UInteractiveTool;

/**
 * Class used to represent a group topology selection independently of a FGroupTopology. Relies
 * on the vertex id's of the mesh to stay the same for the selection to be properly loadable
 * in a new group topology object.
 *
 * If we someday get FGroupTopology persistence across tools, this class is likely to devolve to
 * a copy of FGroupTopologySelection, bundled with some identifying info and inheriting from the
 * base Uclass.
 */
UCLASS()
class MODELINGCOMPONENTS_API UGroupTopologyStorableSelection : public UInteractiveToolStorableSelection
{
	GENERATED_BODY()

public:
	enum class ETopologyType
	{
		FGroupTopology,
		FTriangleGroupTopology,
		FUVGroupTopology,
	};

	/**
	 * Used by tools to figure out whether the stored selection is applicable to their target.
	 *
	 * Note that this info is likely to change as we start to support dynamic mesh (and even
	 * FGroupTopology) persistence, since we will not have to use the original primitive component
	 * as an identifier.
	 */
	struct FIdentifyingInfo
	{
		UPrimitiveComponent* ComponentTarget = nullptr;
		ETopologyType TopologyType = ETopologyType::FGroupTopology;
	};
	FIdentifyingInfo IdentifyingInfo;


	UGroupTopologyStorableSelection() {};

	void SetSelection(const FGroupTopology& TopologyIn, const FGroupTopologySelection& SelectionIn);

	/**
	 * Resets the contents of the object using the given selection.
	 *
	 * @param CompactMaps If the mesh was compacted without updating the passed in topology object, these
	 *  maps will be used to give an object that will work with the new mesh vids.
	 */
	void SetSelection(const FGroupTopology& TopologyIn, const FGroupTopologySelection& SelectionIn, const FCompactMaps& CompactMap);

	/**
	 * Initializes a FGroupTopologySelection using the current contents of the object. The topology
	 * must already be initialized.
	 */
	void ExtractIntoSelectionObject(const FGroupTopology& TopologyIn, FGroupTopologySelection& SelectionOut) const;

	bool IsEmpty() const
	{
		return CornerVids.IsEmpty() && GroupEdgeRepresentativeVerts.IsEmpty() && GroupIDs.IsEmpty();
	}

	bool operator==(const UGroupTopologyStorableSelection& Other) const
	{
		return IdentifyingInfo.ComponentTarget == Other.IdentifyingInfo.ComponentTarget
			&& IdentifyingInfo.TopologyType == Other.IdentifyingInfo.TopologyType
			&& CornerVids == Other.CornerVids
			&& GroupEdgeRepresentativeVerts == Other.GroupEdgeRepresentativeVerts
			&& GroupIDs == Other.GroupIDs;
	}


	/**
	 * Returns a pair of vertex ID's that are representative of a group edge, to be able to identify
	 * a selected group edge independently of a group topology object. 
	 *
	 * For non-loop group edges, this will be the vids of the lower-vid endpoint and its neighbor 
	 * in the group edge, arranged in increasing vid order. For loop group edges, this will be the 
	 * lowest vid in the group edge and its lower-vid neighbor in the group edge.
	 *
	 * This is basically just identifying the group edge with a specific component edge, but using
	 * vids instead of an edge id makes it a bit easier to apply a compact mapping if the mesh was
	 * compacted, and helps the representative survive translation to/from a mesh description in
	 * cases of compaction.
	 *
	 * @param CompactMaps This gets used to remap the vids given in the topology first (i.e., it assumes 
	 *  that the CompactMaps have not yet been applied to the contents of the topology object).
	 */
	static FIndex2i  GetGroupEdgeRepresentativeVerts(const FGroupTopology& TopologyIn, int GroupEdgeID, const FCompactMaps& CompactMaps);

	/**
	 * Returns a pair of vertex ID's that are representative of a group edge, to be able to identify
	 * a selected group edge independently of a group topology object. 
	 *
	 * See other overload for more details.
	 */
	static FIndex2i GetGroupEdgeRepresentativeVerts(const FGroupTopology& TopologyIn, int GroupEdgeID);

protected:
	TArray<int32> CornerVids;
	TArray<FIndex2i> GroupEdgeRepresentativeVerts;
	TArray<int32> GroupIDs;
};

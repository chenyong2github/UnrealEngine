// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithMaxExporterDefines.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "max.h"
MAX_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"


class FDatasmithMaxStaticMeshAttributes;
class FDatasmithMesh;

namespace DatasmithMaxDirectLink
{

class ISceneTracker;
class FNodeTracker;

namespace GeomUtils
{

class FRenderMeshForConversion: FNoncopyable
{
public:
	explicit FRenderMeshForConversion(){}
	explicit FRenderMeshForConversion(INode* InNode, Mesh* InMaxMesh, bool bInNeedsDelete, FTransform InPivot=FTransform::Identity)
		: Node(InNode)
		, MaxMesh(InMaxMesh)
		, bNeedsDelete(bInNeedsDelete)
		, Pivot(InPivot)
	{}
	~FRenderMeshForConversion()
	{
		if (bNeedsDelete)
		{
			MaxMesh->DeleteThis();
		}
	}

	bool IsValid() const
	{
		return MaxMesh != nullptr;
	}

	INode* GetNode() const
	{
		return Node;
	}

	Mesh* GetMesh() const
	{
		return MaxMesh;
	}

	const FTransform& GetPivot() const
	{
		return Pivot;
	}


private:
	INode* Node = nullptr;
	Mesh* MaxMesh = nullptr;
	bool bNeedsDelete = false;
	FTransform Pivot;
};

bool ConvertRailClone(DatasmithMaxDirectLink::ISceneTracker& SceneTracker, DatasmithMaxDirectLink::FNodeTracker& NodeTracker);
bool ConvertForest(DatasmithMaxDirectLink::ISceneTracker& Scene, DatasmithMaxDirectLink::FNodeTracker& NodeTracker);

Mesh* GetMeshFromRenderMesh(TimeValue CurrentTime, INode* Node, BOOL& bNeedsDelete);
FRenderMeshForConversion GetMeshForGeomObject(TimeValue CurrentTime, INode* Node, Object* Obj); // Extract mesh using already evaluated object
FRenderMeshForConversion GetMeshForNode(TimeValue CurrentTime, INode* Node, FTransform Pivot); // Extract mesh evaluating node object

FRenderMeshForConversion GetMeshForCollision(TimeValue CurrentTime, DatasmithMaxDirectLink::ISceneTracker& SceneTracker, INode* Node);
INode* GetCollisionNode(DatasmithMaxDirectLink::ISceneTracker& SceneTracker, INode* OriginalNode, const FDatasmithMaxStaticMeshAttributes* DatasmithAttributes, bool& bOutFromDatasmithAttribute);

void FillDatasmithMeshFromMaxMesh(TimeValue CurrentTime, FDatasmithMesh& DatasmithMesh, Mesh& MaxMesh, INode* ExportedNode, bool bForceSingleMat, TSet<uint16>& SupportedChannels, TMap<int32, int32>& UVChannelsMap, FTransform Pivot);

}
}

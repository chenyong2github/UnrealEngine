// Copyright Epic Games, Inc. All Rights Reserved.

#include "3DElement2String.h"

DISABLE_SDK_WARNINGS_START

#include "ModelElement.hpp"
#include "ModelMeshBody.hpp"
#include "ConvexPolygon.hpp"

#include "Transformation.hpp"

DISABLE_SDK_WARNINGS_END

BEGIN_NAMESPACE_UE_AC

static void AddIfNotZero(utf8_string* IOString, const char* InFmt, int32 InCount)
{
	if (InCount != 0)
	{
		*IOString += Utf8StringFormat(InFmt, InCount);
	}
}

static void AddIfTrue(utf8_string* IOString, const char* InFmt, bool InValue)
{
	if (InValue)
	{
		*IOString += Utf8StringFormat(InFmt, "true");
	}
}

utf8_string F3DElement2String::Element2String(const ModelerAPI::Element& InModelElement)
{
	utf8_string InfoStr;

	if (!InModelElement.IsInvalid())
	{
		InfoStr += Utf8StringFormat("GetType=%d\n", InModelElement.GetType());
		InfoStr += Utf8StringFormat("Guid=%s\n", InModelElement.GetElemGuid().ToUniString().ToUtf8());
		InfoStr += Utf8StringFormat("GenId=%u\n", InModelElement.GetGenId());
		AddIfNotZero(&InfoStr, "TessellatedBodyCount=%d\n", InModelElement.GetTessellatedBodyCount());
		AddIfNotZero(&InfoStr, "MeshBodyCount=%d\n", InModelElement.GetMeshBodyCount());
		AddIfNotZero(&InfoStr, "NurbsBodyCount=%d\n", InModelElement.GetNurbsBodyCount());
		AddIfNotZero(&InfoStr, "PointCloud=%d\n", InModelElement.GetPointCloudCount());
		AddIfNotZero(&InfoStr, "LightCount=%d\n", InModelElement.GetLightCount());
		Box3D Box = InModelElement.GetBounds();
#if AC_VERSION < 24
		InfoStr += Utf8StringFormat("Box={{%lf, %lf, %lf}, {%lf, %lf, %lf}}\n", Box.xMin, Box.yMin,
									Box.zMin, Box.xMax, Box.yMax, Box.zMax);
#else
		InfoStr += Utf8StringFormat("Box={{%lf, %lf, %lf}, {%lf, %lf, %lf}}\n", Box.GetMinX(), Box.GetMinY(),
			Box.GetMinZ(), Box.GetMaxX(), Box.GetMaxY(), Box.GetMaxZ());
#endif
		// GetBaseElemId
		ModelerAPI::Transformation Transform = InModelElement.GetElemLocalToWorldTransformation();
		InfoStr += Utf8StringFormat("LocalToWorldTransformation\n\tStatus=%d\n", Transform.status);
		for (size_t IndexRow = 0; IndexRow < 3; ++IndexRow)
		{
			InfoStr += Utf8StringFormat("\t{%lf, %lf, %lf, %lf}\n", Transform.matrix[IndexRow][0],
										Transform.matrix[IndexRow][1], Transform.matrix[IndexRow][2],
										Transform.matrix[IndexRow][3]);
		}
		GS::Int32 BodyCount = InModelElement.GetMeshBodyCount();
		for (Int32 IndexBody = 1; IndexBody <= BodyCount; ++IndexBody)
		{
			ModelerAPI::MeshBody Body;
			InModelElement.GetMeshBody(IndexBody, &Body);
			InfoStr += Body2String(Body);
		}
	}
	else
	{
		InfoStr += "Element is invalid";
	}

	return InfoStr;
}

utf8_string F3DElement2String::Body2String(const ModelerAPI::MeshBody& InBodyElement)
{
	utf8_string InfoStr;

	AddIfTrue(&InfoStr, "\t\tWireBody=%s\n", InBodyElement.IsWireBody());
	AddIfTrue(&InfoStr, "\t\tIsSurfaceBody=%s\n", InBodyElement.IsSurfaceBody());
	AddIfTrue(&InfoStr, "\t\tIsSolidBody=%s\n", InBodyElement.IsSolidBody());
	AddIfTrue(&InfoStr, "\t\tIsClosed=%s\n", InBodyElement.IsClosed());
	AddIfTrue(&InfoStr, "\t\tIsVisibleIfContour=%s\n", InBodyElement.IsVisibleIfContour());
	AddIfTrue(&InfoStr, "\t\tHasSharpEdge=%s\n", InBodyElement.HasSharpEdge());
	AddIfTrue(&InfoStr, "\t\tAlwaysCastsShadow=%s\n", InBodyElement.AlwaysCastsShadow());
	AddIfTrue(&InfoStr, "\t\tNeverCastsShadow=%s\n", InBodyElement.NeverCastsShadow());
	AddIfTrue(&InfoStr, "\t\tDoesNotReceiveShadow=%s\n", InBodyElement.DoesNotReceiveShadow());
	AddIfTrue(&InfoStr, "\t\tHasColor=%s\n", InBodyElement.HasColor());

	AddIfNotZero(&InfoStr, "\t\tVertexCount=%d\n", InBodyElement.GetVertexCount());
	AddIfNotZero(&InfoStr, "\t\tEdgeCount=%d\n", InBodyElement.GetEdgeCount());
	AddIfNotZero(&InfoStr, "\t\tPolygonCount=%d\n", InBodyElement.GetPolygonCount());
	AddIfNotZero(&InfoStr, "\t\tPolygonVectorCount=%d\n", InBodyElement.GetPolygonVectorCount());

	return InfoStr;
}

END_NAMESPACE_UE_AC

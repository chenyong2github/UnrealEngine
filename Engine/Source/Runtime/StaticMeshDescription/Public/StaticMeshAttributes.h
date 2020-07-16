// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshAttributes.h"


namespace MeshAttribute
{
	namespace Vertex
	{
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName CornerSharpness;	// deprecated
	}

	namespace VertexInstance
	{
		extern STATICMESHDESCRIPTION_API const FName TextureCoordinate;
		extern STATICMESHDESCRIPTION_API const FName Normal;
		extern STATICMESHDESCRIPTION_API const FName Tangent;
		extern STATICMESHDESCRIPTION_API const FName BinormalSign;
		extern STATICMESHDESCRIPTION_API const FName Color;
	}

	namespace Edge
	{
		extern STATICMESHDESCRIPTION_API const FName IsHard;
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName IsUVSeam;			// deprecated
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName CreaseSharpness;	// deprecated
	}

	namespace Triangle
	{
		extern STATICMESHDESCRIPTION_API const FName Normal;
		extern STATICMESHDESCRIPTION_API const FName Tangent;
		extern STATICMESHDESCRIPTION_API const FName Binormal;
	}

	namespace Polygon
	{
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName Normal;
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName Tangent;
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName Binormal;
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName Center;
	}

	namespace PolygonGroup
	{
		extern STATICMESHDESCRIPTION_API const FName ImportedMaterialSlotName;
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName EnableCollision;	// deprecated
		UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
		extern STATICMESHDESCRIPTION_API const FName CastShadow;		// deprecated
	}
}


PRAGMA_DISABLE_DEPRECATION_WARNINGS

class STATICMESHDESCRIPTION_API FStaticMeshAttributes : public FMeshAttributes
{
public:

	explicit FStaticMeshAttributes(FMeshDescription& InMeshDescription)
		: FMeshAttributes(InMeshDescription)
	{}

	virtual void Register() override;

	UE_DEPRECATED(4.26, "Please use RegisterTriangleNormalAndTangentAttributes() instead.")
	void RegisterPolygonNormalAndTangentAttributes();

	void RegisterTriangleNormalAndTangentAttributes();

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TVertexAttributesRef<float> GetVertexCornerSharpnesses() { return MeshDescription.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TVertexAttributesConstRef<float> GetVertexCornerSharpnesses() const { return MeshDescription.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness); }

	TVertexInstanceAttributesRef<FVector2D> GetVertexInstanceUVs() { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }
	TVertexInstanceAttributesConstRef<FVector2D> GetVertexInstanceUVs() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceNormals() { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceNormals() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceTangents() { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceTangents() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }

	TVertexInstanceAttributesRef<float> GetVertexInstanceBinormalSigns() { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }
	TVertexInstanceAttributesConstRef<float> GetVertexInstanceBinormalSigns() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }

	TVertexInstanceAttributesRef<FVector4> GetVertexInstanceColors() { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }
	TVertexInstanceAttributesConstRef<FVector4> GetVertexInstanceColors() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }

	TEdgeAttributesRef<bool> GetEdgeHardnesses() { return MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }
	TEdgeAttributesConstRef<bool> GetEdgeHardnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TEdgeAttributesRef<float> GetEdgeCreaseSharpnesses() { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }

	TTriangleAttributesRef<FVector> GetTriangleNormals() { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Normal); }
	TTriangleAttributesConstRef<FVector> GetTriangleNormals() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Normal); }

	TTriangleAttributesRef<FVector> GetTriangleTangents() { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Tangent); }
	TTriangleAttributesConstRef<FVector> GetTriangleTangents() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Tangent); }

	TTriangleAttributesRef<FVector> GetTriangleBinormals() { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Binormal); }
	TTriangleAttributesConstRef<FVector> GetTriangleBinormals() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Binormal); }

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesRef<FVector> GetPolygonNormals() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonNormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesRef<FVector> GetPolygonTangents() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonTangents() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesRef<FVector> GetPolygonBinormals() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonBinormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesRef<FVector> GetPolygonCenters() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonCenters() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center); }

	TPolygonGroupAttributesRef<FName> GetPolygonGroupMaterialSlotNames() { return MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
	TPolygonGroupAttributesConstRef<FName> GetPolygonGroupMaterialSlotNames() const { return MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
};


class FStaticMeshConstAttributes : public FMeshConstAttributes
{
public:

	explicit FStaticMeshConstAttributes(const FMeshDescription& InMeshDescription)
		: FMeshConstAttributes(InMeshDescription)
	{}

	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TVertexAttributesConstRef<float> GetVertexCornerSharpnesses() const { return MeshDescription.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness); }
	TVertexInstanceAttributesConstRef<FVector2D> GetVertexInstanceUVs() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceNormals() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceTangents() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }
	TVertexInstanceAttributesConstRef<float> GetVertexInstanceBinormalSigns() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }
	TVertexInstanceAttributesConstRef<FVector4> GetVertexInstanceColors() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }
	TEdgeAttributesConstRef<bool> GetEdgeHardnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }
	TTriangleAttributesConstRef<FVector> GetTriangleNormals() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Normal); }
	TTriangleAttributesConstRef<FVector> GetTriangleTangents() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Tangent); }
	TTriangleAttributesConstRef<FVector> GetTriangleBinormals() const { return MeshDescription.TriangleAttributes().GetAttributesRef<FVector>(MeshAttribute::Triangle::Binormal); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonNormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonTangents() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonBinormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }
	UE_DEPRECATED(4.26, "This attribute is no longer supported, please remove code pertaining to it.")
	TPolygonAttributesConstRef<FVector> GetPolygonCenters() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center); }
	TPolygonGroupAttributesConstRef<FName> GetPolygonGroupMaterialSlotNames() const { return MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

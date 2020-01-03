// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshAttributes.h"


namespace MeshAttribute
{
	namespace Vertex
	{
		extern STATICMESHDESCRIPTION_API const FName CornerSharpness;
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
		extern STATICMESHDESCRIPTION_API const FName IsUVSeam;
		extern STATICMESHDESCRIPTION_API const FName CreaseSharpness;
	}

	namespace Polygon
	{
		extern STATICMESHDESCRIPTION_API const FName Normal;
		extern STATICMESHDESCRIPTION_API const FName Tangent;
		extern STATICMESHDESCRIPTION_API const FName Binormal;
		extern STATICMESHDESCRIPTION_API const FName Center;
	}

	namespace PolygonGroup
	{
		extern STATICMESHDESCRIPTION_API const FName ImportedMaterialSlotName;
		extern STATICMESHDESCRIPTION_API const FName EnableCollision;
		extern STATICMESHDESCRIPTION_API const FName CastShadow;
	}
}


class STATICMESHDESCRIPTION_API FStaticMeshAttributes : public FMeshAttributes
{
public:

	explicit FStaticMeshAttributes(FMeshDescription& InMeshDescription)
		: FMeshAttributes(InMeshDescription)
	{}

	virtual void Register() override;

	void RegisterPolygonNormalAndTangentAttributes();

	TVertexAttributesRef<float> GetVertexCornerSharpnesses() { return MeshDescription.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness); }
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

	TEdgeAttributesRef<float> GetEdgeCreaseSharpnesses() { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }
	TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }

	TPolygonAttributesRef<FVector> GetPolygonNormals() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }
	TPolygonAttributesConstRef<FVector> GetPolygonNormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }

	TPolygonAttributesRef<FVector> GetPolygonTangents() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }
	TPolygonAttributesConstRef<FVector> GetPolygonTangents() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }

	TPolygonAttributesRef<FVector> GetPolygonBinormals() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }
	TPolygonAttributesConstRef<FVector> GetPolygonBinormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }

	TPolygonAttributesRef<FVector> GetPolygonCenters() { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center); }
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

	TVertexAttributesConstRef<float> GetVertexCornerSharpnesses() const { return MeshDescription.VertexAttributes().GetAttributesRef<float>(MeshAttribute::Vertex::CornerSharpness); }
	TVertexInstanceAttributesConstRef<FVector2D> GetVertexInstanceUVs() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceNormals() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceTangents() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent); }
	TVertexInstanceAttributesConstRef<float> GetVertexInstanceBinormalSigns() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign); }
	TVertexInstanceAttributesConstRef<FVector4> GetVertexInstanceColors() const { return MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color); }
	TEdgeAttributesConstRef<bool> GetEdgeHardnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard); }
	TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return MeshDescription.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness); }
	TPolygonAttributesConstRef<FVector> GetPolygonNormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal); }
	TPolygonAttributesConstRef<FVector> GetPolygonTangents() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent); }
	TPolygonAttributesConstRef<FVector> GetPolygonBinormals() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal); }
	TPolygonAttributesConstRef<FVector> GetPolygonCenters() const { return MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center); }
	TPolygonGroupAttributesConstRef<FName> GetPolygonGroupMaterialSlotNames() const { return MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); }
};

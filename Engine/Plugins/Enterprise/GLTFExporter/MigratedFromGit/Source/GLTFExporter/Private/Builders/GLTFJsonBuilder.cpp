// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFJsonBuilder.h"
#include "Builders/GLTFBuilderUtility.h"

FGLTFJsonBuilder::FGLTFJsonBuilder()
	: DefaultScene(JsonRoot.DefaultScene)
{
}

void FGLTFJsonBuilder::AddExtension(EGLTFJsonExtension Extension, bool bIsRequired)
{
	JsonRoot.Extensions.Used.Add(Extension);
	if (bIsRequired)
	{
		JsonRoot.Extensions.Required.Add(Extension);
	}
}

FGLTFJsonAccessorIndex FGLTFJsonBuilder::AddAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return FGLTFJsonAccessorIndex(JsonRoot.Accessors.Add(JsonAccessor));
}

FGLTFJsonAnimationIndex FGLTFJsonBuilder::AddAnimation(const FGLTFJsonAnimation& JsonAnimation)
{
	return FGLTFJsonAnimationIndex(JsonRoot.Animations.Add(JsonAnimation));
}

FGLTFJsonBufferIndex FGLTFJsonBuilder::AddBuffer(const FGLTFJsonBuffer& JsonBuffer)
{
	return FGLTFJsonBufferIndex(JsonRoot.Buffers.Add(JsonBuffer));
}

FGLTFJsonBufferViewIndex FGLTFJsonBuilder::AddBufferView(const FGLTFJsonBufferView& JsonBufferView)
{
	return FGLTFJsonBufferViewIndex(JsonRoot.BufferViews.Add(JsonBufferView));
}

FGLTFJsonCameraIndex FGLTFJsonBuilder::AddCamera(const FGLTFJsonCamera& JsonCamera)
{
	return FGLTFJsonCameraIndex(JsonRoot.Cameras.Add(JsonCamera));
}

FGLTFJsonImageIndex FGLTFJsonBuilder::AddImage(const FGLTFJsonImage& JsonImage)
{
	return FGLTFJsonImageIndex(JsonRoot.Images.Add(JsonImage));
}

FGLTFJsonMaterialIndex FGLTFJsonBuilder::AddMaterial(const FGLTFJsonMaterial& JsonMaterial)
{
	return FGLTFJsonMaterialIndex(JsonRoot.Materials.Add(JsonMaterial));
}

FGLTFJsonMeshIndex FGLTFJsonBuilder::AddMesh(const FGLTFJsonMesh& JsonMesh)
{
	return FGLTFJsonMeshIndex(JsonRoot.Meshes.Add(JsonMesh));
}

FGLTFJsonNodeIndex FGLTFJsonBuilder::AddNode(const FGLTFJsonNode& JsonNode)
{
	return FGLTFJsonNodeIndex(JsonRoot.Nodes.Add(JsonNode));
}

FGLTFJsonSamplerIndex FGLTFJsonBuilder::AddSampler(const FGLTFJsonSampler& JsonSampler)
{
	return FGLTFJsonSamplerIndex(JsonRoot.Samplers.Add(JsonSampler));
}

FGLTFJsonSceneIndex FGLTFJsonBuilder::AddScene(const FGLTFJsonScene& JsonScene)
{
	return FGLTFJsonSceneIndex(JsonRoot.Scenes.Add(JsonScene));
}

FGLTFJsonSkinIndex FGLTFJsonBuilder::AddSkin(const FGLTFJsonSkin& JsonSkin)
{
	return FGLTFJsonSkinIndex(JsonRoot.Skins.Add(JsonSkin));
}

FGLTFJsonTextureIndex FGLTFJsonBuilder::AddTexture(const FGLTFJsonTexture& JsonTexture)
{
	return FGLTFJsonTextureIndex(JsonRoot.Textures.Add(JsonTexture));
}

FGLTFJsonBackdropIndex FGLTFJsonBuilder::AddBackdrop(const FGLTFJsonBackdrop& JsonBackdrop)
{
	return FGLTFJsonBackdropIndex(JsonRoot.Backdrops.Add(JsonBackdrop));
}

FGLTFJsonVariationIndex FGLTFJsonBuilder::AddVariation(const FGLTFJsonVariation& JsonVariation)
{
	return FGLTFJsonVariationIndex(JsonRoot.Variations.Add(JsonVariation));
}

FGLTFJsonLightMapIndex FGLTFJsonBuilder::AddLightMap(const FGLTFJsonLightMap& JsonLightMap)
{
	return FGLTFJsonLightMapIndex(JsonRoot.LightMaps.Add(JsonLightMap));
}

FGLTFJsonLightIndex FGLTFJsonBuilder::AddLight(const FGLTFJsonLight& JsonLight)
{
	return FGLTFJsonLightIndex(JsonRoot.Lights.Add(JsonLight));
}

FGLTFJsonHotspotIndex FGLTFJsonBuilder::AddHotspot(const FGLTFJsonHotspot& JsonHotspot)
{
	return FGLTFJsonHotspotIndex(JsonRoot.Hotspots.Add(JsonHotspot));
}

FGLTFJsonNodeIndex FGLTFJsonBuilder::AddChildNode(FGLTFJsonNodeIndex ParentIndex, const FGLTFJsonNode& JsonNode)
{
	const FGLTFJsonNodeIndex ChildIndex = AddNode(JsonNode);

	if (ParentIndex != INDEX_NONE)
	{
		GetNode(ParentIndex).Children.Add(ChildIndex);
	}

	return ChildIndex;
}

FGLTFJsonNodeIndex FGLTFJsonBuilder::AddChildComponentNode(FGLTFJsonNodeIndex ParentIndex, const FGLTFJsonNode& JsonNode)
{
	const FGLTFJsonNodeIndex ChildIndex = AddChildNode(ParentIndex, JsonNode);

	if (ParentIndex != INDEX_NONE)
	{
		GetNode(ParentIndex).ComponentNode = ChildIndex;
	}

	return ChildIndex;
}

FGLTFJsonAccessor& FGLTFJsonBuilder::GetAccessor(FGLTFJsonAccessorIndex AccessorIndex)
{
	return JsonRoot.Accessors[AccessorIndex];
}

FGLTFJsonAnimation& FGLTFJsonBuilder::GetAnimation(FGLTFJsonAnimationIndex AnimationIndex)
{
	return JsonRoot.Animations[AnimationIndex];
}

FGLTFJsonBuffer& FGLTFJsonBuilder::GetBuffer(FGLTFJsonBufferIndex BufferIndex)
{
	return JsonRoot.Buffers[BufferIndex];
}

FGLTFJsonBufferView& FGLTFJsonBuilder::GetBufferView(FGLTFJsonBufferViewIndex BufferViewIndex)
{
	return JsonRoot.BufferViews[BufferViewIndex];
}

FGLTFJsonCamera& FGLTFJsonBuilder::GetCamera(FGLTFJsonCameraIndex CameraIndex)
{
	return JsonRoot.Cameras[CameraIndex];
}

FGLTFJsonImage& FGLTFJsonBuilder::GetImage(FGLTFJsonImageIndex ImageIndex)
{
	return JsonRoot.Images[ImageIndex];
}

FGLTFJsonMaterial& FGLTFJsonBuilder::GetMaterial(FGLTFJsonMaterialIndex MaterialIndex)
{
	return JsonRoot.Materials[MaterialIndex];
}

FGLTFJsonMesh& FGLTFJsonBuilder::GetMesh(FGLTFJsonMeshIndex MeshIndex)
{
	return JsonRoot.Meshes[MeshIndex];
}

FGLTFJsonNode& FGLTFJsonBuilder::GetNode(FGLTFJsonNodeIndex NodeIndex)
{
	return JsonRoot.Nodes[NodeIndex];
}

FGLTFJsonSampler& FGLTFJsonBuilder::GetSampler(FGLTFJsonSamplerIndex SamplerIndex)
{
	return JsonRoot.Samplers[SamplerIndex];
}

FGLTFJsonScene& FGLTFJsonBuilder::GetScene(FGLTFJsonSceneIndex SceneIndex)
{
	return JsonRoot.Scenes[SceneIndex];
}

FGLTFJsonSkin& FGLTFJsonBuilder::GetSkin(FGLTFJsonSkinIndex SkinIndex)
{
	return JsonRoot.Skins[SkinIndex];
}

FGLTFJsonTexture& FGLTFJsonBuilder::GetTexture(FGLTFJsonTextureIndex TextureIndex)
{
	return JsonRoot.Textures[TextureIndex];
}

FGLTFJsonBackdrop& FGLTFJsonBuilder::GetBackdrop(FGLTFJsonBackdropIndex BackdropIndex)
{
	return JsonRoot.Backdrops[BackdropIndex];
}

FGLTFJsonVariation& FGLTFJsonBuilder::GetVariation(FGLTFJsonVariationIndex VariationIndex)
{
	return JsonRoot.Variations[VariationIndex];
}

FGLTFJsonLightMap& FGLTFJsonBuilder::GetLightMap(FGLTFJsonLightMapIndex LightMapIndex)
{
	return JsonRoot.LightMaps[LightMapIndex];
}

FGLTFJsonLight& FGLTFJsonBuilder::GetLight(FGLTFJsonLightIndex LightIndex)
{
	return JsonRoot.Lights[LightIndex];
}

FGLTFJsonHotspot& FGLTFJsonBuilder::GetHotspot(FGLTFJsonHotspotIndex HotspotIndex)
{
	return JsonRoot.Hotspots[HotspotIndex];
}

FGLTFJsonNodeIndex FGLTFJsonBuilder::GetComponentNodeIndex(FGLTFJsonNodeIndex NodeIndex)
{
	if (NodeIndex == INDEX_NONE)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const FGLTFJsonNode& Node = GetNode(NodeIndex);
	return Node.ComponentNode != INDEX_NONE ? Node.ComponentNode : NodeIndex;
}

bool FGLTFJsonBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	JsonRoot.Serialize(&Archive, true);
	return true;
}

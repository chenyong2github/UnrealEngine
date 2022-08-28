// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFJsonBuilder.h"
#include "GLTFExporterModule.h"
#include "Interfaces/IPluginManager.h"
#include "Runtime/Launch/Resources/Version.h"

FGLTFJsonBuilder::FGLTFJsonBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FGLTFTaskBuilder(FilePath, ExportOptions)
	, DefaultScene(JsonRoot.DefaultScene)
{
	JsonRoot.Asset.Generator = GetGeneratorString();
}

void FGLTFJsonBuilder::WriteJson(FArchive& Archive)
{
	JsonRoot.WriteJson(Archive, !bIsGlbFile, ExportOptions->bSkipNearDefaultValues ? KINDA_SMALL_NUMBER : 0);
}

TSet<EGLTFJsonExtension> FGLTFJsonBuilder::GetCustomExtensionsUsed() const
{
	TSet<EGLTFJsonExtension> CustomExtensions;

	for (EGLTFJsonExtension Extension : JsonRoot.Extensions.Used)
	{
		if (IsCustomExtension(Extension))
		{
			CustomExtensions.Add(Extension);
		}
	}

	return CustomExtensions;
}

void FGLTFJsonBuilder::AddExtension(EGLTFJsonExtension Extension, bool bIsRequired)
{
	JsonRoot.Extensions.Used.Add(Extension);
	if (bIsRequired)
	{
		JsonRoot.Extensions.Required.Add(Extension);
	}
}

FGLTFJsonAccessor* FGLTFJsonBuilder::AddAccessor()
{
	return JsonRoot.Accessors.Add();
}

FGLTFJsonAnimation* FGLTFJsonBuilder::AddAnimation()
{
	return JsonRoot.Animations.Add();
}

FGLTFJsonBuffer* FGLTFJsonBuilder::AddBuffer()
{
	return JsonRoot.Buffers.Add();
}

FGLTFJsonBufferView* FGLTFJsonBuilder::AddBufferView()
{
	return JsonRoot.BufferViews.Add();
}

FGLTFJsonCamera* FGLTFJsonBuilder::AddCamera()
{
	return JsonRoot.Cameras.Add();
}

FGLTFJsonImage* FGLTFJsonBuilder::AddImage()
{
	return JsonRoot.Images.Add();
}

FGLTFJsonMaterial* FGLTFJsonBuilder::AddMaterial()
{
	return JsonRoot.Materials.Add();
}

FGLTFJsonMesh* FGLTFJsonBuilder::AddMesh()
{
	return JsonRoot.Meshes.Add();
}

FGLTFJsonNode* FGLTFJsonBuilder::AddNode()
{
	return JsonRoot.Nodes.Add();
}

FGLTFJsonSampler* FGLTFJsonBuilder::AddSampler()
{
	return JsonRoot.Samplers.Add();
}

FGLTFJsonScene* FGLTFJsonBuilder::AddScene()
{
	return JsonRoot.Scenes.Add();
}

FGLTFJsonSkin* FGLTFJsonBuilder::AddSkin()
{
	return JsonRoot.Skins.Add();
}

FGLTFJsonTexture* FGLTFJsonBuilder::AddTexture()
{
	return JsonRoot.Textures.Add();
}

FGLTFJsonBackdrop* FGLTFJsonBuilder::AddBackdrop()
{
	return JsonRoot.Backdrops.Add();
}

FGLTFJsonHotspot* FGLTFJsonBuilder::AddHotspot()
{
	return JsonRoot.Hotspots.Add();
}

FGLTFJsonLight* FGLTFJsonBuilder::AddLight()
{
	return JsonRoot.Lights.Add();
}

FGLTFJsonLightMap* FGLTFJsonBuilder::AddLightMap()
{
	return JsonRoot.LightMaps.Add();
}

FGLTFJsonSkySphere* FGLTFJsonBuilder::AddSkySphere()
{
	return JsonRoot.SkySpheres.Add();
}

FGLTFJsonEpicLevelVariantSets* FGLTFJsonBuilder::AddEpicLevelVariantSets()
{
	return JsonRoot.EpicLevelVariantSets.Add();
}

FGLTFJsonKhrMaterialVariant* FGLTFJsonBuilder::AddKhrMaterialVariant()
{
	return JsonRoot.KhrMaterialVariants.Add();
}

FGLTFJsonNode* FGLTFJsonBuilder::AddChildNode(FGLTFJsonNode* ParentNode)
{
	FGLTFJsonNode* ChildNode = AddNode();

	if (ParentNode != nullptr)
	{
		ParentNode->Children.Add(ChildNode);
	}

	return ChildNode;
}

FGLTFJsonNode* FGLTFJsonBuilder::AddChildComponentNode(FGLTFJsonNode* ParentNode)
{
	FGLTFJsonNode* ChildNode = AddChildNode(ParentNode);

	if (ParentNode != nullptr)
	{
		ParentNode->ComponentNode = ChildNode;
	}

	return ChildNode;
}

FGLTFJsonAccessor* FGLTFJsonBuilder::AddAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return JsonRoot.Accessors.Add(JsonAccessor);
}

FGLTFJsonBuffer* FGLTFJsonBuilder::AddBuffer(const FGLTFJsonBuffer& JsonBuffer)
{
	return JsonRoot.Buffers.Add(JsonBuffer);
}

FGLTFJsonBufferView* FGLTFJsonBuilder::AddBufferView(const FGLTFJsonBufferView& JsonBufferView)
{
	return JsonRoot.BufferViews.Add(JsonBufferView);
}

FGLTFJsonCamera* FGLTFJsonBuilder::AddCamera(const FGLTFJsonCamera& JsonCamera)
{
	return JsonRoot.Cameras.Add(JsonCamera);
}

FGLTFJsonImage* FGLTFJsonBuilder::AddImage(const FGLTFJsonImage& JsonImage)
{
	return JsonRoot.Images.Add(JsonImage);
}

FGLTFJsonMaterial* FGLTFJsonBuilder::AddMaterial(const FGLTFJsonMaterial& JsonMaterial)
{
	return JsonRoot.Materials.Add(JsonMaterial);
}

FGLTFJsonMesh* FGLTFJsonBuilder::AddMesh(const FGLTFJsonMesh& JsonMesh)
{
	return JsonRoot.Meshes.Add(JsonMesh);
}

FGLTFJsonNode* FGLTFJsonBuilder::AddNode(const FGLTFJsonNode& JsonNode)
{
	return JsonRoot.Nodes.Add(JsonNode);
}

FGLTFJsonSampler* FGLTFJsonBuilder::AddSampler(const FGLTFJsonSampler& JsonSampler)
{
	return JsonRoot.Samplers.Add(JsonSampler);
}

FGLTFJsonScene* FGLTFJsonBuilder::AddScene(const FGLTFJsonScene& JsonScene)
{
	return JsonRoot.Scenes.Add(JsonScene);
}

FGLTFJsonSkin* FGLTFJsonBuilder::AddSkin(const FGLTFJsonSkin& JsonSkin)
{
	return JsonRoot.Skins.Add(JsonSkin);
}

FGLTFJsonTexture* FGLTFJsonBuilder::AddTexture(const FGLTFJsonTexture& JsonTexture)
{
	return JsonRoot.Textures.Add(JsonTexture);
}

FGLTFJsonBackdrop* FGLTFJsonBuilder::AddBackdrop(const FGLTFJsonBackdrop& JsonBackdrop)
{
	return JsonRoot.Backdrops.Add(JsonBackdrop);
}

FGLTFJsonHotspot* FGLTFJsonBuilder::AddHotspot(const FGLTFJsonHotspot& JsonHotspot)
{
	return JsonRoot.Hotspots.Add(JsonHotspot);
}

FGLTFJsonLight* FGLTFJsonBuilder::AddLight(const FGLTFJsonLight& JsonLight)
{
	return JsonRoot.Lights.Add(JsonLight);
}

FGLTFJsonLightMap* FGLTFJsonBuilder::AddLightMap(const FGLTFJsonLightMap& JsonLightMap)
{
	return JsonRoot.LightMaps.Add(JsonLightMap);
}

FGLTFJsonSkySphere* FGLTFJsonBuilder::AddSkySphere(const FGLTFJsonSkySphere& JsonSkySphere)
{
	return JsonRoot.SkySpheres.Add(JsonSkySphere);
}

FGLTFJsonEpicLevelVariantSets* FGLTFJsonBuilder::AddEpicLevelVariantSets(const FGLTFJsonEpicLevelVariantSets& JsonEpicLevelVariantSets)
{
	return JsonRoot.EpicLevelVariantSets.Add(JsonEpicLevelVariantSets);
}

FGLTFJsonKhrMaterialVariant* FGLTFJsonBuilder::AddKhrMaterialVariant(const FGLTFJsonKhrMaterialVariant& JsonKhrMaterialVariant)
{
	return JsonRoot.KhrMaterialVariants.Add(JsonKhrMaterialVariant);
}

FGLTFJsonNode* FGLTFJsonBuilder::AddChildNode(FGLTFJsonNode* Parent, const FGLTFJsonNode& JsonNode)
{
	FGLTFJsonNode* ChildNode = AddNode(JsonNode);

	if (Parent != nullptr)
	{
		Parent->Children.Add(ChildNode);
	}

	return ChildNode;
}

FGLTFJsonNode* FGLTFJsonBuilder::AddChildComponentNode(FGLTFJsonNode* Parent, const FGLTFJsonNode& JsonNode)
{
	FGLTFJsonNode* ChildNode = AddChildNode(Parent, JsonNode);

	if (Parent != nullptr)
	{
		Parent->ComponentNode = ChildNode;
	}

	return ChildNode;
}

const FGLTFJsonRoot& FGLTFJsonBuilder::GetRoot() const
{
	return JsonRoot;
}

FGLTFJsonAccessor& FGLTFJsonBuilder::GetAccessor(FGLTFJsonAccessor* AccessorIndex)
{
	return *AccessorIndex;
}

FGLTFJsonAnimation& FGLTFJsonBuilder::GetAnimation(FGLTFJsonAnimation* AnimationIndex)
{
	return *AnimationIndex;
}

FGLTFJsonBuffer& FGLTFJsonBuilder::GetBuffer(FGLTFJsonBuffer* BufferIndex)
{
	return *BufferIndex;
}

FGLTFJsonBufferView& FGLTFJsonBuilder::GetBufferView(FGLTFJsonBufferView* BufferViewIndex)
{
	return *BufferViewIndex;
}

FGLTFJsonCamera& FGLTFJsonBuilder::GetCamera(FGLTFJsonCamera* CameraIndex)
{
	return *CameraIndex;
}

FGLTFJsonImage& FGLTFJsonBuilder::GetImage(FGLTFJsonImage* ImageIndex)
{
	return *ImageIndex;
}

FGLTFJsonMaterial& FGLTFJsonBuilder::GetMaterial(FGLTFJsonMaterial* MaterialIndex)
{
	return *MaterialIndex;
}

FGLTFJsonMesh& FGLTFJsonBuilder::GetMesh(FGLTFJsonMesh* MeshIndex)
{
	return *MeshIndex;
}

FGLTFJsonNode& FGLTFJsonBuilder::GetNode(FGLTFJsonNode* NodeIndex)
{
	return *NodeIndex;
}

FGLTFJsonSampler& FGLTFJsonBuilder::GetSampler(FGLTFJsonSampler* SamplerIndex)
{
	return *SamplerIndex;
}

FGLTFJsonScene& FGLTFJsonBuilder::GetScene(FGLTFJsonScene* SceneIndex)
{
	return *SceneIndex;
}

FGLTFJsonSkin& FGLTFJsonBuilder::GetSkin(FGLTFJsonSkin* SkinIndex)
{
	return *SkinIndex;
}

FGLTFJsonTexture& FGLTFJsonBuilder::GetTexture(FGLTFJsonTexture* TextureIndex)
{
	return *TextureIndex;
}

FGLTFJsonBackdrop& FGLTFJsonBuilder::GetBackdrop(FGLTFJsonBackdrop* BackdropIndex)
{
	return *BackdropIndex;
}

FGLTFJsonHotspot& FGLTFJsonBuilder::GetHotspot(FGLTFJsonHotspot* HotspotIndex)
{
	return *HotspotIndex;
}

FGLTFJsonLight& FGLTFJsonBuilder::GetLight(FGLTFJsonLight* LightIndex)
{
	return *LightIndex;
}

FGLTFJsonLightMap& FGLTFJsonBuilder::GetLightMap(FGLTFJsonLightMap* LightMapIndex)
{
	return *LightMapIndex;
}

FGLTFJsonSkySphere& FGLTFJsonBuilder::GetSkySphere(FGLTFJsonSkySphere* SkySphereIndex)
{
	return *SkySphereIndex;
}

FGLTFJsonEpicLevelVariantSets& FGLTFJsonBuilder::GetEpicLevelVariantSets(FGLTFJsonEpicLevelVariantSets* EpicLevelVariantSetsIndex)
{
	return *EpicLevelVariantSetsIndex;
}

FGLTFJsonKhrMaterialVariant& FGLTFJsonBuilder::GetKhrMaterialVariant(FGLTFJsonKhrMaterialVariant* KhrMaterialVariantIndex)
{
	return *KhrMaterialVariantIndex;
}

FGLTFJsonNode* FGLTFJsonBuilder::GetComponentNode(FGLTFJsonNode* Node)
{
	if (Node == nullptr)
	{
		return nullptr;
	}

	return Node->ComponentNode != nullptr ? Node->ComponentNode : Node;
}

FString FGLTFJsonBuilder::GetGeneratorString() const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(GLTFEXPORTER_MODULE_NAME);
	const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();

	return ExportOptions->bIncludeGeneratorVersion
		? TEXT(EPIC_PRODUCT_NAME) TEXT(" ") ENGINE_VERSION_STRING TEXT(" ") + PluginDescriptor.FriendlyName + TEXT(" ") + PluginDescriptor.VersionName
		: TEXT(EPIC_PRODUCT_NAME) TEXT(" ") + PluginDescriptor.FriendlyName;
}

bool FGLTFJsonBuilder::IsCustomExtension(EGLTFJsonExtension Extension)
{
	const TCHAR CustomPrefix[] = TEXT("EPIC_");

	const TCHAR* ExtensionString = FGLTFJsonUtility::GetValue(Extension);
	return FCString::Strncmp(ExtensionString, CustomPrefix, GetNum(CustomPrefix) - 1) == 0;
}

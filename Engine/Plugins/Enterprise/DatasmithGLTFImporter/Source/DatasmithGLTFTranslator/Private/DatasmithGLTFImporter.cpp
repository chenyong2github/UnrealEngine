// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithGLTFImporter.h"

#include "DatasmithGLTFAnimationImporter.h"
#include "DatasmithGLTFImportOptions.h"
#include "DatasmithGLTFMaterialElement.h"
#include "DatasmithGLTFTextureFactory.h"

#include "GLTFAsset.h"
#include "GLTFMaterialFactory.h"
#include "GLTFReader.h"
#include "GLTFStaticMeshFactory.h"

#include "DatasmithAssetImportData.h"
#include "DatasmithSceneFactory.h"
#include "IDatasmithSceneElements.h"
#include "ObjectTemplates/DatasmithStaticMeshTemplate.h"
#include "Utility/DatasmithImporterUtils.h"
#include "DatasmithMeshHelper.h"

#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogDatasmithGLTFImport);

#define LOCTEXT_NAMESPACE "DatasmithGLTFImporter"

class FGLTFMaterialElementFactory : public GLTF::IMaterialElementFactory
{
public:
	IDatasmithScene* CurrentScene;

public:
	FGLTFMaterialElementFactory()
	    : CurrentScene(nullptr)
	{
	}

	virtual GLTF::FMaterialElement* CreateMaterial(const TCHAR* Name, UObject* ParentPackage, EObjectFlags Flags) override
	{
		check(CurrentScene);

		TSharedPtr<IDatasmithUEPbrMaterialElement> MaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(Name);
		CurrentScene->AddMaterial(MaterialElement);
		return new FDatasmithGLTFMaterialElement(MaterialElement);
	}
};

FDatasmithGLTFImporter::FDatasmithGLTFImporter(TSharedRef<IDatasmithScene>& OutScene, UDatasmithGLTFImportOptions* InOptions)
    : DatasmithScene(OutScene)
	, GLTFReader(new GLTF::FFileReader())
    , GLTFAsset(new GLTF::FAsset())
    , StaticMeshFactory(new GLTF::FStaticMeshFactory())
    , MaterialFactory(new GLTF::FMaterialFactory(new FGLTFMaterialElementFactory(), new FDatasmithGLTFTextureFactory()))
    , AnimationImporter(new FDatasmithGLTFAnimationImporter(LogMessages))
	, ImportOptions(InOptions)
{
}

FDatasmithGLTFImporter::~FDatasmithGLTFImporter() {}

void FDatasmithGLTFImporter::SetImportOptions(UDatasmithGLTFImportOptions* InOptions)
{
	ImportOptions = InOptions;
}

const TArray<GLTF::FLogMessage>& FDatasmithGLTFImporter::GetLogMessages() const
{
	LogMessages.Append(GLTFReader->GetLogMessages());
	LogMessages.Append(StaticMeshFactory->GetLogMessages());
	return LogMessages;
}

bool FDatasmithGLTFImporter::OpenFile(const FString& InFileName)
{
	LogMessages.Empty();

	GLTFReader->ReadFile(InFileName, false, true, *GLTFAsset);
	const GLTF::FLogMessage* Found = GLTFReader->GetLogMessages().FindByPredicate(
	    [](const GLTF::FLogMessage& Message) { return Message.Get<0>() == GLTF::EMessageSeverity::Error; });
	if (Found)
	{
		return false;
	}
	check(GLTFAsset->ValidationCheck() == GLTF::FAsset::Valid);

	GLTFAsset->GenerateNames(FPaths::GetBaseFilename(InFileName));

	// check extensions supported
	static const TArray<GLTF::EExtension> SupportedExtensions = {GLTF::EExtension::KHR_MaterialsPbrSpecularGlossiness,
	                                                             GLTF::EExtension::KHR_MaterialsUnlit, GLTF::EExtension::KHR_LightsPunctual};
	for (GLTF::EExtension Extension : GLTFAsset->ExtensionsUsed)
	{
		if (SupportedExtensions.Find(Extension) == INDEX_NONE)
		{
			LogMessages.Emplace(GLTF::EMessageSeverity::Warning, FString::Printf(TEXT("Extension is not supported: %s"), GLTF::ToString(Extension)));
		}
	}

	return true;
}

TSharedPtr<IDatasmithActorElement> FDatasmithGLTFImporter::CreateCameraActor(int32 CameraIndex) const
{
	const GLTF::FCamera& Camera = GLTFAsset->Cameras[CameraIndex];

	TSharedRef<IDatasmithCameraActorElement> CameraElement = FDatasmithSceneFactory::CreateCameraActor(*Camera.Name);

	float       AspectRatio;
	float       FocalLength;
	const float SensorWidth = 36.f;  // mm
	CameraElement->SetSensorWidth(SensorWidth);
	if (Camera.bIsPerspective)
	{
		AspectRatio = Camera.Perspective.AspectRatio;
		FocalLength = (SensorWidth / Camera.Perspective.AspectRatio) / (2.0 * tan(Camera.Perspective.Fov / 2.0));
	}
	else
	{
		AspectRatio = Camera.Orthographic.XMagnification / Camera.Orthographic.YMagnification;
		FocalLength = (SensorWidth / AspectRatio) / (AspectRatio * tan(AspectRatio / 4.0));  // can only approximate Fov
	}

	CameraElement->SetSensorAspectRatio(AspectRatio);
	CameraElement->SetFocalLength(FocalLength);
	CameraElement->SetEnableDepthOfField(false);
	// ignore znear and zfar

	return CameraElement;
}

TSharedPtr<IDatasmithActorElement> FDatasmithGLTFImporter::CreateLightActor(int32 LightIndex) const
{
	const GLTF::FLight& Light = GLTFAsset->Lights[LightIndex];

	TSharedPtr<IDatasmithLightActorElement> LightElement;

	switch (Light.Type)
	{
		case GLTF::FLight::EType::Point:
		{
			TSharedRef<IDatasmithPointLightElement> Point = FDatasmithSceneFactory::CreatePointLight(*Light.Name);
			//  NOTE. spot light should also have these
			Point->SetIntensityUnits(EDatasmithLightUnits::Candelas);
			check(Light.Range > 0.f);
			if (Light.Range)
			{
				Point->SetAttenuationRadius(Light.Range * ImportOptions->ImportScale);
			}
			LightElement = Point;
		}
		break;
		case GLTF::FLight::EType::Spot:
		{
			TSharedRef<IDatasmithSpotLightElement> Spot = FDatasmithSceneFactory::CreateSpotLight(*Light.Name);
			Spot->SetIntensityUnits(EDatasmithLightUnits::Candelas);
			Spot->SetInnerConeAngle(FMath::RadiansToDegrees(Light.Spot.InnerConeAngle));
			Spot->SetOuterConeAngle(FMath::RadiansToDegrees(Light.Spot.OuterConeAngle));
			LightElement = Spot;
		}
		break;
		case GLTF::FLight::EType::Directional:
		{
			TSharedRef<IDatasmithDirectionalLightElement> Directional = FDatasmithSceneFactory::CreateDirectionalLight(*Light.Name);
			LightElement                                              = Directional;
		}
		break;
		default:
			check(false);
			break;
	}

	// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_lights_punctual/README.md
	// "Brightness of light in.The units that this is defined in depend on the type of light.point and spot lights use luminous intensity in candela(lm / sr) while directional lights use illuminance in lux(lm / m2)"
	LightElement->SetIntensity(Light.Intensity);
	LightElement->SetColor(Light.Color);

	return LightElement;
}

TSharedPtr<IDatasmithMeshActorElement> FDatasmithGLTFImporter::CreateStaticMeshActor(int32 MeshIndex)
{
	TSharedPtr<IDatasmithMeshActorElement> MeshActorElement;

	if (!ImportedMeshes.Contains(MeshIndex))
	{
		ImportedMeshes.Add(MeshIndex);

		TSharedRef<IDatasmithMeshElement> MeshElement = FDatasmithSceneFactory::CreateMesh(*GLTFAsset->Meshes[MeshIndex].Name);

		const TArray<GLTF::FMaterialElement*>& Materials = MaterialFactory->GetMaterials();
		for (int32 MaterialID = 0; MaterialID < Materials.Num(); ++MaterialID)
		{
			GLTF::FMaterialElement* Material = Materials[MaterialID];
			MeshElement->SetMaterial(*Material->GetName(), MaterialID);
		}

		if (ImportOptions->bGenerateLightmapUVs)
		{
			MeshElement->SetLightmapSourceUV(0);
			MeshElement->SetLightmapCoordinateIndex(-1);
		}
		else
		{
			MeshElement->SetLightmapCoordinateIndex(0);
		}

		MeshElementToGLTFMeshIndex.Add(&MeshElement.Get(), MeshIndex);
		GLTFMeshIndexToMeshElement.Add(MeshIndex, MeshElement);
		DatasmithScene->AddMesh(MeshElement);
	}

	MeshActorElement = FDatasmithSceneFactory::CreateMeshActor(TEXT("TempName"));
	MeshActorElement->SetStaticMeshPathName(*FDatasmithUtils::SanitizeObjectName(GLTFAsset->Meshes[MeshIndex].Name));
	return MeshActorElement;
}

TSharedPtr<IDatasmithActorElement> FDatasmithGLTFImporter::ConvertNode(int32 NodeIndex)
{
	TSharedPtr<IDatasmithActorElement> ActorElement;

	GLTF::FNode& Node = GLTFAsset->Nodes[NodeIndex];
	check(!Node.Name.IsEmpty());

	FTransform Transform = Node.Transform;

	switch (Node.Type)
	{
		case GLTF::FNode::EType::Mesh:
		case GLTF::FNode::EType::MeshSkinned:
			if (GLTFAsset->Meshes.IsValidIndex(Node.MeshIndex))
			{
				TSharedPtr<IDatasmithMeshActorElement> MeshActorElement = CreateStaticMeshActor(Node.MeshIndex);

				ActorElement = MeshActorElement;
				ActorElement->SetName(*Node.Name);
			}
			break;
		case GLTF::FNode::EType::Camera:
			ActorElement = CreateCameraActor(Node.CameraIndex);
			// fix GLTF camera orientation
			// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#cameras
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());
			ActorElement->SetName(*Node.Name);
			break;
		case GLTF::FNode::EType::Light:
			ActorElement = CreateLightActor(Node.LightIndex);
			Transform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion());
			ActorElement->SetName(*Node.Name);
			break;
		case GLTF::FNode::EType::Transform:
		case GLTF::FNode::EType::Joint:
		default:
			// Create regular actor
			ActorElement = FDatasmithSceneFactory::CreateActor(*Node.Name);
			break;
	}

	if (!ActorElement)
	{
		return ActorElement;
	}

	FString NodeOriginalName = Node.Name.RightChop(Node.Name.Find(TEXT("_")) + 1);
	ActorElement->AddTag(*NodeOriginalName);
	ActorElement->SetLabel(*NodeOriginalName);

	SetActorElementTransform(ActorElement, Transform);

	for (int32 Index = 0; Index < Node.Children.Num(); ++Index)
	{
		AddActorElementChild(ActorElement, ConvertNode(Node.Children[Index]));
	}

	return ActorElement;
}

bool FDatasmithGLTFImporter::SendSceneToDatasmith()
{
	if (GLTFAsset->ValidationCheck() != GLTF::FAsset::Valid)
	{
		return false;
	}

	// Setup importer
	AnimationImporter->SetUniformScale(ImportOptions->ImportScale);
	StaticMeshFactory->SetUniformScale(ImportOptions->ImportScale);
	StaticMeshFactory->SetGenerateLightmapUVs(ImportOptions->bGenerateLightmapUVs);

	FDatasmithGLTFTextureFactory& TextureFactory     = static_cast<FDatasmithGLTFTextureFactory&>(MaterialFactory->GetTextureFactory());
	FGLTFMaterialElementFactory&  ElementFactory     = static_cast<FGLTFMaterialElementFactory&>(MaterialFactory->GetMaterialElementFactory());
	TextureFactory.CurrentScene                      = &DatasmithScene.Get();
	ElementFactory.CurrentScene                      = &DatasmithScene.Get();
	const TArray<GLTF::FMaterialElement*>& Materials = MaterialFactory->CreateMaterials(*GLTFAsset, nullptr, EObjectFlags::RF_NoFlags);
	check(Materials.Num() == GLTFAsset->Materials.Num());

	// Perform conversion
	ImportedMeshes.Empty();

	TArray<int32> RootNodes;
	GLTFAsset->GetRootNodes(RootNodes);
	for (int32 RootIndex : RootNodes)
	{
		const GLTF::FNode& Node = GLTFAsset->Nodes[RootIndex];

		const TSharedPtr<IDatasmithActorElement> NodeActor = ConvertNode(RootIndex);
		if (NodeActor.IsValid())
		{
			DatasmithScene->AddActor(NodeActor);
		}
	}

	AnimationImporter->CurrentScene = &DatasmithScene.Get();
	AnimationImporter->CreateAnimations(*GLTFAsset);

	return true;
}

void FDatasmithGLTFImporter::GetGeometriesForMeshElementAndRelease(const TSharedRef<IDatasmithMeshElement> MeshElement, TArray<FMeshDescription>& OutMeshDescriptions)
{
	if (int32* MeshIndexPtr = MeshElementToGLTFMeshIndex.Find(&MeshElement.Get()))
	{
		int32 MeshIndex = *MeshIndexPtr;

		FMeshDescription MeshDescription;
		DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

		StaticMeshFactory->FillMeshDescription(GLTFAsset->Meshes[MeshIndex], &MeshDescription);

		OutMeshDescriptions.Add(MoveTemp(MeshDescription));
	}
}

const TArray<TSharedRef<IDatasmithLevelSequenceElement>>& FDatasmithGLTFImporter::GetImportedSequences()
{
	return AnimationImporter->GetImportedSequences();
}

void FDatasmithGLTFImporter::UnloadScene()
{
	StaticMeshFactory->CleanUp();
	MaterialFactory->CleanUp();
	GLTFAsset->Clear(8 * 1024, 512);

	GLTFReader.Reset(new GLTF::FFileReader());
	GLTFAsset.Reset(new GLTF::FAsset());
	StaticMeshFactory.Reset(new GLTF::FStaticMeshFactory());
	MaterialFactory.Reset(new GLTF::FMaterialFactory(new FGLTFMaterialElementFactory(), new FDatasmithGLTFTextureFactory()));
}

void FDatasmithGLTFImporter::SetActorElementTransform(TSharedPtr<IDatasmithActorElement> ActorElement, const FTransform &Transform)
{
	if (Transform.GetRotation().IsNormalized())
	{
		ActorElement->SetRotation(Transform.GetRotation());
	}
	else
	{
		LogMessages.Emplace(GLTF::EMessageSeverity::Warning, FString::Printf(TEXT("Actor %s rotation is not normalized"), ActorElement->GetLabel()));
	}

	if (Transform.GetScale3D().IsNearlyZero())
	{
		FVector Scale = Transform.GetScale3D();
		LogMessages.Emplace(GLTF::EMessageSeverity::Warning, FString::Printf(TEXT("Actor %s scale(%f, %f, %f) is nearly zero"), ActorElement->GetLabel(), Scale.X, Scale.Y, Scale.Z));
	}
	ActorElement->SetScale(Transform.GetScale3D());

	ActorElement->SetTranslation(Transform.GetTranslation() * StaticMeshFactory->GetUniformScale());
	ActorElement->SetUseParentTransform(TransformIsLocal);
}

void FDatasmithGLTFImporter::AddActorElementChild(TSharedPtr<IDatasmithActorElement> ActorElement, const TSharedPtr<IDatasmithActorElement>& ChildNodeActor)
{
	if (ChildNodeActor.IsValid())
	{
		ActorElement->AddChild(ChildNodeActor, TransformIsLocal ? EDatasmithActorAttachmentRule::KeepRelativeTransform : EDatasmithActorAttachmentRule::KeepWorldTransform);
	}
}

#undef LOCTEXT_NAMESPACE

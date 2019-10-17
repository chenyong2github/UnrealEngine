// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneXmlReader.h"

#include "DatasmithCore.h"
#include "DatasmithDefinitions.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

#include "Algo/Find.h"
#include "Containers/ArrayView.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Templates/SharedPointer.h"
#include "XmlParser.h"

FDatasmithSceneXmlReader::FDatasmithSceneXmlReader() = default;
FDatasmithSceneXmlReader::~FDatasmithSceneXmlReader() = default;

namespace DatasmithSceneXmlReaderImpl
{
	const TCHAR* ActorTags[] = { DATASMITH_ACTORNAME, DATASMITH_ACTORMESHNAME, DATASMITH_CAMERANAME, DATASMITH_LIGHTNAME,
		DATASMITH_CUSTOMACTORNAME, DATASMITH_LANDSCAPENAME, DATASMITH_POSTPROCESSVOLUME, DATASMITH_ACTORHIERARCHICALINSTANCEDMESHNAME };

	template< typename T >
	T ValueFromString( const FString& InString )
	{
		// Invalid
	}

	template<>
	float ValueFromString< float >( const FString& InString )
	{
		return FCString::Atof( *InString );
	}

	template<>
	FVector ValueFromString< FVector >( const FString& InString )
	{
		FVector Value;
		Value.InitFromString( InString );

		return Value;
	}

	template<>
	FColor ValueFromString< FColor >( const FString& InString )
	{
		FColor Value;
		Value.InitFromString( InString );

		return Value;
	}

	template<>
	FLinearColor ValueFromString< FLinearColor >( const FString& InString )
	{
		FLinearColor Value;
		Value.InitFromString( InString );

		return Value;
	}

	template<>
	int32 ValueFromString< int32 >( const FString& InString )
	{
		return FCString::Atoi( *InString );
	}

	template<>
	bool ValueFromString< bool >( const FString& InString )
	{
		return InString.ToBool();
	}
}

FString FDatasmithSceneXmlReader::ResolveFilePath(const FString& AssetFile) const
{
	if ( !FPaths::IsRelative( AssetFile ) )
	{
		return AssetFile;
	}

	FString FullAssetPath = FPaths::Combine(ProjectPath, AssetFile);

	if ( FPaths::FileExists(FullAssetPath) )
	{
		return FullAssetPath;
	}
	else
	{
		return AssetFile;
	}
}

void FDatasmithSceneXmlReader::PatchUpVersion(TSharedRef< IDatasmithScene >& OutScene)
{
	//@todo parse version string in proper version object
	// Handle legacy behavior, when materials from the first actor using a mesh applied its materials to it.
	// Materials on meshes appeared in 0.19
	if (FCString::Atof(OutScene->GetExporterVersion()) < 0.19f)
	{
		TArray<TSharedPtr< IDatasmithMeshActorElement>> MeshActors = FDatasmithSceneUtils::GetAllMeshActorsFromScene(OutScene);
		TMap<FString, TSharedPtr< IDatasmithMeshActorElement>> ActorUsingMeshMap;
		for (const TSharedPtr< IDatasmithMeshActorElement >& MeshActor : MeshActors)
		{
			if (MeshActor.IsValid() && !ActorUsingMeshMap.Contains(MeshActor->GetStaticMeshPathName()))
			{
				ActorUsingMeshMap.Add(MeshActor->GetStaticMeshPathName(), MeshActor);
			}
		}

		for (int32 MeshIndex = 0; MeshIndex < OutScene->GetMeshesCount(); ++MeshIndex)
		{
			if (OutScene->GetMesh(MeshIndex)->GetMaterialSlotCount() == 0 && ActorUsingMeshMap.Contains(OutScene->GetMesh(MeshIndex)->GetName()))
			{
				TSharedPtr< IDatasmithMeshActorElement> MeshActor = ActorUsingMeshMap[OutScene->GetMesh(MeshIndex)->GetName()];
				for (int i = 0; i < MeshActor->GetMaterialOverridesCount(); ++i)
				{
					OutScene->GetMesh(MeshIndex)->SetMaterial(MeshActor->GetMaterialOverride(i)->GetName(), MeshActor->GetMaterialOverride(i)->GetId() == -1 ? 0 : MeshActor->GetMaterialOverride(i)->GetId());
				}
			}
		}
	}
}

void FDatasmithSceneXmlReader::ParseElement(FXmlNode* InNode, TSharedRef<IDatasmithElement> OutElement) const
{
	OutElement->SetLabel( *InNode->GetAttribute( TEXT("label") ) );
}

void FDatasmithSceneXmlReader::ParseLevelSequence(FXmlNode* InNode, const TSharedRef<IDatasmithLevelSequenceElement>& OutElement) const
{
	const TArray<FXmlNode*>& ChildrenNodes = InNode->GetChildrenNodes();
	for (int j = 0; j < ChildrenNodes.Num(); ++j)
	{
		if (ChildrenNodes[j]->GetTag().Compare( TEXT("file"), ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetFile( *ResolveFilePath(ChildrenNodes[j]->GetAttribute(TEXT("path"))) );
		}
		else if (ChildrenNodes[j]->GetTag().Compare(DATASMITH_HASH, ESearchCase::IgnoreCase) == 0)
		{
			FMD5Hash Hash;
			LexFromString(Hash, *ChildrenNodes[j]->GetAttribute(TEXT("value")));
			OutElement->SetFileHash(Hash);
		}
	}
}

void FDatasmithSceneXmlReader::ParseMesh(FXmlNode* InNode, TSharedPtr<IDatasmithMeshElement>& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	const TArray<FXmlNode*>& MeshNodes = InNode->GetChildrenNodes();
	for (int j = 0; j < MeshNodes.Num(); j++)
	{
		if (MeshNodes[j]->GetTag().Compare( TEXT("file"), ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetFile( *ResolveFilePath(MeshNodes[j]->GetAttribute(TEXT("path"))) );
		}
		else if (MeshNodes[j]->GetTag().Compare(TEXT("Size"), ESearchCase::IgnoreCase) == 0)
		{
			float Area = FCString::Atod(*MeshNodes[j]->GetAttribute(TEXT("a")));
			float Width = FCString::Atod(*MeshNodes[j]->GetAttribute(TEXT("x")));
			float Height = FCString::Atod(*MeshNodes[j]->GetAttribute(TEXT("y")));
			float Depth = FCString::Atod(*MeshNodes[j]->GetAttribute(TEXT("z")));
			OutElement->SetDimensions(Area, Width, Height, Depth);
		}
		else if (MeshNodes[j]->GetTag().Compare(DATASMITH_LIGHTMAPCOORDINATEINDEX, ESearchCase::IgnoreCase) ==0)
		{
			OutElement->SetLightmapCoordinateIndex(FCString::Atoi(*MeshNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if (MeshNodes[j]->GetTag().Compare(DATASMITH_LIGHTMAPUVSOURCE, ESearchCase::IgnoreCase) ==0)
		{
			OutElement->SetLightmapSourceUV(FCString::Atoi(*MeshNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if (MeshNodes[j]->GetTag().Compare(DATASMITH_HASH, ESearchCase::IgnoreCase) == 0)
		{
			FMD5Hash Hash;
			LexFromString(Hash, *MeshNodes[j]->GetAttribute(TEXT("value")));
			OutElement->SetFileHash(Hash);
		}
		else if (MeshNodes[j]->GetTag().Compare(DATASMITH_MATERIAL, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetMaterial(*MeshNodes[j]->GetAttribute(TEXT("name")), FCString::Atoi(*MeshNodes[j]->GetAttribute(TEXT("id"))));
		}

	}
}

void FDatasmithSceneXmlReader::ParseTextureElement(FXmlNode* InNode, TSharedPtr<IDatasmithTextureElement>& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	FString GammaAttribute = InNode->GetAttribute(TEXT("rgbcurve"));
	if (GammaAttribute.IsEmpty() == false)
	{
		OutElement->SetRGBCurve(FCString::Atod(*GammaAttribute));
	}

	FString StrValue = InNode->GetAttribute(TEXT("texturemode"));
	if (StrValue != TEXT(""))
	{
		OutElement->SetTextureMode((EDatasmithTextureMode)FCString::Atoi(*StrValue));
	}
	StrValue = InNode->GetAttribute(TEXT("texturefilter"));
	if (StrValue != TEXT(""))
	{
		OutElement->SetTextureFilter((EDatasmithTextureFilter)FCString::Atoi(*StrValue));
	}
	StrValue = InNode->GetAttribute(TEXT("textureaddressx"));
	if (StrValue != TEXT(""))
	{
		OutElement->SetTextureAddressX((EDatasmithTextureAddress)FCString::Atoi(*StrValue));
	}
	StrValue = InNode->GetAttribute(TEXT("textureaddressy"));
	if (StrValue != TEXT(""))
	{
		OutElement->SetTextureAddressY((EDatasmithTextureAddress)FCString::Atoi(*StrValue));
	}
	OutElement->SetFile(*ResolveFilePath(InNode->GetAttribute(TEXT("file"))));

	const TArray<FXmlNode*>& TexNode = InNode->GetChildrenNodes();
	for (int i = 0; i < TexNode.Num(); ++i)
	{
		if (TexNode[i]->GetTag().Compare(DATASMITH_HASH, ESearchCase::IgnoreCase) == 0)
		{
			FMD5Hash Hash;
			LexFromString(Hash, *TexNode[i]->GetAttribute(TEXT("value")));
			OutElement->SetFileHash(Hash);
		}
	}
}

void FDatasmithSceneXmlReader::ParseTexture(FXmlNode* InNode, FString& OutTextureFilename, FDatasmithTextureSampler& OutTextureSampler) const
{
	OutTextureFilename = InNode->GetAttribute(TEXT("tex"));
	OutTextureSampler.ScaleX = FCString::Atod(*InNode->GetAttribute(TEXT("sx")));
	OutTextureSampler.ScaleY = FCString::Atod(*InNode->GetAttribute(TEXT("sy")));
	OutTextureSampler.OffsetX = FCString::Atod(*InNode->GetAttribute(TEXT("ox")));
	OutTextureSampler.OffsetY = FCString::Atod(*InNode->GetAttribute(TEXT("oy")));
	OutTextureSampler.MirrorX = FCString::Atoi(*InNode->GetAttribute(TEXT("mx")));
	OutTextureSampler.MirrorY = FCString::Atoi(*InNode->GetAttribute(TEXT("my")));
	OutTextureSampler.Rotation = FCString::Atod(*InNode->GetAttribute(TEXT("rot")));
	OutTextureSampler.Multiplier = FCString::Atod(*InNode->GetAttribute(TEXT("mul")));
	OutTextureSampler.OutputChannel = FCString::Atoi(*InNode->GetAttribute(TEXT("channel")));
	OutTextureSampler.CoordinateIndex = FCString::Atoi(*InNode->GetAttribute(TEXT("coordinate")));

	if (FCString::Atoi(*InNode->GetAttribute(TEXT("inv"))) == 1)
	{
		OutTextureSampler.bInvert = true;
	}

	if (FCString::Atoi(*InNode->GetAttribute(TEXT("cropped"))) == 1)
	{
		OutTextureSampler.bCroppedTexture = true;
	}
}

void FDatasmithSceneXmlReader::ParseTransform(FXmlNode* InNode, TSharedPtr< IDatasmithActorElement >& OutElement) const
{
	OutElement->SetTranslation(FCString::Atof(*InNode->GetAttribute(TEXT("tx"))), FCString::Atof(*InNode->GetAttribute(TEXT("ty"))),
		FCString::Atof(*InNode->GetAttribute(TEXT("tz"))));

	FString RotationBlob(InNode->GetAttribute(TEXT("qhex")));
	if (!RotationBlob.IsEmpty())
	{
		OutElement->SetRotation( QuatFromHexString( RotationBlob ) );
	}
	else
	{
		OutElement->SetRotation(FCString::Atof(*InNode->GetAttribute(TEXT("qx"))), FCString::Atof(*InNode->GetAttribute(TEXT("qy"))),
			FCString::Atof(*InNode->GetAttribute(TEXT("qz"))), FCString::Atof(*InNode->GetAttribute(TEXT("qw"))));
	}

	OutElement->SetScale(FCString::Atof(*InNode->GetAttribute(TEXT("sx"))), FCString::Atof(*InNode->GetAttribute(TEXT("sy"))), FCString::Atof(*InNode->GetAttribute(TEXT("sz"))));
}

FTransform FDatasmithSceneXmlReader::ParseTransform(FXmlNode* InNode) const
{
	FVector Translation(FCString::Atof(*InNode->GetAttribute(TEXT("tx"))), FCString::Atof(*InNode->GetAttribute(TEXT("ty"))),
		FCString::Atof(*InNode->GetAttribute(TEXT("tz"))));

	FQuat Quaternion;

	FString RotationBlob(InNode->GetAttribute(TEXT("qhex")));
	if (!RotationBlob.IsEmpty())
	{
		Quaternion = QuatFromHexString(RotationBlob);
	}
	else
	{
		Quaternion = FQuat(FCString::Atof(*InNode->GetAttribute(TEXT("qx"))), FCString::Atof(*InNode->GetAttribute(TEXT("qy"))),
			FCString::Atof(*InNode->GetAttribute(TEXT("qz"))), FCString::Atof(*InNode->GetAttribute(TEXT("qw"))));
	}

	FVector Scale(FCString::Atof(*InNode->GetAttribute(TEXT("sx"))), FCString::Atof(*InNode->GetAttribute(TEXT("sy"))), FCString::Atof(*InNode->GetAttribute(TEXT("sz"))));

	return FTransform(Quaternion, Translation, Scale);
}


FQuat FDatasmithSceneXmlReader::QuatFromHexString(const FString& HexString) const
{
	float Floats[4];
	FString::ToHexBlob( HexString, (uint8*)Floats, sizeof(Floats) );

	FQuat QuatResult( Floats[0], Floats[1], Floats[2], Floats[3] );

	return QuatResult;
}

void FDatasmithSceneXmlReader::ParsePostProcess(FXmlNode *InNode, const TSharedPtr< IDatasmithPostProcessElement >& Element) const
{
	ParseElement( InNode, Element.ToSharedRef() );

	const TArray<FXmlNode*>& CompNodes = InNode->GetChildrenNodes();

	FLinearColor Color;
	for (int j = 0; j < CompNodes.Num(); j++)
	{
		if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONTEMP, ESearchCase::IgnoreCase) == 0)
		{
			Element->SetTemperature(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONVIGNETTE, ESearchCase::IgnoreCase) == 0)
		{
			Element->SetVignette(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
		}
		// #ueent_wip: broken PostProcess serialization.
		// - create missing tags
		// - update Writer
// 		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONVIGNETTE, ESearchCase::IgnoreCase) == 0)
// 		{
// 			Element->SetDof(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
// 		}
// 		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONVIGNETTE, ESearchCase::IgnoreCase) == 0)
// 		{
// 			Element->SetMotionBlur(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
// 		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONSATURATION, ESearchCase::IgnoreCase) == 0)
		{
			Element->SetSaturation(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONCOLOR, ESearchCase::IgnoreCase) == 0)
		{
			ParseColor(CompNodes[j], Color);
			Element->SetColorFilter(Color);
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONCAMERAISO, ESearchCase::IgnoreCase) == 0)
		{
			Element->SetCameraISO(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_POSTPRODUCTIONSHUTTERSPEED, ESearchCase::IgnoreCase) == 0)
		{
			Element->SetCameraShutterSpeed(FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))));
		}
		else if ( CompNodes[j]->GetTag().Equals( DATASMITH_FSTOP, ESearchCase::IgnoreCase ) )
		{
			Element->SetDepthOfFieldFstop( FCString::Atof( *CompNodes[j]->GetAttribute( TEXT("value") ) ) );
		}
	}
}

void FDatasmithSceneXmlReader::ParsePostProcessVolume(FXmlNode* InNode, const TSharedRef<IDatasmithPostProcessVolumeElement>& Element) const
{
	ParseElement( InNode, Element );

	for ( FXmlNode* ChildNode : InNode->GetChildrenNodes() )
	{
		if ( ChildNode->GetTag().Equals(DATASMITH_POSTPRODUCTIONNAME, ESearchCase::IgnoreCase) )
		{
			ParsePostProcess(ChildNode, Element->GetSettings());
		}
		else if ( ChildNode->GetTag().Equals( DATASMITH_ENABLED, ESearchCase::IgnoreCase ) )
		{
			Element->SetEnabled( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("value") ) ) );
		}
		else if ( ChildNode->GetTag().Equals( DATASMITH_POSTPROCESSVOLUME_UNBOUND, ESearchCase::IgnoreCase ) )
		{
			Element->SetUnbound( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("value") ) ) );
		}
	}
}

void FDatasmithSceneXmlReader::ParseColor(FXmlNode* InNode, FLinearColor& OutColor) const
{
	OutColor.R = FCString::Atof(*InNode->GetAttribute(TEXT("R")));
	OutColor.G = FCString::Atof(*InNode->GetAttribute(TEXT("G")));
	OutColor.B = FCString::Atof(*InNode->GetAttribute(TEXT("B")));
}

void FDatasmithSceneXmlReader::ParseComp(FXmlNode* InNode, TSharedPtr< IDatasmithCompositeTexture >& OutCompTexture, bool bInIsNormal) const
{
	OutCompTexture->SetMode( (EDatasmithCompMode) FCString::Atoi(*InNode->GetAttribute(TEXT("mode"))) );

	const TArray<FXmlNode*>& CompNodes = InNode->GetChildrenNodes();

	FString FileName;
	FDatasmithTextureSampler TextureSampler;
	FLinearColor Color;

	for (int j = 0; j < CompNodes.Num(); j++)
	{
		if (CompNodes[j]->GetTag().Compare(DATASMITH_TEXTURENAME, ESearchCase::IgnoreCase) == 0)
		{
			ParseTexture(CompNodes[j], FileName, TextureSampler);
			OutCompTexture->AddSurface( *FileName, TextureSampler );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_COLORNAME, ESearchCase::IgnoreCase) == 0)
		{
			ParseColor(CompNodes[j], Color);
			OutCompTexture->AddSurface( Color );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_TEXTURECOMPNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithCompositeTexture > SubCompTex = FDatasmithSceneFactory::CreateCompositeTexture();
			ParseComp(CompNodes[j], SubCompTex, bInIsNormal);
			OutCompTexture->AddSurface( SubCompTex );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_MASKNAME, ESearchCase::IgnoreCase) == 0)
		{
			ParseTexture(CompNodes[j], FileName, TextureSampler);
			OutCompTexture->AddMaskSurface( *FileName, TextureSampler );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_MASKCOLOR, ESearchCase::IgnoreCase) == 0)
		{
			ParseColor(CompNodes[j], Color);
			OutCompTexture->AddMaskSurface( Color );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_MASKCOMPNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithCompositeTexture > SubCompTex = FDatasmithSceneFactory::CreateCompositeTexture();
			ParseComp(CompNodes[j], SubCompTex, bInIsNormal);
			OutCompTexture->AddMaskSurface( SubCompTex );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_VALUE1NAME, ESearchCase::IgnoreCase) == 0)
		{
			OutCompTexture->AddParamVal1( IDatasmithCompositeTexture::ParamVal( FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))), TEXT("") ) );
		}
		else if (CompNodes[j]->GetTag().Compare(DATASMITH_VALUE2NAME, ESearchCase::IgnoreCase) == 0)
		{
			OutCompTexture->AddParamVal2( IDatasmithCompositeTexture::ParamVal( FCString::Atof(*CompNodes[j]->GetAttribute(TEXT("value"))), TEXT("") ) );
		}
	}
}

void FDatasmithSceneXmlReader::ParseActor(FXmlNode* InNode, TSharedPtr<IDatasmithActorElement>& InOutElement, TSharedRef< IDatasmithScene > Scene, TMap< FString, TSharedPtr<IDatasmithActorElement> >& Actors) const
{
	if (InNode->GetTag().Compare(DATASMITH_ACTORMESHNAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithMeshActorElement> MeshElement = FDatasmithSceneFactory::CreateMeshActor(*InNode->GetAttribute(TEXT("name")));
		ParseMeshActor(InNode, MeshElement, Scene);
		InOutElement = MeshElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_LIGHTNAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithLightActorElement > LightElement;
		ParseLight(InNode, LightElement);
		InOutElement = LightElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_CAMERANAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithCameraActorElement > CameraElement = FDatasmithSceneFactory::CreateCameraActor(*InNode->GetAttribute(TEXT("name")));
		ParseCamera(InNode, CameraElement);
		InOutElement = CameraElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_CUSTOMACTORNAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithCustomActorElement > CustomActorElement = FDatasmithSceneFactory::CreateCustomActor(*InNode->GetAttribute(TEXT("name")));
		ParseCustomActor(InNode, CustomActorElement);
		InOutElement = CustomActorElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_LANDSCAPENAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedRef< IDatasmithLandscapeElement > LandscapeElement = FDatasmithSceneFactory::CreateLandscape(*InNode->GetAttribute(TEXT("name")));
		ParseLandscape(InNode, LandscapeElement);
		InOutElement = LandscapeElement;
	}
	else if (InNode->GetTag().Equals(DATASMITH_POSTPROCESSVOLUME, ESearchCase::IgnoreCase))
	{
		TSharedRef< IDatasmithPostProcessVolumeElement > PostElement = FDatasmithSceneFactory::CreatePostProcessVolume(*InNode->GetAttribute(TEXT("name")));
		ParsePostProcessVolume(InNode, PostElement);
		InOutElement = PostElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_ACTORNAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithActorElement > ActorElement = FDatasmithSceneFactory::CreateActor(*InNode->GetAttribute(TEXT("name")));
		ParseElement( InNode, ActorElement.ToSharedRef() );
		InOutElement = ActorElement;
	}
	else if (InNode->GetTag().Compare(DATASMITH_ACTORHIERARCHICALINSTANCEDMESHNAME, ESearchCase::IgnoreCase) == 0)
	{
		TSharedPtr< IDatasmithHierarchicalInstancedStaticMeshActorElement > HierarchicalInstancesStaticMeshElement = FDatasmithSceneFactory::CreateHierarchicalInstanceStaticMeshActor(*InNode->GetAttribute(TEXT("name")));
		ParseHierarchicalInstancedStaticMeshActor(InNode, HierarchicalInstancesStaticMeshElement, Scene);
		InOutElement = HierarchicalInstancesStaticMeshElement;
	}

	if (!InOutElement.IsValid())
	{
		return;
	}

	// Make sure that the InOutElement name is unique. It should be but if it isn't we need to force it to prevent issues down the road.
	FString ActorName = InOutElement->GetName();

	if ( Actors.Contains( ActorName ) )
	{
		FString Prefix = ActorName;
		int32 NameIdx = 1;

		// Update the actor name until we find one that doesn't already exist
		while ( Actors.Contains( ActorName ) )
		{
			++NameIdx;
			ActorName = FString::Printf( TEXT("%s%d"), *Prefix, NameIdx );
		}

		InOutElement->SetName( *ActorName );
	}

	Actors.Add( ActorName, InOutElement );

	InOutElement->SetLayer(*InNode->GetAttribute(TEXT("layer")));

	InOutElement->SetIsAComponent( FCString::ToBool( *InNode->GetAttribute(TEXT("component"))) );

	for (FXmlNode* ChildNode : InNode->GetChildrenNodes())
	{
		if (ChildNode->GetTag().Compare(TEXT("transform"), ESearchCase::IgnoreCase) == 0)
		{
			ParseTransform(ChildNode, InOutElement);
		}
		else if (ChildNode->GetTag().Compare(TEXT("tag"), ESearchCase::IgnoreCase) == 0)
		{
			InOutElement->AddTag(*ChildNode->GetAttribute(TEXT("value")));
		}
		else if (ChildNode->GetTag().Compare(TEXT("children"), ESearchCase::IgnoreCase) == 0)
		{
			// @todo: If attribute does not exist, default value should be true not false.
			InOutElement->SetVisibility(FCString::ToBool(*ChildNode->GetAttribute(TEXT("visible"))));
			InOutElement->SetAsSelector(FCString::ToBool(*ChildNode->GetAttribute(TEXT("selector"))));
			if (InOutElement->IsASelector())
			{
				InOutElement->SetSelectionIndex(FCString::Atoi(*ChildNode->GetAttribute(TEXT("selection"))));
			}

			// Recursively parse the children, can be of any supported actor type
			for (FXmlNode* ChildActorNode : ChildNode->GetChildrenNodes())
			{
				TArrayView< const TCHAR* > ActorTagsView( DatasmithSceneXmlReaderImpl::ActorTags );

				for ( const TCHAR* ActorTag : ActorTagsView )
				{
					if ( ChildActorNode->GetTag().Compare( ActorTag, ESearchCase::IgnoreCase ) == 0 )
					{
						TSharedPtr< IDatasmithActorElement > ChildActorElement;
						ParseActor(ChildActorNode, ChildActorElement, Scene, Actors );
						if (ChildActorElement.IsValid())
						{
							InOutElement->AddChild(ChildActorElement);
						}

						break;
					}
				}
			}
		}
	}
}

void FDatasmithSceneXmlReader::ParseMeshActor(FXmlNode* InNode, TSharedPtr<IDatasmithMeshActorElement>& OutElement, TSharedRef< IDatasmithScene > Scene) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	for (FXmlNode* ChildNode : InNode->GetChildrenNodes())
	{
		if (ChildNode->GetTag().Compare(TEXT("mesh"), ESearchCase::IgnoreCase) == 0)
		{
			int32 StaticMeshIndex = -1;

			if ( !ChildNode->GetAttribute(TEXT("index")).IsEmpty() )
			{
				StaticMeshIndex = FCString::Atoi(*ChildNode->GetAttribute(TEXT("index")));
			}

			if ( StaticMeshIndex < 0 )
			{
				OutElement->SetStaticMeshPathName( *ChildNode->GetAttribute(TEXT("name")) );
			}
			else
			{
				TSharedPtr< IDatasmithMeshElement > MeshElement = Scene->GetMesh( StaticMeshIndex );

				if ( MeshElement.IsValid() )
				{
					OutElement->SetStaticMeshPathName( MeshElement->GetName() );
				}
			}
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_MATERIAL, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithMaterialIDElement > MatElement = FDatasmithSceneFactory::CreateMaterialId(*ChildNode->GetAttribute(TEXT("name")));
			MatElement->SetId(FCString::Atoi(*ChildNode->GetAttribute(TEXT("id"))));
			OutElement->AddMaterialOverride(MatElement);
		}
	}
}

void FDatasmithSceneXmlReader::ParseHierarchicalInstancedStaticMeshActor(FXmlNode* InNode, TSharedPtr<IDatasmithHierarchicalInstancedStaticMeshActorElement>& OutElement, TSharedRef< IDatasmithScene > Scene) const
{
	TSharedPtr<IDatasmithMeshActorElement> OutElementAsMeshActor = OutElement;
	ParseMeshActor(InNode, OutElementAsMeshActor, Scene);

	for (FXmlNode* ChildNode : InNode->GetChildrenNodes())
	{
		if (ChildNode->GetTag().Compare(TEXT("Instances"), ESearchCase::IgnoreCase) == 0)
		{
			OutElement->ReserveSpaceForInstances( FCString::Atoi( *ChildNode->GetAttribute( TEXT("count") ) ) );

			for (FXmlNode* InstanceTransformNode : ChildNode->GetChildrenNodes())
			{
				OutElement->AddInstance(ParseTransform(InstanceTransformNode));
			}
			break;
		}
	}
}

void FDatasmithSceneXmlReader::ParseLight(FXmlNode* InNode, TSharedPtr<IDatasmithLightActorElement>& OutElement) const
{
	FString LightTypeValue = InNode->GetAttribute( TEXT("type") );

	EDatasmithElementType LightType = EDatasmithElementType::SpotLight;

	if ( LightTypeValue.Compare(DATASMITH_POINTLIGHTNAME, ESearchCase::IgnoreCase) == 0 )
	{
		LightType = EDatasmithElementType::PointLight;
	}
	else if ( LightTypeValue.Compare(DATASMITH_SPOTLIGHTNAME, ESearchCase::IgnoreCase) == 0 )
	{
		LightType = EDatasmithElementType::SpotLight;
	}
	else if ( LightTypeValue.Compare(DATASMITH_DIRECTLIGHTNAME, ESearchCase::IgnoreCase) == 0 )
	{
		LightType = EDatasmithElementType::DirectionalLight;
	}
	else if ( LightTypeValue.Compare(DATASMITH_AREALIGHTNAME, ESearchCase::IgnoreCase) == 0 )
	{
		LightType = EDatasmithElementType::AreaLight;
	}
	else if ( LightTypeValue.Compare(DATASMITH_PORTALLIGHTNAME, ESearchCase::IgnoreCase) == 0 )
	{
		LightType = EDatasmithElementType::LightmassPortal;
	}

	TSharedPtr< IDatasmithElement > Element = FDatasmithSceneFactory::CreateElement( LightType, *InNode->GetAttribute(TEXT("name")) );

	if( !Element.IsValid() || !Element->IsA( EDatasmithElementType::Light ) )
	{
		return;
	}

	OutElement = StaticCastSharedPtr< IDatasmithLightActorElement >( Element );
	ParseElement( InNode, OutElement.ToSharedRef() );

	OutElement->SetEnabled( FCString::ToBool( *InNode->GetAttribute(TEXT("enabled")) ) );

	for ( FXmlNode* ChildNode : InNode->GetChildrenNodes() )
	{
		if (ChildNode->GetTag().Compare(DATASMITH_LIGHTCOLORNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetUseTemperature(FCString::ToBool(*ChildNode->GetAttribute(DATASMITH_LIGHTUSETEMPNAME)));
			OutElement->SetTemperature(FCString::Atod(*ChildNode->GetAttribute(DATASMITH_LIGHTTEMPNAME)));

			// Make sure color info is available
			if ( !ChildNode->GetAttribute(TEXT("R")).IsEmpty() )
			{
				FLinearColor Color;
				ParseColor(ChildNode, Color);
				OutElement->SetColor(Color);
			}
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTIESNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetUseIes(true);
			OutElement->SetIesFile( *ResolveFilePath(ChildNode->GetAttribute(TEXT("file"))) );
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTIESBRIGHTNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetIesBrightnessScale(FCString::Atod(*ChildNode->GetAttribute(TEXT("scale"))));
			if(OutElement->GetIesBrightnessScale() > 0.0)
			{
				OutElement->SetUseIesBrightness(true);
			}
			else
			{
				OutElement->SetUseIesBrightness(false);
				OutElement->SetIesBrightnessScale(1.0);
			}
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTIESROTATION, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetIesRotation( QuatFromHexString( ChildNode->GetAttribute( TEXT("qhex") ) ) );
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTINTENSITYNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetIntensity( FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))) );
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTMATERIAL, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetLightFunctionMaterial(*ChildNode->GetAttribute(TEXT("name")));
		}

		if ( OutElement->IsA( EDatasmithElementType::PointLight ) )
		{
			TSharedRef< IDatasmithPointLightElement > PointLightElement = StaticCastSharedRef< IDatasmithPointLightElement >( OutElement.ToSharedRef() );

			if (ChildNode->GetTag().Compare(DATASMITH_LIGHTSOURCESIZENAME, ESearchCase::IgnoreCase) == 0)
			{
				PointLightElement->SetSourceRadius( FCString::Atof(*ChildNode->GetAttribute(TEXT("value"))) );
			}
			else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTSOURCELENGTHNAME, ESearchCase::IgnoreCase) == 0)
			{
				PointLightElement->SetSourceLength( FCString::Atof(*ChildNode->GetAttribute(TEXT("value"))) );
			}
			else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTINTENSITYUNITSNAME, ESearchCase::IgnoreCase) == 0)
			{
				FString LightUnitsValue = ChildNode->GetAttribute( TEXT("value") );

				if ( LightUnitsValue == TEXT("Candelas") )
				{
					PointLightElement->SetIntensityUnits( EDatasmithLightUnits::Candelas );
				}
				else if ( LightUnitsValue == TEXT("Lumens") )
				{
					PointLightElement->SetIntensityUnits( EDatasmithLightUnits::Lumens );
				}
				else
				{
					PointLightElement->SetIntensityUnits( EDatasmithLightUnits::Unitless );
				}
			}
		}

		if ( OutElement->IsA( EDatasmithElementType::SpotLight ) )
		{
			TSharedRef< IDatasmithSpotLightElement > SpotLightElement = StaticCastSharedRef< IDatasmithSpotLightElement >( OutElement.ToSharedRef() );

			if (ChildNode->GetTag().Compare(DATASMITH_LIGHTATTENUATIONRADIUSNAME, ESearchCase::IgnoreCase) == 0)
			{
				SpotLightElement->SetAttenuationRadius( FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))) );
			}
			else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTINNERRADIUSNAME, ESearchCase::IgnoreCase) == 0)
			{
				SpotLightElement->SetInnerConeAngle( FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))) );
			}
			else if (ChildNode->GetTag().Compare(DATASMITH_LIGHTOUTERRADIUSNAME, ESearchCase::IgnoreCase) == 0)
			{
				SpotLightElement->SetOuterConeAngle( FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))) );
			}
		}

		if ( OutElement->IsA( EDatasmithElementType::AreaLight ) )
		{
			TSharedRef< IDatasmithAreaLightElement > AreaLightElement = StaticCastSharedRef< IDatasmithAreaLightElement >( OutElement.ToSharedRef() );

			if ( ChildNode->GetTag().Compare(DATASMITH_AREALIGHTSHAPE, ESearchCase::IgnoreCase) == 0 )
			{
				FString ShapeType = *ChildNode->GetAttribute( TEXT("type") );

				TArrayView< const TCHAR* > ShapeTypeEnumStrings( DatasmithAreaLightShapeStrings );
				int32 ShapeTypeIndexOfEnumValue = ShapeTypeEnumStrings.IndexOfByPredicate( [ TypeString = ShapeType ]( const TCHAR* Value )
				{
					return TypeString == Value;
				} );

				if ( ShapeTypeIndexOfEnumValue != INDEX_NONE )
				{
					AreaLightElement->SetLightShape( (EDatasmithLightShape)ShapeTypeIndexOfEnumValue );
				}

				AreaLightElement->SetWidth( FCString::Atod(*ChildNode->GetAttribute(TEXT("width"))) );
				AreaLightElement->SetLength( FCString::Atod(*ChildNode->GetAttribute(TEXT("length"))) );

				FString AreaLightType = ChildNode->GetAttribute( DATASMITH_AREALIGHTTYPE );

				if ( AreaLightType.IsEmpty() )
				{
					AreaLightType = ChildNode->GetAttribute( DATASMITH_AREALIGHTDISTRIBUTION ); // Used to be called light distribution
				}

				TArrayView< const TCHAR* > LightTypeEnumStrings( DatasmithAreaLightTypeStrings );
				int32 LightTypeIndexOfEnumValue = LightTypeEnumStrings.IndexOfByPredicate( [ TypeString = AreaLightType ]( const TCHAR* Value )
				{
					return TypeString == Value;
				} );

				if ( LightTypeIndexOfEnumValue != INDEX_NONE )
				{
					AreaLightElement->SetLightType( (EDatasmithAreaLightType)LightTypeIndexOfEnumValue );
				}
			}
		}
	}
}

void FDatasmithSceneXmlReader::ParseCamera(FXmlNode* InNode, TSharedPtr<IDatasmithCameraActorElement>& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	for (FXmlNode* ChildNode : InNode->GetChildrenNodes())
	{
		if (ChildNode->GetTag().Compare(DATASMITH_SENSORWIDTH, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetSensorWidth(FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))));
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_SENSORASPECT, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetSensorAspectRatio(FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))));
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_DEPTHOFFIELD, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetEnableDepthOfField( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute(TEXT("enabled")) ) );
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_FOCUSDISTANCE, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetFocusDistance(FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))));
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_FSTOP, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetFStop(FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))));
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_FOCALLENGTH, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetFocalLength(FCString::Atod(*ChildNode->GetAttribute(TEXT("value"))));
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_POSTPRODUCTIONNAME, ESearchCase::IgnoreCase) == 0)
		{
			ParsePostProcess(ChildNode, OutElement->GetPostProcess());
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LOOKAT, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetLookAtActor( *ChildNode->GetAttribute(DATASMITH_ACTORNAME) );
		}
		else if (ChildNode->GetTag().Compare(DATASMITH_LOOKATROLL, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetLookAtAllowRoll( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute(TEXT("enabled")) ) );
		}
	}
}

bool FDatasmithSceneXmlReader::LoadFromFile(const FString & InFilename)
{
	FString FileBuffer;
	if ( !FFileHelper::LoadFileToString( FileBuffer, *InFilename ) )
	{
		return false;
	}

	ProjectPath = FPaths::GetPath(InFilename);

	return LoadFromBuffer( FileBuffer );
}

bool FDatasmithSceneXmlReader::LoadFromBuffer(const FString& XmlBuffer)
{
	XmlFile = MakeUnique< FXmlFile >(XmlBuffer, EConstructMethod::ConstructFromBuffer); // Don't use FXmlFile to load the file for now because it fails to properly convert from UTF-8. FFileHelper::LoadFileToString does the conversion.

	const FXmlNode* SceneNode = XmlFile->GetRootNode();
	if (SceneNode == NULL)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString( TEXT("Invalid Datasmith File") ));
		XmlFile.Reset();
		return false;
	}

	if (SceneNode->GetTag().Compare("DatasmithUnrealScene", ESearchCase::IgnoreCase) != 0)
	{
		FText DialogTitle = FText::FromString( TEXT("Error parsing file") );
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString( SceneNode->GetTag() ), &DialogTitle);
		XmlFile.Reset();
	}

	return true;
}

bool FDatasmithSceneXmlReader::ParseFile(const FString& InFilename, TSharedRef< IDatasmithScene >& OutScene, bool bInAppend)
{
	if ( !LoadFromFile(InFilename) )
	{
		return false;
	}

	return ParseXmlFile(OutScene, bInAppend);
}

bool FDatasmithSceneXmlReader::ParseBuffer(const FString& XmlBuffer, TSharedRef< IDatasmithScene >& OutScene, bool bInAppend)
{
	if ( !LoadFromBuffer(XmlBuffer) )
	{
		return false;
	}

	return ParseXmlFile( OutScene, bInAppend );
}

bool FDatasmithSceneXmlReader::ParseXmlFile(TSharedRef< IDatasmithScene >& OutScene, bool bInAppend)
{
	if (!XmlFile.IsValid())
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDatasmithSceneXmlReader::ParseXmlFile);

	if (bInAppend == false)
	{
		OutScene->Reset();
	}

	OutScene->SetExporterSDKVersion( TEXT("N/A") ); // We're expecting to read the SDK Version from the XML file. If it's not available, put "N/A"

	TMap< FString, TSharedPtr<IDatasmithActorElement> > Actors;

	const TArray<FXmlNode*>& Nodes = XmlFile->GetRootNode()->GetChildrenNodes();

	for (int i = 0; i < Nodes.Num(); i++)
	{
		// HOST
		if (Nodes[i]->GetTag().Compare(DATASMITH_HOSTNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetHost(*Nodes[i]->GetContent());
		}
		// VERSION
		else if (Nodes[i]->GetTag().Compare(DATASMITH_EXPORTERVERSION, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetExporterVersion(*Nodes[i]->GetContent());
		}
		// SDK VERSION
		else if (Nodes[i]->GetTag().Compare(DATASMITH_EXPORTERSDKVERSION, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetExporterSDKVersion(*Nodes[i]->GetContent());
		}
		// APPLICATION INFO
		else if (Nodes[i]->GetTag().Compare(DATASMITH_APPLICATION, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetVendor(*Nodes[i]->GetAttribute(DATASMITH_VENDOR));
			OutScene->SetProductName(*Nodes[i]->GetAttribute(DATASMITH_PRODUCTNAME));
			OutScene->SetProductVersion(*Nodes[i]->GetAttribute(DATASMITH_PRODUCTVERSION));
		}
		// USER INFO
		else if (Nodes[i]->GetTag().Compare(DATASMITH_USER, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetUserID(*Nodes[i]->GetAttribute(DATASMITH_USERID));
			OutScene->SetUserOS(*Nodes[i]->GetAttribute(DATASMITH_USEROS));
		}
		//READ STATIC MESHES
		else if (Nodes[i]->GetTag().Compare(DATASMITH_STATICMESHNAME, ESearchCase::IgnoreCase) == 0)
		{
			FString ElementName = Nodes[i]->GetAttribute(TEXT("name"));
			TSharedPtr< IDatasmithMeshElement > Element = FDatasmithSceneFactory::CreateMesh(*ElementName);

			ParseMesh( Nodes[i], Element );

			OutScene->AddMesh(Element);
		}
		//READ LEVEL SEQUENCES
		else if (Nodes[i]->GetTag().Compare(DATASMITH_LEVELSEQUENCENAME, ESearchCase::IgnoreCase) == 0)
		{
			FString ElementName = Nodes[i]->GetAttribute(TEXT("name"));
			TSharedRef< IDatasmithLevelSequenceElement > Element = FDatasmithSceneFactory::CreateLevelSequence(*ElementName);

			ParseLevelSequence( Nodes[i], Element );

			OutScene->AddLevelSequence(Element);
		}
		//READ TEXTURES
		else if (Nodes[i]->GetTag().Compare(DATASMITH_TEXTURENAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithTextureElement > Element = FDatasmithSceneFactory::CreateTexture(*Nodes[i]->GetAttribute(TEXT("name")));
			ParseTextureElement(Nodes[i], Element);

			FString TextureName(Element->GetName());
			if (FPaths::FileExists(Element->GetFile()))
			{
				int32 TexturesCount = OutScene->GetTexturesCount();
				bool bIsDuplicate = false;
				for (int32 t = 0; t < TexturesCount; t++)
				{
					const TSharedPtr< IDatasmithTextureElement >& TextureElement = OutScene->GetTexture(t);
					if (TextureName == TextureElement->GetName())
					{
						bIsDuplicate = true;
						break;
					}
				}

				if (bIsDuplicate == false)
				{
					OutScene->AddTexture(Element);
				}
			}
		}
		//READ ENVIRONMENTS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_ENVIRONMENTNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithEnvironmentElement > Element = FDatasmithSceneFactory::CreateEnvironment(*Nodes[i]->GetAttribute(TEXT("name")));

			ParseElement( Nodes[i], Element.ToSharedRef() );

			const TArray<FXmlNode*>& EleNodes = Nodes[i]->GetChildrenNodes();
			for (int j = 0; j < EleNodes.Num(); j++)
			{
				if (EleNodes[j]->GetTag().Compare(DATASMITH_TEXTURENAME, ESearchCase::IgnoreCase) == 0)
				{
					FString TextureFile;
					FDatasmithTextureSampler TextureSampler;
					ParseTexture(EleNodes[j], TextureFile, TextureSampler);

					Element->GetEnvironmentComp()->AddSurface( *TextureFile, TextureSampler );
				}
				else if (EleNodes[j]->GetTag().Compare(DATASMITH_ENVILLUMINATIONMAP, ESearchCase::IgnoreCase) == 0)
				{
					Element->SetIsIlluminationMap(FCString::ToBool(*EleNodes[j]->GetAttribute(TEXT("enabled"))));
				}
			}

			if( Element->GetEnvironmentComp()->GetParamSurfacesCount() != 0 && !FString(Element->GetEnvironmentComp()->GetParamTexture(0)).IsEmpty() )
			{
				OutScene->AddActor(Element);
			}
		}
		//READ SKY
		else if (Nodes[i]->GetTag().Compare(DATASMITH_PHYSICALSKYNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetUsePhysicalSky( FCString::ToBool(*Nodes[i]->GetAttribute(TEXT("enabled"))));
		}
		//READ POSTPROCESS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_POSTPRODUCTIONNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithPostProcessElement > PostProcess = FDatasmithSceneFactory::CreatePostProcess();
			ParsePostProcess(Nodes[i], PostProcess);
			OutScene->SetPostProcess(PostProcess);
		}
		//READ MATERIALS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_MATERIALNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithMaterialElement > Material = FDatasmithSceneFactory::CreateMaterial(*Nodes[i]->GetAttribute(TEXT("name")));

			ParseMaterial(Nodes[i], Material);
			OutScene->AddMaterial(Material);
		}
		//READ MASTER MATERIALS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_MASTERMATERIALNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithMasterMaterialElement > MasterMaterial = FDatasmithSceneFactory::CreateMasterMaterial(*Nodes[i]->GetAttribute(TEXT("name")));

			ParseMasterMaterial(Nodes[i], MasterMaterial);
			OutScene->AddMaterial(MasterMaterial);
		}
		//READ UEPBR MATERIALS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_UEPBRMATERIALNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithUEPbrMaterialElement > Material = FDatasmithSceneFactory::CreateUEPbrMaterial(*Nodes[i]->GetAttribute(TEXT("name")));

			ParseUEPbrMaterial(Nodes[i], Material);
			OutScene->AddMaterial(Material);
		}
		//READ METADATA
		else if (Nodes[i]->GetTag().Compare(DATASMITH_METADATANAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithMetaDataElement > MetaData = FDatasmithSceneFactory::CreateMetaData(*Nodes[i]->GetAttribute(TEXT("name")));
			ParseMetaData(Nodes[i], MetaData, OutScene, Actors );
			OutScene->AddMetaData(MetaData);
		}
		//LOD SCREEN SIZES
		else if (Nodes[i]->GetTag().Compare(DATASMITH_LODSCREENSIZE, ESearchCase::IgnoreCase) == 0)
		{
			float LODScreenSize = FCString::Atof( *Nodes[i]->GetAttribute(TEXT("value")) );
			OutScene->AddLODScreenSize(LODScreenSize);
		}
		// EXPORT STATS
		else if (Nodes[i]->GetTag().Compare(DATASMITH_EXPORT, ESearchCase::IgnoreCase) == 0)
		{
			OutScene->SetExportDuration(FCString::Atoi(*Nodes[i]->GetAttribute(DATASMITH_EXPORTDURATION)));
		}
		//READ ACTORS
		else
		{
			TArrayView< const TCHAR* > ActorTagsView( DatasmithSceneXmlReaderImpl::ActorTags );

			for ( const TCHAR* ActorTag : ActorTagsView )
			{
				if ( Nodes[i]->GetTag().Compare( ActorTag, ESearchCase::IgnoreCase ) == 0 )
				{
					TSharedPtr< IDatasmithActorElement > ActorElement;
					ParseActor(Nodes[i], ActorElement, OutScene, Actors );
					if ( ActorElement.IsValid() )
					{
						OutScene->AddActor(ActorElement);
					}

					break;
				}
			}
		}
	}

	PatchUpVersion(OutScene);

	return true;
}

void FDatasmithSceneXmlReader::ParseMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithMaterialElement >& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	const TArray<FXmlNode*>& MaterialNodes = InNode->GetChildrenNodes();
	for (int m = 0; m < MaterialNodes.Num(); m++)
	{
		if (MaterialNodes[m]->GetTag().Compare(DATASMITH_SHADERNAME, ESearchCase::IgnoreCase) == 0)
		{
			TSharedPtr< IDatasmithShaderElement > ShaderElement = FDatasmithSceneFactory::CreateShader(*MaterialNodes[m]->GetAttribute(TEXT("name")));

			const TArray<FXmlNode*>& ShaderNodes = MaterialNodes[m]->GetChildrenNodes();
			for (int j = 0; j < ShaderNodes.Num(); j++)
			{
				FString Texture;
				FDatasmithTextureSampler TextureSampler;
				FLinearColor Color;

				if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DIFFUSETEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetDiffuseTexture(*Texture);
					ShaderElement->SetDiffTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DIFFUSECOLNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseColor(ShaderNodes[j], Color);
					ShaderElement->SetDiffuseColor(Color);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DIFFUSECOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetDiffuseComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_REFLETEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetReflectanceTexture(*Texture);
					ShaderElement->SetRefleTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_REFLECOLNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseColor(ShaderNodes[j], Color);
					ShaderElement->SetReflectanceColor(Color);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_REFLECOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetRefleComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_ROUGHNESSTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetRoughnessTexture(*Texture);
					ShaderElement->SetRoughTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_ROUGHNESSVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetRoughness(fmax(0.02, FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value")))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_ROUGHNESSCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetRoughnessComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_BUMPVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetBumpAmount(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_BUMPTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetBumpTexture(*Texture);
					ShaderElement->SetBumpTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_NORMALTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetNormalTexture(*Texture);
					ShaderElement->SetNormalTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_NORMALCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetNormalComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_TRANSPTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetTransparencyTexture(*Texture);
					ShaderElement->SetTransTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_TRANSPCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetTransComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_CLIPTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetMaskTexture(*Texture);
					ShaderElement->SetMaskTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_CLIPCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetMaskComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_TRANSPCOLNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseColor(ShaderNodes[j], Color);
					ShaderElement->SetTransparencyColor(Color);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_IORVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetIOR(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_IORKVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetIORk(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_REFRAIORVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetIORRefra(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_TWOSIDEDVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetTwoSided(FCString::ToBool(*ShaderNodes[j]->GetAttribute(TEXT("enabled"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DISPLACETEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetDisplaceTexture(*Texture);
					ShaderElement->SetDisplaceTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DISPLACEVALNAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetDisplace(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DISPLACECOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetDisplaceComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DISPLACESUBNAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetDisplaceSubDivision(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_METALTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetMetalTexture(*Texture);
					ShaderElement->SetMetalTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_METALVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetMetal(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_METALCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetMetalComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetEmitTexture(*Texture);
					ShaderElement->SetEmitTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetEmitPower(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetEmitComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITTEMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetEmitTemperature(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITCOLNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseColor(ShaderNodes[j], Color);
					ShaderElement->SetEmitColor(Color);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_EMITONLYVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetLightOnly(FCString::ToBool(*ShaderNodes[j]->GetAttribute(TEXT("enabled"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_WEIGHTTEXNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseTexture(ShaderNodes[j], Texture, TextureSampler);
					ShaderElement->SetWeightTexture(*Texture);
					ShaderElement->SetWeightTextureSampler(TextureSampler);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_WEIGHTCOLNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseColor(ShaderNodes[j], Color);
					ShaderElement->SetWeightColor(Color);
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_WEIGHTCOMPNAME, ESearchCase::IgnoreCase) == 0)
				{
					ParseComp(ShaderNodes[j], ShaderElement->GetWeightComp());
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_WEIGHTVALUENAME, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetWeightValue(FCString::Atod(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_BLENDMODE, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetBlendMode((EDatasmithBlendMode)FCString::Atoi(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_STACKLAYER, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetIsStackedLayer(FCString::ToBool(*ShaderNodes[j]->GetAttribute(TEXT("enabled"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_DYNAMICEMISSIVE, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetUseEmissiveForDynamicAreaLighting(FCString::ToBool(*ShaderNodes[j]->GetAttribute(TEXT("enabled"))));
				}
				else if (ShaderNodes[j]->GetTag().Compare(DATASMITH_SHADERUSAGE, ESearchCase::IgnoreCase) == 0)
				{
					ShaderElement->SetShaderUsage((EDatasmithShaderUsage)FCString::Atoi(*ShaderNodes[j]->GetAttribute(TEXT("value"))));
				}
			}

			if (FCString::Strlen(ShaderElement->GetReflectanceTexture()) == 0 && ShaderElement->GetReflectanceColor().IsAlmostBlack() && ShaderElement->GetRefleComp().IsValid() == false &&
				FCString::Strlen(ShaderElement->GetMetalTexture()) == 0 && ShaderElement->GetMetal() <= 0.0 && ShaderElement->GetMetalComp().IsValid() == false)
			{
				ShaderElement->SetReflectanceColor(FLinearColor(0.07f, 0.07f, 0.07f));
				ShaderElement->SetRoughness(0.7);
			}
			OutElement->AddShader(ShaderElement);
		}
	}
}

void FDatasmithSceneXmlReader::ParseMasterMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithMasterMaterialElement >& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	for ( const FXmlAttribute& Attribute : InNode->GetAttributes() )
	{
		if (Attribute.GetTag().Compare(DATASMITH_MASTERMATERIALTYPE, ESearchCase::IgnoreCase) == 0)
		{
			EDatasmithMasterMaterialType MaterialType = (EDatasmithMasterMaterialType)FMath::Clamp( FCString::Atoi( *Attribute.GetValue() ), 0, (int32)EDatasmithMasterMaterialType::Count - 1 );

			OutElement->SetMaterialType( MaterialType );
		}
		else if (Attribute.GetTag().Compare(DATASMITH_MASTERMATERIALQUALITY, ESearchCase::IgnoreCase) == 0)
		{
			EDatasmithMasterMaterialQuality Quality = (EDatasmithMasterMaterialQuality)FMath::Clamp( FCString::Atoi( *Attribute.GetValue() ), 0, (int32)EDatasmithMasterMaterialQuality::Count - 1 );
			OutElement->SetQuality( Quality );
		}
		else if (Attribute.GetTag().Compare(DATASMITH_MASTERMATERIALPATHNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetCustomMaterialPathName( *Attribute.GetValue() );
		}
	}

	ParseKeyValueProperties( InNode, *OutElement );
}

template< typename ExpressionInputType >
void ParseExpressionInput(const FXmlNode* InNode, TSharedPtr< IDatasmithUEPbrMaterialElement >& OutElement, ExpressionInputType& ExpressionInput)
{
	if ( !InNode )
	{
		return;
	}

	FString ExpressionIndexAttribute = InNode->GetAttribute( TEXT("expression") );

	if ( !ExpressionIndexAttribute.IsEmpty() )
	{
		// Before 4.23 Expressions were serialized as <0 expression="5" OutputIndex="0"/>
		// From 4.23 Expressions are serialized as <Input Name="0" expression="5" OutputIndex="0"/>
		// So if the Name is used and that backward compatibility is desired, the Node Tag can be used instead of the "Name" Attribute.

		int32 ExpressionIndex = FCString::Atoi( *ExpressionIndexAttribute );

		int32 OutputIndex = FCString::Atoi( *InNode->GetAttribute( TEXT("OutputIndex") ) );

		IDatasmithMaterialExpression* Expression = OutElement->GetExpression( ExpressionIndex );

		if ( Expression )
		{
			Expression->ConnectExpression( ExpressionInput, OutputIndex );
		}
	}
}

void FDatasmithSceneXmlReader::ParseUEPbrMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithUEPbrMaterialElement >& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	OutElement->SetParentLabel( *InNode->GetAttribute( DATASMITH_PARENTMATERIALLABEL ) );

	FString ExpressionsTag = TEXT("Expressions");

	FXmlNode* const* ExpressionsNode = Algo::FindByPredicate( InNode->GetChildrenNodes(), [ ExpressionsTag ]( const FXmlNode* Node ) -> bool
	{
		return Node->GetTag() == ExpressionsTag;
	});

	if ( ExpressionsNode )
	{
		// Create all the material expressions
		for ( const FXmlNode* ChildNode : (*ExpressionsNode)->GetChildrenNodes() )
		{
			if ( ChildNode->GetTag() == TEXT("Texture") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::Texture );

				if ( Expression )
				{
					Expression->SetName( *ChildNode->GetAttribute( TEXT("Name") ) );
					IDatasmithMaterialExpressionTexture* TextureExpression = static_cast< IDatasmithMaterialExpressionTexture* >( Expression );
					TextureExpression->SetTexturePathName( *ChildNode->GetAttribute( TEXT("PathName") ) );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("TextureCoordinate") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::TextureCoordinate );

				if ( Expression )
				{
					IDatasmithMaterialExpressionTextureCoordinate* TextureCoordinateExpression = static_cast< IDatasmithMaterialExpressionTextureCoordinate* >( Expression );
					TextureCoordinateExpression->SetCoordinateIndex( DatasmithSceneXmlReaderImpl::ValueFromString< int32 >( ChildNode->GetAttribute( TEXT("Index") ) ) );
					TextureCoordinateExpression->SetUTiling( DatasmithSceneXmlReaderImpl::ValueFromString< float >( ChildNode->GetAttribute( TEXT("UTiling") ) ) );
					TextureCoordinateExpression->SetVTiling( DatasmithSceneXmlReaderImpl::ValueFromString< float >( ChildNode->GetAttribute( TEXT("VTiling") ) ) );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("FlattenNormal") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::FlattenNormal );

				if ( Expression )
				{
					IDatasmithMaterialExpressionFlattenNormal* FlattenNormal = static_cast< IDatasmithMaterialExpressionFlattenNormal* >( Expression );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("Bool") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantBool );

				if ( Expression )
				{
					Expression->SetName( *ChildNode->GetAttribute( TEXT("Name") ) );

					IDatasmithMaterialExpressionBool* ConstantBool = static_cast< IDatasmithMaterialExpressionBool* >( Expression );

					ConstantBool->GetBool() = DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("Constant") ) );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("Color") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantColor );

				if ( Expression )
				{
					Expression->SetName( *ChildNode->GetAttribute( TEXT("Name") ) );

					IDatasmithMaterialExpressionColor* ConstantColor = static_cast< IDatasmithMaterialExpressionColor* >( Expression );

					ConstantColor->GetColor() = DatasmithSceneXmlReaderImpl::ValueFromString< FLinearColor >( ChildNode->GetAttribute( TEXT("Constant") ) );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("Scalar") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantScalar );

				if ( Expression )
				{
					Expression->SetName( *ChildNode->GetAttribute( TEXT("Name") ) );

					IDatasmithMaterialExpressionScalar* ConstantScalar = static_cast< IDatasmithMaterialExpressionScalar* >( Expression );

					ConstantScalar->GetScalar() = DatasmithSceneXmlReaderImpl::ValueFromString< float >( ChildNode->GetAttribute( TEXT("Constant") ) );
				}
			}
			else if ( ChildNode->GetTag() == TEXT("FunctionCall") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::FunctionCall );

				if ( Expression )
				{
					IDatasmithMaterialExpressionFunctionCall* FunctionCall = static_cast< IDatasmithMaterialExpressionFunctionCall* >( Expression );
					FunctionCall->SetFunctionPathName( *ChildNode->GetAttribute( TEXT("Function") ) );
				}
			}
			else
			{
				IDatasmithMaterialExpression* Expression = OutElement->AddMaterialExpression( EDatasmithMaterialExpressionType::Generic );

				if ( Expression )
				{
					IDatasmithMaterialExpressionGeneric* GenericExpression = static_cast< IDatasmithMaterialExpressionGeneric* >( Expression );
					GenericExpression->SetName( *ChildNode->GetAttribute( TEXT("Name") ) );

					GenericExpression->SetExpressionName( *ChildNode->GetTag() );
					ParseKeyValueProperties( ChildNode, *GenericExpression );
				}
			}
		}

		// Connect the material expressions
		int32 ExpressionIndex = 0;
		for ( const FXmlNode* ChildNode : (*ExpressionsNode)->GetChildrenNodes() )
		{
			if ( ChildNode->GetTag() == TEXT("FlattenNormal") )
			{
				IDatasmithMaterialExpression* Expression = OutElement->GetExpression( ExpressionIndex );

				if ( Expression )
				{
					IDatasmithMaterialExpressionFlattenNormal* FlattenNormal = static_cast< IDatasmithMaterialExpressionFlattenNormal* >( Expression );

					{
						FXmlNode* const* NormalNode = Algo::FindByPredicate( ChildNode->GetChildrenNodes(), [ InputName = FlattenNormal->GetNormal().GetInputName() ]( FXmlNode* Node ) -> bool
						{
							return Node->GetTag() == InputName;
						});

						ParseExpressionInput( *NormalNode, OutElement, FlattenNormal->GetNormal() );
					}
				}
			}
			else // Generic
			{
				IDatasmithMaterialExpression* Expression = OutElement->GetExpression( ExpressionIndex );

				if ( Expression )
				{
					IDatasmithMaterialExpressionGeneric* GenericExpression = static_cast< IDatasmithMaterialExpressionGeneric* >( Expression );

					for ( const FXmlNode* InputChildNode : ChildNode->GetChildrenNodes() )
					{
						const FString& NameAttribute = InputChildNode->GetAttribute(TEXT("Name"));
						int32 InputIndex = DatasmithSceneXmlReaderImpl::ValueFromString< int32 >( NameAttribute.IsEmpty() ? InputChildNode->GetTag() : NameAttribute );

						if (IDatasmithExpressionInput* Input = GenericExpression->GetInput( InputIndex ))
						{
							ParseExpressionInput( InputChildNode, OutElement, *Input );
						}
					}
				}
			}

			++ExpressionIndex;
		}
	}

	const TArray<FXmlNode*>& ChildrenNodes = InNode->GetChildrenNodes();

	auto TryConnectMaterialInput = [&ChildrenNodes, &OutElement](IDatasmithExpressionInput& Input)
	{
		const TCHAR* InputName = Input.GetInputName();
		for (FXmlNode* XmlNode : ChildrenNodes)
		{
			if (XmlNode && (XmlNode->GetAttribute(TEXT("Name")) == InputName || XmlNode->GetTag() == InputName ))
			{
				ParseExpressionInput( XmlNode, OutElement, Input );
				return;
			}
		}
	};

	TryConnectMaterialInput(OutElement->GetBaseColor());
	TryConnectMaterialInput(OutElement->GetMetallic());
	TryConnectMaterialInput(OutElement->GetSpecular());
	TryConnectMaterialInput(OutElement->GetRoughness());
	TryConnectMaterialInput(OutElement->GetEmissiveColor());
	TryConnectMaterialInput(OutElement->GetOpacity());
	TryConnectMaterialInput(OutElement->GetNormal());
	TryConnectMaterialInput(OutElement->GetWorldDisplacement());
	TryConnectMaterialInput(OutElement->GetRefraction());
	TryConnectMaterialInput(OutElement->GetAmbientOcclusion());
	TryConnectMaterialInput(OutElement->GetMaterialAttributes());

	for ( const FXmlNode* ChildNode : InNode->GetChildrenNodes() )
	{
		if ( ChildNode->GetTag() == DATASMITH_USEMATERIALATTRIBUTESNAME )
		{
			OutElement->SetUseMaterialAttributes( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("enabled") ) ) );
		}
		else if ( ChildNode->GetTag() == DATASMITH_TWOSIDEDVALUENAME )
		{
			OutElement->SetTwoSided( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("enabled") ) ) );
		}
		else if ( ChildNode->GetTag() == DATASMITH_BLENDMODE )
		{
			OutElement->SetBlendMode( DatasmithSceneXmlReaderImpl::ValueFromString< int >( ChildNode->GetAttribute( TEXT("value") ) ) );
		}
		else if ( ChildNode->GetTag() == DATASMITH_FUNCTIONLYVALUENAME )
		{
			OutElement->SetMaterialFunctionOnly( DatasmithSceneXmlReaderImpl::ValueFromString< bool >( ChildNode->GetAttribute( TEXT("enabled") ) ) );
		}
	}
}

void FDatasmithSceneXmlReader::ParseCustomActor(FXmlNode* InNode, TSharedPtr< IDatasmithCustomActorElement >& OutElement) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	for ( const FXmlAttribute& Attribute : InNode->GetAttributes() )
	{
		if (Attribute.GetTag().Compare(DATASMITH_CUSTOMACTORPATHNAME, ESearchCase::IgnoreCase) == 0)
		{
			OutElement->SetClassOrPathName( *Attribute.GetValue() );
		}
	}

	ParseKeyValueProperties( InNode, *OutElement );
}

namespace MetaDataElementUtils
{
	void FinalizeMetaData(const TSharedRef< IDatasmithScene >& InScene, TSharedPtr<IDatasmithMetaDataElement>& OutElement, const FString& ReferenceString, TMap< FString, TSharedPtr<IDatasmithActorElement> >& Actors)
	{
		// Retrieve the associated element, which is saved as "ReferenceType.ReferenceName"
		FString ReferenceType;
		FString ReferenceName;
		ReferenceString.Split(TEXT("."), &ReferenceType, &ReferenceName);

		TSharedPtr<IDatasmithElement> AssociatedElement;
		if (ReferenceType == TEXT("Actor"))
		{
			TSharedPtr<IDatasmithActorElement> * Actor = Actors.Find(ReferenceName);
			if (Actor != nullptr)
			{
				AssociatedElement = *Actor;
			}
			else
			{
				UE_LOG(LogDatasmith, Warning, TEXT("Missing actor referenced in metadata %s"), *ReferenceName);
			}
		}
		else if (ReferenceType == TEXT("Texture"))
		{
			int32 NumElements = InScene->GetTexturesCount();
			for (int32 i = 0; i < NumElements; ++i)
			{
				const TSharedPtr<IDatasmithElement>& CurrentElement = InScene->GetTexture(i);
				if (FCString::Strcmp(CurrentElement->GetName(), *ReferenceName) == 0)
				{
					AssociatedElement = CurrentElement;
					break;
				}
			}
		}
		else if (ReferenceType == TEXT("Material"))
		{
			int32 NumElements = InScene->GetMaterialsCount();
			for (int32 i = 0; i < NumElements; ++i)
			{
				const TSharedPtr<IDatasmithElement>& CurrentElement = InScene->GetMaterial(i);
				if (FCString::Strcmp(CurrentElement->GetName(), *ReferenceName) == 0)
				{
					AssociatedElement = CurrentElement;
					break;
				}
			}
		}
		else if (ReferenceType == TEXT("StaticMesh"))
		{
			int32 NumElements = InScene->GetMeshesCount();
			for (int32 i = 0; i < NumElements; ++i)
			{
				const TSharedPtr<IDatasmithElement>& CurrentElement = InScene->GetMesh(i);
				if (FCString::Strcmp(CurrentElement->GetName(), *ReferenceName) == 0)
				{
					AssociatedElement = CurrentElement;
					break;
				}
			}
		}
		else
		{
			ensure(false);
		}

		OutElement->SetAssociatedElement(AssociatedElement);
	}
}

void FDatasmithSceneXmlReader::ParseMetaData(FXmlNode* InNode, TSharedPtr< IDatasmithMetaDataElement >& OutElement, const TSharedRef< IDatasmithScene >& InScene, TMap< FString, TSharedPtr<IDatasmithActorElement> >& Actors) const
{
	ParseElement( InNode, OutElement.ToSharedRef() );

	ParseKeyValueProperties(InNode, *OutElement);

	FString ReferenceString = InNode->GetAttribute(DATASMITH_REFERENCENAME);
	MetaDataElementUtils::FinalizeMetaData(InScene, OutElement, ReferenceString, Actors );
}

void FDatasmithSceneXmlReader::ParseLandscape(FXmlNode* InNode, TSharedRef< IDatasmithLandscapeElement >& OutElement) const
{
	ParseElement( InNode, OutElement );

	for ( const FXmlNode* ChildNode : InNode->GetChildrenNodes() )
	{
		if ( ChildNode->GetTag().Compare( DATASMITH_HEIGHTMAPNAME, ESearchCase::IgnoreCase ) == 0 )
		{
			OutElement->SetHeightmap( *ResolveFilePath( ChildNode->GetAttribute( TEXT("value") ) ) );
		}
		else if ( ChildNode->GetTag().Compare( DATASMITH_MATERIAL, ESearchCase::IgnoreCase ) == 0 )
		{
			OutElement->SetMaterial( *ChildNode->GetAttribute( DATASMITH_PATHNAME ) );
		}
	}
}

template< typename ElementType >
void FDatasmithSceneXmlReader::ParseKeyValueProperties(const FXmlNode* InNode, ElementType& OutElement) const
{
	for ( const FXmlNode* PropNode : InNode->GetChildrenNodes() )
	{
		TSharedPtr< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty( *PropNode->GetAttribute( TEXT("name") ) );

		TArrayView< const TCHAR* > EnumStrings( KeyValuePropertyTypeStrings );
		int32 IndexOfEnumValue = EnumStrings.IndexOfByPredicate( [ TypeString = PropNode->GetAttribute(TEXT("type")) ]( const TCHAR* Value )
		{
			return TypeString == Value;
		} );

		if ( IndexOfEnumValue != INDEX_NONE )
		{
			KeyValueProperty->SetPropertyType( (EDatasmithKeyValuePropertyType)IndexOfEnumValue );

			KeyValueProperty->SetValue( *PropNode->GetAttribute(TEXT("val")) );
			OutElement.AddProperty( KeyValueProperty );
		}
	}
}

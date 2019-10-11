// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithSceneFactory.h"

#include "DatasmithAnimationElementsImpl.h"
#include "DatasmithMaterialElementsImpl.h"
#include "DatasmithSceneElementsImpl.h"
#include "DatasmithVariantElementsImpl.h"

TSharedPtr< IDatasmithElement > FDatasmithSceneFactory::CreateElement( EDatasmithElementType InType, const TCHAR* InName )
{
	switch ( InType )
	{
	// Abstract types
	case EDatasmithElementType::None:
	case EDatasmithElementType::Light:
		ensure( false );
		break;
	case EDatasmithElementType::Actor:
		return CreateActor( InName );
	case EDatasmithElementType::StaticMesh:
		return CreateMesh( InName );
	case EDatasmithElementType::StaticMeshActor:
		return CreateMeshActor( InName );
	case EDatasmithElementType::PointLight:
		return CreatePointLight( InName );
	case EDatasmithElementType::SpotLight:
		return CreateSpotLight( InName );
	case EDatasmithElementType::DirectionalLight:
		return CreateDirectionalLight( InName );
	case EDatasmithElementType::AreaLight:
		return CreateAreaLight( InName );
	case EDatasmithElementType::LightmassPortal:
		return CreateLightmassPortal( InName );
	case EDatasmithElementType::EnvironmentLight:
		return CreateEnvironment( InName );
	case EDatasmithElementType::Camera:
		return CreateCameraActor( InName );
	case EDatasmithElementType::Shader:
		return CreateShader( InName );
	case EDatasmithElementType::Material:
		return CreateMaterial( InName );
	case EDatasmithElementType::MasterMaterial:
		return CreateMasterMaterial( InName );
	case EDatasmithElementType::KeyValueProperty:
		return CreateKeyValueProperty( InName );
	case EDatasmithElementType::Texture:
		return CreateTexture( InName );
	case EDatasmithElementType::MaterialId:
		return CreateMaterialId( InName );
	case EDatasmithElementType::PostProcess:
		return CreatePostProcess();
	case EDatasmithElementType::Scene:
		return CreateScene( InName );
	case EDatasmithElementType::MetaData:
		return CreateMetaData( InName );
	case EDatasmithElementType::CustomActor:
		return CreateCustomActor( InName );
	case EDatasmithElementType::HierarchicalInstanceStaticMesh:
		return CreateHierarchicalInstanceStaticMeshActor( InName );
	default:
		ensure( false );
		break;
	}

	return TSharedPtr< IDatasmithElement >();
}

TSharedRef< IDatasmithActorElement > FDatasmithSceneFactory::CreateActor( const TCHAR* InName )
{
	return MakeShared< FDatasmithActorElementImpl< IDatasmithActorElement > >( InName, EDatasmithElementType::None );
}

TSharedRef< IDatasmithCameraActorElement > FDatasmithSceneFactory::CreateCameraActor( const TCHAR* InName )
{
	return MakeShared< FDatasmithCameraActorElementImpl >( InName );
}

TSharedRef< IDatasmithCompositeTexture > FDatasmithSceneFactory::CreateCompositeTexture()
{
	return MakeShared< FDatasmithCompositeTextureImpl >();
}

TSharedRef< IDatasmithCustomActorElement > FDatasmithSceneFactory::CreateCustomActor( const TCHAR* InName )
{
	return MakeShared< FDatasmithCustomActorElementImpl >( InName );
}

TSharedRef< IDatasmithLandscapeElement > FDatasmithSceneFactory::CreateLandscape( const TCHAR* InName )
{
	return MakeShared< FDatasmithLandscapeElementImpl >( InName );
}

TSharedRef< IDatasmithPostProcessVolumeElement > FDatasmithSceneFactory::CreatePostProcessVolume( const TCHAR* InName )
{
	return MakeShared< FDatasmithPostProcessVolumeElementImpl >( InName );
}

TSharedRef< IDatasmithEnvironmentElement > FDatasmithSceneFactory::CreateEnvironment( const TCHAR* InName )
{
	return MakeShared< FDatasmithEnvironmentElementImpl >( InName );
}

TSharedRef< IDatasmithPointLightElement > FDatasmithSceneFactory::CreatePointLight( const TCHAR* InName )
{
	return MakeShared< FDatasmithPointLightElementImpl<> >( InName );
}

TSharedRef< IDatasmithSpotLightElement > FDatasmithSceneFactory::CreateSpotLight( const TCHAR* InName )
{
	return MakeShared< FDatasmithSpotLightElementImpl<> >( InName );
}

TSharedRef< IDatasmithDirectionalLightElement > FDatasmithSceneFactory::CreateDirectionalLight( const TCHAR* InName )
{
	return MakeShared< FDatasmithDirectionalLightElementImpl >( InName );
}

TSharedRef< IDatasmithAreaLightElement > FDatasmithSceneFactory::CreateAreaLight( const TCHAR* InName )
{
	return MakeShared< FDatasmithAreaLightElementImpl >( InName );
}

TSharedRef< IDatasmithLightmassPortalElement > FDatasmithSceneFactory::CreateLightmassPortal( const TCHAR* InName )
{
	return MakeShared< FDatasmithLightmassPortalElementImpl >( InName );
}

TSharedRef< IDatasmithKeyValueProperty > FDatasmithSceneFactory::CreateKeyValueProperty( const TCHAR* InName )
{
	return MakeShared< FDatasmithKeyValuePropertyImpl >( InName );
}

TSharedRef< IDatasmithMeshElement > FDatasmithSceneFactory::CreateMesh( const TCHAR* InName )
{
	return MakeShared< FDatasmithMeshElementImpl >( InName );
}

TSharedRef< IDatasmithMeshActorElement > FDatasmithSceneFactory::CreateMeshActor( const TCHAR* InName )
{
	return MakeShared< FDatasmithMeshActorElementImpl<> >( InName );
}

TSharedRef< IDatasmithHierarchicalInstancedStaticMeshActorElement > FDatasmithSceneFactory::CreateHierarchicalInstanceStaticMeshActor(const TCHAR* InName)
{
	return MakeShared< FDatasmithHierarchicalInstancedStaticMeshActorElementImpl >( InName );
}

TSharedRef< IDatasmithMaterialElement > FDatasmithSceneFactory::CreateMaterial( const TCHAR* InName )
{
	return MakeShared< FDatasmithMaterialElementImpl >( InName );
}

TSharedRef< IDatasmithMasterMaterialElement > FDatasmithSceneFactory::CreateMasterMaterial( const TCHAR* InName )
{
	return MakeShared< FDatasmithMasterMaterialElementImpl >( InName );
}

TSharedRef< IDatasmithUEPbrMaterialElement > FDatasmithSceneFactory::CreateUEPbrMaterial( const TCHAR* InName )
{
	return MakeShared< FDatasmithUEPbrMaterialElementImpl >( InName );
}

TSharedRef< IDatasmithMetaDataElement > FDatasmithSceneFactory::CreateMetaData( const TCHAR* InName )
{
	return MakeShared< FDatasmithMetaDataElementImpl >( InName );
}

TSharedRef< IDatasmithMaterialIDElement > FDatasmithSceneFactory::CreateMaterialId( const TCHAR* InName )
{
	return MakeShared< FDatasmithMaterialIDElementImpl >( InName );
}

TSharedRef< IDatasmithPostProcessElement > FDatasmithSceneFactory::CreatePostProcess()
{
	return MakeShared< FDatasmithPostProcessElementImpl >();
}

TSharedRef< IDatasmithShaderElement > FDatasmithSceneFactory::CreateShader( const TCHAR* InName )
{
	return MakeShared< FDatasmithShaderElementImpl >( InName );
}

TSharedRef< IDatasmithTextureElement > FDatasmithSceneFactory::CreateTexture( const TCHAR* InName )
{
	return MakeShared< FDatasmithTextureElementImpl >( InName );
}

TSharedRef< IDatasmithLevelSequenceElement > FDatasmithSceneFactory::CreateLevelSequence( const TCHAR* InName )
{
	return MakeShared< FDatasmithLevelSequenceElementImpl >( InName );
}

TSharedRef< IDatasmithTransformAnimationElement > FDatasmithSceneFactory::CreateTransformAnimation( const TCHAR* InName )
{
	return MakeShared< FDatasmithTransformAnimationElementImpl >( InName );
}

TSharedRef< IDatasmithVisibilityAnimationElement > FDatasmithSceneFactory::CreateVisibilityAnimation( const TCHAR* InName )
{
	return MakeShared< FDatasmithVisibilityAnimationElementImpl >( InName );
}

TSharedRef< IDatasmithSubsequenceAnimationElement > FDatasmithSceneFactory::CreateSubsequenceAnimation( const TCHAR* InName )
{
	return MakeShared< FDatasmithSubsequenceAnimationElementImpl >( InName );
}

TSharedRef< IDatasmithLevelVariantSetsElement > FDatasmithSceneFactory::CreateLevelVariantSets(const TCHAR* InName)
{
	return MakeShared< FDatasmithLevelVariantSetsElementImpl >( InName );
}

TSharedRef< IDatasmithVariantSetElement > FDatasmithSceneFactory::CreateVariantSet(const TCHAR* InName)
{
	return MakeShared< FDatasmithVariantSetElementImpl >( InName );
}

TSharedRef< IDatasmithVariantElement > FDatasmithSceneFactory::CreateVariant(const TCHAR* InName)
{
	return MakeShared< FDatasmithVariantElementImpl >( InName );
}

TSharedRef< IDatasmithActorBindingElement > FDatasmithSceneFactory::CreateActorBinding()
{
	return MakeShared< FDatasmithActorBindingElementImpl >();
}

TSharedRef< IDatasmithPropertyCaptureElement > FDatasmithSceneFactory::CreatePropertyCapture()
{
	return MakeShared< FDatasmithPropertyCaptureElementImpl >();
}

TSharedRef< IDatasmithObjectPropertyCaptureElement > FDatasmithSceneFactory::CreateObjectPropertyCapture()
{
	return MakeShared< FDatasmithObjectPropertyCaptureElementImpl >();
}

TSharedRef< IDatasmithScene > FDatasmithSceneFactory::CreateScene( const TCHAR* InName )
{
	return MakeShared< FDatasmithSceneImpl >( InName );
}

TSharedRef< IDatasmithScene > FDatasmithSceneFactory::DuplicateScene( const TSharedRef< IDatasmithScene >& InScene )
{
	return MakeShared< FDatasmithSceneImpl >( StaticCastSharedRef< FDatasmithSceneImpl >( InScene ).Get() );
}

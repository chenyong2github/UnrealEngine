// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithAnimationElements.h"
#include "DatasmithMaterialElements.h"
#include "DatasmithVariantElements.h"
#include "IDatasmithSceneElements.h"
#include "Templates/SharedPointer.h"

/**
 * Factory to create the scene elements used for the export and import process.
 * The shared pointer returned is the only one existing at that time.
 * Make sure to hang onto it until the scene element isn't needed anymore.
 */
class DATASMITHCORE_API FDatasmithSceneFactory
{
public:
	static TSharedPtr< IDatasmithElement > CreateElement( EDatasmithElementType InType, const TCHAR* InName );

	static TSharedRef< IDatasmithActorElement > CreateActor( const TCHAR* InName );

	static TSharedRef< IDatasmithCameraActorElement > CreateCameraActor( const TCHAR* InName );

	static TSharedRef< IDatasmithCompositeTexture > CreateCompositeTexture();

	static TSharedRef< IDatasmithCustomActorElement > CreateCustomActor( const TCHAR* InName );

	static TSharedRef< IDatasmithLandscapeElement > CreateLandscape( const TCHAR* InName );

	static TSharedRef< IDatasmithPostProcessVolumeElement > CreatePostProcessVolume( const TCHAR* InName );

	static TSharedRef< IDatasmithEnvironmentElement > CreateEnvironment( const TCHAR* InName );

	static TSharedRef< IDatasmithPointLightElement > CreatePointLight( const TCHAR* InName );
	static TSharedRef< IDatasmithSpotLightElement > CreateSpotLight( const TCHAR* InName );
	static TSharedRef< IDatasmithDirectionalLightElement > CreateDirectionalLight( const TCHAR* InName );
	static TSharedRef< IDatasmithAreaLightElement > CreateAreaLight( const TCHAR* InName );
	static TSharedRef< IDatasmithLightmassPortalElement > CreateLightmassPortal( const TCHAR* InName );

	static TSharedRef< IDatasmithKeyValueProperty > CreateKeyValueProperty( const TCHAR* InName );

	static TSharedRef< IDatasmithMeshElement > CreateMesh( const TCHAR* InName );

	static TSharedRef< IDatasmithMeshActorElement > CreateMeshActor( const TCHAR* InName );

	static TSharedRef< IDatasmithHierarchicalInstancedStaticMeshActorElement > CreateHierarchicalInstanceStaticMeshActor( const TCHAR* InName );

	static TSharedRef< IDatasmithMaterialElement > CreateMaterial( const TCHAR* InName );

	static TSharedRef< IDatasmithMasterMaterialElement > CreateMasterMaterial( const TCHAR* InName );

	static TSharedRef< IDatasmithUEPbrMaterialElement > CreateUEPbrMaterial( const TCHAR* InName );

	static TSharedRef< IDatasmithMetaDataElement > CreateMetaData( const TCHAR* InName );

	static TSharedRef< IDatasmithMaterialIDElement > CreateMaterialId( const TCHAR* InName );

	static TSharedRef< IDatasmithPostProcessElement > CreatePostProcess();

	static TSharedRef< IDatasmithShaderElement > CreateShader( const TCHAR* InName );

	static TSharedRef< IDatasmithTextureElement > CreateTexture( const TCHAR* InName );

	static TSharedRef< IDatasmithLevelSequenceElement > CreateLevelSequence( const TCHAR* InName );
	static TSharedRef< IDatasmithTransformAnimationElement > CreateTransformAnimation( const TCHAR* InName );
	static TSharedRef< IDatasmithVisibilityAnimationElement > CreateVisibilityAnimation( const TCHAR* InName );
	static TSharedRef< IDatasmithSubsequenceAnimationElement > CreateSubsequenceAnimation( const TCHAR* InName );

	static TSharedRef< IDatasmithLevelVariantSetsElement > CreateLevelVariantSets( const TCHAR* InName );
	static TSharedRef< IDatasmithVariantSetElement > CreateVariantSet( const TCHAR* InName );
	static TSharedRef< IDatasmithVariantElement > CreateVariant( const TCHAR* InName );
	static TSharedRef< IDatasmithActorBindingElement > CreateActorBinding();
	static TSharedRef< IDatasmithPropertyCaptureElement > CreatePropertyCapture();
	static TSharedRef< IDatasmithObjectPropertyCaptureElement > CreateObjectPropertyCapture();

	static TSharedRef< IDatasmithScene > CreateScene( const TCHAR* InName );
	static TSharedRef< IDatasmithScene > DuplicateScene( const TSharedRef< IDatasmithScene >& InScene );
};

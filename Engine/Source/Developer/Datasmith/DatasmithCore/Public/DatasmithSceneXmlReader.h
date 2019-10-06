// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithMaterialElements.h"
#include "IDatasmithSceneElements.h"

class FXmlFile;
class FXmlNode;
class UTexture;

class DATASMITHCORE_API FDatasmithSceneXmlReader
{
public:
	// Force non-inline constructor and destructor to prevent instantiating TUniquePtr< FXmlFile > with an incomplete FXmlFile type (forward declared)
	FDatasmithSceneXmlReader();
	~FDatasmithSceneXmlReader();

	bool ParseFile(const FString& InFilename, TSharedRef< IDatasmithScene >& OutScene, bool bInAppend = false);
	bool ParseBuffer(const FString& XmlBuffer, TSharedRef< IDatasmithScene >& OutScene, bool bInAppend = false);

private:
	bool ParseXmlFile(TSharedRef< IDatasmithScene >& OutScene, bool bInAppend = false);

	void PatchUpVersion(TSharedRef< IDatasmithScene >& OutScene);

	void ParseElement(FXmlNode* InNode, TSharedRef<IDatasmithElement> OutElement) const;
	void ParseLevelSequence(FXmlNode* InNode, const TSharedRef<IDatasmithLevelSequenceElement>& OutElement) const;
	void ParseMesh(FXmlNode* InNode, TSharedPtr<IDatasmithMeshElement>& OutElement) const;
	void ParseTextureElement(FXmlNode* InNode, TSharedPtr<IDatasmithTextureElement>& OutElement) const;
	void ParseTexture(FXmlNode* InNode, FString& OutTextureFilename, FDatasmithTextureSampler& OutTextureUV) const;
	void ParseTransform(FXmlNode* InNode, TSharedPtr< IDatasmithActorElement >& OutElement) const;
	FTransform ParseTransform(FXmlNode* InNode) const;
	void ParseActor(FXmlNode* InNode, TSharedPtr<IDatasmithActorElement>& InOutElement, TSharedRef< IDatasmithScene > Scene, TMap< FString, TSharedPtr<IDatasmithActorElement> >& Actors) const;
	void ParseMeshActor(FXmlNode* InNode, TSharedPtr<IDatasmithMeshActorElement>& OutElement, TSharedRef< IDatasmithScene > Scene) const;
	void ParseHierarchicalInstancedStaticMeshActor(FXmlNode* InNode, TSharedPtr<IDatasmithHierarchicalInstancedStaticMeshActorElement>& OutElement, TSharedRef< IDatasmithScene > Scene) const;
	void ParseLight(FXmlNode* InNode, TSharedPtr<IDatasmithLightActorElement>& OutElement) const;
	void ParseCamera(FXmlNode* InNode, TSharedPtr<IDatasmithCameraActorElement>& OutElement) const;
	void ParsePostProcess(FXmlNode* InNode, const TSharedPtr< IDatasmithPostProcessElement >& Element) const;
	void ParsePostProcessVolume(FXmlNode* InNode, const TSharedRef< IDatasmithPostProcessVolumeElement >& Element) const;
	void ParseColor(FXmlNode* InNode, FLinearColor& OutColor) const;
	void ParseComp(FXmlNode* InNode, TSharedPtr< IDatasmithCompositeTexture >& OutCompTexture, bool bInIsNormal = false) const;
	void ParseMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithMaterialElement >& OutElement) const;
	void ParseMasterMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithMasterMaterialElement >& OutElement) const;
	void ParseUEPbrMaterial(FXmlNode* InNode, TSharedPtr< IDatasmithUEPbrMaterialElement >& OutElement) const;
	void ParseCustomActor(FXmlNode* InNode, TSharedPtr< IDatasmithCustomActorElement >& OutElement) const;
	void ParseMetaData(FXmlNode* InNode, TSharedPtr< IDatasmithMetaDataElement >& OutElement, const TSharedRef< IDatasmithScene >& InScene, TMap< FString, TSharedPtr<IDatasmithActorElement> >& Actors) const;
	void ParseLandscape(FXmlNode* InNode, TSharedRef< IDatasmithLandscapeElement >& OutElement) const;

	template< typename ElementType >
	void ParseKeyValueProperties(const FXmlNode* InNode, ElementType& OutElement) const;

	bool LoadFromFile(const FString& InFilename);
	bool LoadFromBuffer(const FString& XmlBuffer);

	FString ResolveFilePath(const FString& InAssetFile) const;

	FQuat QuatFromHexString(const FString& HexString) const;

	TUniquePtr< FXmlFile > XmlFile;
	FString ProjectPath;
};
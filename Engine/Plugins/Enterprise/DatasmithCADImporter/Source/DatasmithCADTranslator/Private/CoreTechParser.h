// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if !defined(USE_CORETECH_MT_PARSER) && defined(CAD_LIBRARY)

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "CTSession.h"
#include "DatasmithImportOptions.h"

using namespace CADLibrary;

class IDatasmithActorElement;
class IDatasmithMeshElement;
class IDatasmithScene;
class FDatasmithSceneSource;
class IDatasmithMaterialIDElement;
class IDatasmithUEPbrMaterialElement;
struct FMeshDescription;

class FImportDestination
{
public:
	FImportDestination()
		: ActorElement(nullptr)
		, Translation(0.f, 0.f, 0.f)
		, Scale(1.f, 1.f, 1.f)
		, Rotation(FQuat::Identity)
		, Parent(nullptr)
		, bIsAMeshActor(false)
	{};

	FImportDestination(TSharedPtr< FImportDestination > InParent, bool bInMeshActor = false)
		: FImportDestination()
	{
		Parent = InParent;
		bIsAMeshActor = bInMeshActor;
	};

	TSharedPtr< IDatasmithActorElement > GetActorElement()
	{
		return ActorElement;
	}

	TMap<FString, FString>& GetAttributMap(bool bIsInstance)
	{
		return bIsInstance ? InstanceNodeAttributeSetMap : ReferenceNodeAttributeSetMap;
	}

	void SetTranslation(FVector InTranslation)
	{
		Translation = InTranslation;
	}
	void SetScale(FVector InScale)
	{
		Scale = InScale;
	}
	void SetRotation(FQuat InRotation)
	{
		Rotation = InRotation;
	}

	bool IsValidActor();

	void SetNodeParameterFromAttribute(bool bIsBody = false);

	void AddMetaData(TSharedRef<IDatasmithScene> DatasmithScene, const FDatasmithSceneSource& Source);

	const FString& GetExternalDefinition()
	{
		return ExternalDefinition;
	}

	const void SetExternalDefinition(const TCHAR* ExtDefinition)
	{
		ExternalDefinition = ExtDefinition;
	}
		
	const FString& GetReferenceName()
	{
		return ReferenceName;
	}

	const FString& GetLabel()
	{
		return ReferenceInstanceName;
	}

	void SetActor(TSharedPtr< IDatasmithActorElement > Actor)
	{
		ActorElement = Actor;
	}

	TSharedPtr< IDatasmithActorElement > GetActor()
	{
		return ActorElement;
	}

	TSharedPtr< FImportDestination > GetParent()
	{
		return Parent;
	}

	void AddActorTransform();

	bool IsRootNodeOfAFile();

	/**
	 * Return the UUID as a string
	 * UUID of a component is based on all ancestor and self name
	 * UUID of a meshActor is based on all ancestor and self name in the scope of its containing file
	 * @return UUID as a string
	 */
	const FString& GetUEUUID()
	{
		return UEUUIDStr;
	}
	const uint32& GetUUID()
	{
		return UEUUID;
	}

	const uint32& GetMeshUUID()
	{
		return MeshUEUUID;
	}

private:
	/**
	 * Mesh Actor UUID of a mesh actor as to be unique in the scope of its containing file
	 * UUID is based on :
	 * - file name
	 * - ancestor reference(instance) names
	 * - body name
	 */
	void BuildMeshActorUUID();

private:
	TSharedPtr< IDatasmithActorElement > ActorElement;

	FVector Translation;
	FVector Scale;
	FQuat Rotation;

	TMap<FString, FString> ReferenceNodeAttributeSetMap;
	TMap<FString, FString> InstanceNodeAttributeSetMap;

	FString ReferenceName;
	FString ReferenceInstanceName;

	FString ExternalDefinition;

	TSharedPtr< FImportDestination > Parent;

	bool bIsAMeshActor;

	FString UEUUIDStr;
	uint32  UEUUID;
	uint32  MeshUEUUID;
};

class FCoreTechParser : public CTSession
{
public:
	FCoreTechParser(TSharedRef<IDatasmithScene> DatasmithScene, const FDatasmithSceneSource& InSource, CT_DOUBLE Unit, CT_DOUBLE Tolerance);
	
	virtual CheckedCTError Read();
	virtual void UnloadScene();

	void SetTessellationOptions(const FDatasmithTessellationOptions& Options);
	void SetOutputPath(const FString& Path) { OutputPath = Path; }

	virtual TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> MeshElement, FMeshParameters& MeshParameters);

protected:
	CheckedCTError ReadNode(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent);
	CheckedCTError ReadInstance(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent);
	CheckedCTError ReadComponent (CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent);
	CheckedCTError ReadBody(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent);

	CheckedCTError ReadObjectTransform(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent);
	CheckedCTError ReadNodeAttributes(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent, bool bIsInstance);

	virtual CheckedCTError CreateActor(TSharedRef<FImportDestination> AssemblyNode);
	virtual void LinkActor(TSharedRef<FImportDestination> ActorNode);
	virtual CheckedCTError CreateMeshActor(TSharedRef<FImportDestination> ActorNode);
	virtual TSharedPtr< IDatasmithMeshElement > FindOrAddMeshElement(TSharedRef<FImportDestination> BodyNode, CT_OBJECT_ID BodyId);

	TSharedPtr< IDatasmithUEPbrMaterialElement > GetDefaultMaterial();
	TSharedPtr<IDatasmithMaterialIDElement> FindOrAddMaterial(uint32 MaterialID);

	CT_FLAGS SetCoreTechImportOption();

private:
	void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

protected:
	const FDatasmithSceneSource& Source;
	FString SourceFullPath; // Source give relative path 
	FString MainFileExt;
	TSharedRef<IDatasmithScene> DatasmithScene;

	FString OutputPath;
	uint32 TessellationOptionsHash;
	FMeshParameters MeshParameters;

	/** Datasmith mesh elements to OpenModel objects */
	TMap< TSharedPtr< IDatasmithMeshElement >, CT_OBJECT_ID > MeshElementToCTBodyMap;

	/** Map of materials associated with CT material identifier */
	TMap< uint32, TSharedPtr< IDatasmithUEPbrMaterialElement > > MaterialMap;

	TSharedPtr< IDatasmithUEPbrMaterialElement > DefaultMaterial;


	/** Table of correspondence between mesh identifier and associated Datasmith mesh element */
	TMap< uint32, TSharedPtr< IDatasmithMeshElement > > BodyUUIDToMeshElementMap;

};


#endif 
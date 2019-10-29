// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithDefinitions.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Math/Vector.h"

class FDatasmithMesh;
struct FMeshDescription;
struct FRawMesh;

class DATASMITHCORE_API FDatasmithUtils
{
public:
	static void SanitizeNameInplace(FString& InString);
	static FString SanitizeName(FString InString);
	static FString SanitizeObjectName(FString InString);
	static FString SanitizeFileName(FString InString);

	static int32 GetEnterpriseVersionAsInt();
	static FString GetEnterpriseVersionAsString();

	/** Returns the Datasmith data format version */
	static float GetDatasmithFormatVersionAsFloat();
	static int32 GetDatasmithFormatVersionAsInt();
	static FString GetDatasmithFormatVersionAsString();

	/** Returns the file extension without the dot, of the DatasmithScene. Currently udatasmith */
	static const TCHAR* GetFileExtension();

	/** Returns the long name of Datasmith */
	static const TCHAR* GetLongAppName();
	/** Returns the abbreviated name of Datasmith */
	static const TCHAR* GetShortAppName();

	/** Computes the area of a triangle */
	static double AreaTriangle3D(FVector v0, FVector v1, FVector v2);

	enum class EModelCoordSystem : uint8
	{
		ZUp_LeftHanded,
		ZUp_RightHanded,
		YUp_LeftHanded,
		YUp_RightHanded,
	};

	template<typename VecType>
	static FVector ConvertVector(EModelCoordSystem ModelCoordSys, const VecType& V)
	{
		switch (ModelCoordSys)
		{
		case EModelCoordSystem::YUp_LeftHanded:
			return FVector(V[2], V[0], V[1]);

		case EModelCoordSystem::YUp_RightHanded:
			return FVector(-V[2], V[0], V[1]);

		case EModelCoordSystem::ZUp_RightHanded:
			return FVector(-V[0], V[1], V[2]);

		case EModelCoordSystem::ZUp_LeftHanded:
		default:
			return FVector(V[0], V[1], V[2]);
		}
	}

	FTransform static ConvertTransform(EModelCoordSystem SourceCoordSystem, const FTransform& LocalTransform);

	FMatrix static GetSymmetricMatrix(const FVector& Origin, const FVector& Normal);
};

class DATASMITHCORE_API FDatasmithMeshUtils
{
public:
	/**
	 * @param bValidateRawMesh this boolean indicate if the raw must be valid.
	 * For example a collision mesh don't need to have a valid raw mesh
	 */
	static bool ToRawMesh(const FDatasmithMesh& Mesh, FRawMesh& RawMesh, bool bValidateRawMesh = true);
	static bool ToMeshDescription(FDatasmithMesh& DsMesh, FMeshDescription& MeshDescription);


	/**
	 * Validates that the given UV Channel does not contain a degenerated triangle.
	 *
	 * @param DSMesh	The DatasmithMesh to validate.
	 * @param Channel	The UV channel to validate, starting at 0
	 */
	static bool IsUVChannelValid(const FDatasmithMesh& DsMesh, const int32 Channel);
};

enum class EDSTextureUtilsError : int32
{
	NoError = 0,
	FileNotFound = -1,
	InvalidFileType = -2,
	FileReadIssue = -3,
	InvalidData = -4,
	FreeImageNotFound = -5,
	FileNotSaved = -6,
	ResizeFailed = -7,
};

/**
 * NoResize: Keep original size
 * NearestPowerOfTwo: resizes to the nearest power of two value (recommended)
 * PreviousPowerOfTwo: it decreases the value to the previous power of two
 * NextPowerOfTwo: it increases the value to the next power of two
 */
enum class EDSResizeTextureMode
{
	NoResize,
	NearestPowerOfTwo,
	PreviousPowerOfTwo,
	NextPowerOfTwo
};

class DATASMITHCORE_API FDatasmithTextureUtils
{
public:
	static bool CalculateTextureHash(const TSharedPtr<class IDatasmithTextureElement>& TextureElement);
	static void CalculateTextureHashes(const TSharedPtr<class IDatasmithScene>& Scene);
};

/**
 * Enum mainly used to describe which components of a transform animation are enabled. Should mostly be
 * used with FDatasmithAnimationUtils::GetChannelTypeComponents
 */
enum class ETransformChannelComponents : uint8
{
	None = 0x00,
	X	 = 0x01,
	Y    = 0x02,
	Z    = 0x04,
	All  = X | Y | Z,
};
ENUM_CLASS_FLAGS(ETransformChannelComponents);

class DATASMITHCORE_API FDatasmithAnimationUtils
{
public:
	/** Utility to help handling the components of a channel independently of the transform type **/
	static ETransformChannelComponents GetChannelTypeComponents(EDatasmithTransformChannels Channels, EDatasmithTransformType TransformType);

	/** Utility to help assembling a transform type's components into a EDatasmithTransformChannels enum **/
	static EDatasmithTransformChannels SetChannelTypeComponents(ETransformChannelComponents Components, EDatasmithTransformType TransformType);
};

class DATASMITHCORE_API FDatasmithSceneUtils
{
public:
	static TArray<TSharedPtr<class IDatasmithCameraActorElement>> GetAllCameraActorsFromScene(const TSharedPtr<class IDatasmithScene>& Scene);
	static TArray<TSharedPtr<class IDatasmithLightActorElement>> GetAllLightActorsFromScene(const TSharedPtr<class IDatasmithScene>& Scene);
	static TArray<TSharedPtr<class IDatasmithMeshActorElement>> GetAllMeshActorsFromScene(const TSharedPtr<class IDatasmithScene>& Scene);
	static TArray<TSharedPtr<class IDatasmithCustomActorElement>> GetAllCustomActorsFromScene(const TSharedPtr<class IDatasmithScene>& Scene);

	using TActorHierarchy = TArray<TSharedPtr<class IDatasmithActorElement>, TInlineAllocator<8>>;
	static bool FindActorHierarchy(const IDatasmithScene* Scene, const TSharedPtr<IDatasmithActorElement>& ToFind, TActorHierarchy& OutHierarchy);

	static bool IsMaterialIDUsedInScene(const TSharedPtr<class IDatasmithScene>& Scene, const TSharedPtr<class IDatasmithMaterialIDElement>& MaterialElement);
	static bool IsPostProcessUsedInScene(const TSharedPtr<class IDatasmithScene>& Scene, const TSharedPtr<class IDatasmithPostProcessElement>& PostProcessElement);
};

/**
 * Based on a table of frequently used names, this class generates unique names
 * with a good complexity when the number of name is important.
 * @note: This abstact class allows various implementation of the cache of known name.
 * Implementation could use a simple TSet, or reuse existing specific cache structure
 */
class DATASMITHCORE_API FDatasmithUniqueNameProviderBase
{
public:
	virtual ~FDatasmithUniqueNameProviderBase() = default;

	/**
	 * Generates a unique name
	 * @param BaseName Name that will be suffixed with an index to be unique
	 * @return FString unique name. Calling "Contains()" with this name will be false
	 */
	FString GenerateUniqueName(const FString& BaseName);

	/**
	 * Register a name as known
	 * @param Name name to register
	 */
	virtual void AddExistingName(const FString& Name) = 0;

	/**
	 * Remove a name from the list of existing name
	 * @param Name name to unregister
	 */
	virtual void RemoveExistingName(const FString& Name) = 0;

protected:

	/**
	 * Check if the given name is already registered
	 *
	 * @param Name name to test
	 * @return true if the name is in the cache
	 */
	virtual bool Contains(const FString& Name) = 0;

private:
	TMap<FString, int32> FrequentlyUsedNames;
};

/**
 * Name provider with internal cache implemented with a simple TSet
 */
class DATASMITHCORE_API FDatasmithUniqueNameProvider : public FDatasmithUniqueNameProviderBase
{
public:
	void Reserve( int32 NumberOfName ) { KnownNames.Reserve(NumberOfName); }

	virtual void AddExistingName(const FString& Name) override { KnownNames.Add(Name); }
	virtual void RemoveExistingName(const FString& Name) override { KnownNames.Remove(Name); }

protected:
	virtual bool Contains(const FString& Name) override { return KnownNames.Contains(Name); }
	void Clear() { KnownNames.Empty(); }

private:
	TSet<FString> KnownNames;
};



// Copyright Epic Games, Inc. All Rights Reserved.
#include "DatasmithUtils.h"

#include "DatasmithDefinitions.h"
#include "DatasmithMesh.h"
#include "DatasmithSceneFactory.h"
#include "IDatasmithSceneElements.h"

#include "RawMesh.h"
#include "HAL/FileManager.h"
#include "Math/UnrealMath.h"
#include "Misc/EngineVersion.h"
#include "UObject/NameTypes.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionOperations.h"

static const float DATASMITH_FORMAT_VERSION = 0.24f; // Major.Minor - A change in the major version means that backward compatibility is broken

// List of invalid names copied from Engine/Source/Runtime/Core/Private/Misc/FileHelper.cpp
static const FString InvalidNames[] = {
	TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("CLOCK$"), TEXT("NUL"), TEXT("NONE"),
	TEXT("COM1"), TEXT("COM2"), TEXT("COM3"), TEXT("COM4"), TEXT("COM5"), TEXT("COM6"), TEXT("COM7"), TEXT("COM8"), TEXT("COM9"),
	TEXT("LPT1"), TEXT("LPT2"), TEXT("LPT3"), TEXT("LPT4"), TEXT("LPT5"), TEXT("LPT6"), TEXT("LPT7"), TEXT("LPT8"), TEXT("LPT9")
};

template<typename T, int32 Size>
static constexpr int GetArrayLength(T(&)[Size]) { return Size; }

void FDatasmithUtils::SanitizeNameInplace(FString& InString)
{
	static const TCHAR Original[] = TEXT("ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿБбВвГгДдЁёЖжЗзИиЙйКкЛлМмНнОоПпРрСсТтУуФфХхЦцЧчШшЩщЪъЫыЬьЭэЮюЯя'\"");
	static const TCHAR Modified[] = TEXT("AAAAAAECEEEEIIIIDNOOOOOx0UUUUYPsaaaaaaeceeeeiiiiOnoooood0uuuuypyBbVvGgDdEeJjZzIiYyKkLlMmNnOoPpRrSsTtUuFfJjTtCcSsSs__ii__EeYyYy__");
	static_assert(GetArrayLength(Original) == GetArrayLength(Modified), "array size mismatch");

	for (int32 i = 0; i < GetArrayLength(Original); i++)
	{
		InString.ReplaceCharInline(Original[i], Modified[i], ESearchCase::CaseSensitive);
	}

	// Also remove control character and other oddities
	for (TCHAR& InChar : InString)
	{
		InChar = FChar::IsPrint(InChar) ? InChar : TEXT('_');
	}
}

FString FDatasmithUtils::SanitizeName(FString InString)
{
	SanitizeNameInplace(InString);
	return InString;
}

FString FDatasmithUtils::SanitizeObjectName(FString InString)
{
	// List of invalid characters taken from ObjectTools::SanitizeObjectName
	// Also prevent character not allowed in filenames as we use labels as package name
	const FString Invalid = INVALID_OBJECTNAME_CHARACTERS TEXT("*<>?\\·");

	if ( InString.IsEmpty() )
	{
		return InString;
	}

	for (int32 i = 0; i < Invalid.Len(); i++)
	{
		InString.ReplaceCharInline(Invalid[i], TCHAR('_'), ESearchCase::CaseSensitive);
	}

	SanitizeNameInplace(InString);

	// Unreal does not support names starting with '_'
	if (InString[0] == TEXT('_'))
	{
		InString.InsertAt(0, TEXT("Object"));
	}

	// Object's Name equals to "[InvalidNames]" or formatted like "[InvalidNames][_]%d" will generate a crash on save or reload of the asset. See UE-66320.
	// Consequently, in that case, "_SAFE" is appended to the name to avoid the crash but keep the original name
	// Note that the format (uppercase, lowercase) of "[InvalidNames]" does not matter.
	for (const FString& InvalidName : InvalidNames)
	{
		if (InString.StartsWith(InvalidName, ESearchCase::IgnoreCase))
		{
			const int32 InvalidNameLen = InvalidName.Len();
			if (InString.Len() == InvalidNameLen)
			{
				InString += TEXT("_SAFE");
			}
			else
			{
				const int32 InStringLen = InString.Len();
				int32 Index = InString[InvalidNameLen] == TEXT('_') ? InvalidNameLen + 1 : InvalidNameLen;
				for (; Index < InStringLen; ++Index)
				{
					if (!FChar::IsDigit(InString[Index]))
					{
						break;
					}
				}

				// Input name is formatted as "[InvalidNames][_]%d". Append "_SAFE"
				if (Index == InStringLen)
				{
					InString += TEXT("_SAFE");
				}
			}

			break;
		}
	}

	return InString;
}

FString FDatasmithUtils::SanitizeFileName(FString InString)
{
	const FString Invalid = TEXT(" \"'*:.,;<>?/\\|&$·#");
	for (int32 i = 0; i < Invalid.Len(); i++)
	{
		InString.ReplaceCharInline(Invalid[i], TCHAR('_'), ESearchCase::CaseSensitive);
	}
	SanitizeNameInplace(InString);
	return InString;
}

int32 FDatasmithUtils::GetEnterpriseVersionAsInt()
{
	const int32 MinorVersion = FEngineVersion::Current().GetMinor() * 10;
	int32 MinorNumberOfDigits = 1;
	for (int32 Version = MinorVersion; Version /= 10; MinorNumberOfDigits++);

	const int32 MajorVersion = FEngineVersion::Current().GetMajor() * FMath::Pow(10, MinorNumberOfDigits);

	return MajorVersion + MinorVersion;
}

FString FDatasmithUtils::GetEnterpriseVersionAsString()
{
	return FEngineVersion::Current().ToString( EVersionComponent::Minor );
}

float FDatasmithUtils::GetDatasmithFormatVersionAsFloat()
{
	return DATASMITH_FORMAT_VERSION;
}

int32 FDatasmithUtils::GetDatasmithFormatVersionAsInt()
{
	return (int32)DATASMITH_FORMAT_VERSION * 100;
}

FString FDatasmithUtils::GetDatasmithFormatVersionAsString()
{
	return FString::SanitizeFloat(DATASMITH_FORMAT_VERSION, 2);
}

const TCHAR* FDatasmithUtils::GetFileExtension()
{
	return TEXT("udatasmith");
}

const TCHAR* FDatasmithUtils::GetLongAppName()
{
	return TEXT("Unreal Datasmith");
}

const TCHAR* FDatasmithUtils::GetShortAppName()
{
	return TEXT("Datasmith");
}

double FDatasmithUtils::AreaTriangle3D(FVector v0, FVector v1, FVector v2)
{
	FVector TriangleNormal = (v1 - v2) ^ (v0 - v2);
	double Area = TriangleNormal.Size() * 0.5;

	return Area;
}

bool FDatasmithMeshUtils::ToRawMesh(const FDatasmithMesh& Mesh, FRawMesh& RawMesh, bool bValidateRawMesh)
{
	RawMesh.Empty();

	if (Mesh.GetVerticesCount() == 0 || Mesh.GetFacesCount() == 0)
	{
		return false;
	}

	RawMesh.VertexPositions.Reserve( Mesh.GetVerticesCount() );

	for ( int32 i = 0; i < Mesh.GetVerticesCount(); ++i )
	{
		RawMesh.VertexPositions.Add( Mesh.GetVertex( i ) );
	}

	RawMesh.FaceMaterialIndices.Reserve( Mesh.GetFacesCount() );
	RawMesh.FaceSmoothingMasks.Reserve( Mesh.GetFacesCount() );
	RawMesh.WedgeIndices.Reserve( Mesh.GetFacesCount() * 3 );
	RawMesh.WedgeTangentZ.Reserve( Mesh.GetFacesCount() * 3 );

	for ( int32 FaceIndex = 0; FaceIndex < Mesh.GetFacesCount(); ++FaceIndex )
	{
		int32 Vertex1;
		int32 Vertex2;
		int32 Vertex3;
		int32 MaterialId;

		Mesh.GetFace( FaceIndex, Vertex1, Vertex2, Vertex3, MaterialId );

		RawMesh.FaceMaterialIndices.Add( MaterialId );
		RawMesh.FaceSmoothingMasks.Add( Mesh.GetFaceSmoothingMask( FaceIndex ) );

		RawMesh.WedgeIndices.Add( Vertex1 );
		RawMesh.WedgeIndices.Add( Vertex2 );
		RawMesh.WedgeIndices.Add( Vertex3 );

		for ( int32 j = 0; j < 3; ++j )
		{
			RawMesh.WedgeTangentZ.Add( Mesh.GetNormal( FaceIndex * 3 + j ) );
		}
	}

	for ( int32 UVChannel = 0; UVChannel < FMath::Min( Mesh.GetUVChannelsCount(), (int32)MAX_MESH_TEXTURE_COORDS ) ; ++UVChannel )
	{
		RawMesh.WedgeTexCoords[ UVChannel ].Reserve( Mesh.GetFacesCount() * 3 );

		for ( int32 FaceIndex = 0; FaceIndex < Mesh.GetFacesCount(); ++FaceIndex )
		{
			int32 UVIndex1;
			int32 UVIndex2;
			int32 UVIndex3;

			Mesh.GetFaceUV( FaceIndex, UVChannel, UVIndex1, UVIndex2, UVIndex3 );

			RawMesh.WedgeTexCoords[ UVChannel ].Add( Mesh.GetUV( UVChannel, UVIndex1 ) );
			RawMesh.WedgeTexCoords[ UVChannel ].Add( Mesh.GetUV( UVChannel, UVIndex2 ) );
			RawMesh.WedgeTexCoords[ UVChannel ].Add( Mesh.GetUV( UVChannel, UVIndex3 ) );
		}
	}


	RawMesh.WedgeColors.Reserve( Mesh.GetVertexColorCount() );
	for ( int32 i = 0; i < Mesh.GetVertexColorCount(); i++ )
	{
		RawMesh.WedgeColors.Add( Mesh.GetVertexColor( i ) );
	}

	// Verify RawMesh is actually valid;
	if (bValidateRawMesh && !RawMesh.IsValid())
	{
		RawMesh.Empty();
		return false;
	}

	return true;
}

bool FDatasmithMeshUtils::ToMeshDescription(FDatasmithMesh& DsMesh, FMeshDescription& MeshDescription)
{
	MeshDescription.Empty();

	TVertexAttributesRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	// Prepared for static mesh usage ?
	if (!ensure(VertexPositions.IsValid())
	 || !ensure(VertexInstanceNormals.IsValid())
	 || !ensure(VertexInstanceUVs.IsValid())
	 || !ensure(PolygonGroupImportedMaterialSlotNames.IsValid()) )
	{
		return false;
	}

	// Reserve space for attributes.
	int32 VertexCount = DsMesh.GetVerticesCount();
	int32 TriangleCount = DsMesh.GetFacesCount();
	int32 VertexInstanceCount = 3 * TriangleCount;
	int32 MaterialCount = DsMesh.GetMaterialsCount();
	MeshDescription.ReserveNewVertices(VertexCount);
	MeshDescription.ReserveNewVertexInstances(VertexInstanceCount);
	MeshDescription.ReserveNewEdges(VertexInstanceCount);
	MeshDescription.ReserveNewPolygons(TriangleCount);
	MeshDescription.ReserveNewPolygonGroups(MaterialCount);

	// At least one UV set must exist.
	int32 DsUVCount = DsMesh.GetUVChannelsCount();
	int32 MeshDescUVCount = FMath::Max(1, DsUVCount);
	VertexInstanceUVs.SetNumIndices(MeshDescUVCount);

	//Fill the vertex array
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		FVertexID AddedVertexId = MeshDescription.CreateVertex();
		VertexPositions[AddedVertexId] = DsMesh.GetVertex(VertexIndex);
	}

	TMap<int32, FPolygonGroupID> PolygonGroupMapping;
	auto GetOrCreatePolygonGroupId = [&](int32 MaterialIndex)
	{
		FPolygonGroupID& PolyGroupId = PolygonGroupMapping.FindOrAdd(MaterialIndex);
		if (PolyGroupId == FPolygonGroupID::Invalid)
		{
			PolyGroupId = MeshDescription.CreatePolygonGroup();
			FName ImportedSlotName = *FString::FromInt(MaterialIndex); // No access to DatasmithMeshHelper::DefaultSlotName
			PolygonGroupImportedMaterialSlotNames[PolyGroupId] = ImportedSlotName;
		}
		return PolyGroupId;
	};

	// corner informations
	const int32 CornerCount = 3; // only triangles in DatasmithMesh
	FVector CornerPositions[3];
	TArray<FVertexInstanceID> CornerVertexInstanceIDs;
	CornerVertexInstanceIDs.SetNum(3);
	FVertexID CornerVertexIDs[3];
	TArray<uint32> FaceSmoothingMasks;
	for (int32 PolygonIndex = 0; PolygonIndex < TriangleCount; PolygonIndex++)
	{
		// face basics info
		int32 MaterialIndex;
		int32 VertexIndex[3];
		DsMesh.GetFace(PolygonIndex, VertexIndex[0], VertexIndex[1], VertexIndex[2], MaterialIndex);
		for (int32 CornerIndex = 0; CornerIndex < CornerCount; CornerIndex++)
		{
			CornerVertexIDs[CornerIndex] = FVertexID(VertexIndex[CornerIndex]);
			CornerPositions[CornerIndex] = VertexPositions[CornerVertexIDs[CornerIndex]];
		}

		// Create Vertex instances
		for (int32 CornerIndex = 0; CornerIndex < CornerCount; CornerIndex++)
		{
			CornerVertexInstanceIDs[CornerIndex] = MeshDescription.CreateVertexInstance(CornerVertexIDs[CornerIndex]);
		}

		// UVs attributes
		for (int32 UVChannelIndex = 0; UVChannelIndex < DsUVCount; ++UVChannelIndex)
		{
			int32 UV[3];
			DsMesh.GetFaceUV(PolygonIndex, UVChannelIndex, UV[0], UV[1], UV[2]);
			for (int32 CornerIndex = 0; CornerIndex < CornerCount; ++CornerIndex)
			{
				check(UV[CornerIndex] < DsMesh.GetUVCount(UVChannelIndex))
				FVector2D UVVector = DsMesh.GetUV(UVChannelIndex, UV[CornerIndex]);
				if (!UVVector.ContainsNaN())
				{
					VertexInstanceUVs.Set(CornerVertexInstanceIDs[CornerIndex], UVChannelIndex, UVVector);
				}
			}
		}

		for (int32 CornerIndex = 0; CornerIndex < CornerCount; CornerIndex++)
		{
			VertexInstanceNormals[CornerVertexInstanceIDs[CornerIndex]] = DsMesh.GetNormal(3 * PolygonIndex + CornerIndex);
		}

		// smoothing information
		FaceSmoothingMasks.Add(DsMesh.GetFaceSmoothingMask(PolygonIndex));

		// Create in-mesh Polygon
		const FPolygonGroupID PolygonGroupID = GetOrCreatePolygonGroupId(MaterialIndex);
		const FPolygonID NewPolygonID = MeshDescription.CreatePolygon(PolygonGroupID, CornerVertexInstanceIDs);
	}

	FMeshDescriptionOperations::ConvertSmoothGroupToHardEdges(FaceSmoothingMasks, MeshDescription);

	return true;
}

bool FDatasmithMeshUtils::IsUVChannelValid(const FDatasmithMesh& DsMesh, const int32 Channel)
{
	for (int32 FaceIndex = 0, FacesCount = DsMesh.GetFacesCount(); FaceIndex < FacesCount; ++FaceIndex)
	{
		int32 UV[3];
		DsMesh.GetFaceUV(FaceIndex, Channel, UV[0], UV[1], UV[2]);

		FVector2D UVCoords[3] = {
			DsMesh.GetUV(Channel, UV[0]),
			DsMesh.GetUV(Channel, UV[1]),
			DsMesh.GetUV(Channel, UV[2]) };

		float TriangleArea = FMath::Abs(((UVCoords[0].X * (UVCoords[1].Y - UVCoords[2].Y))
			+ (UVCoords[1].X * (UVCoords[2].Y - UVCoords[0].Y))
			+ (UVCoords[2].X * (UVCoords[0].Y - UVCoords[1].Y))) * 0.5f);

		if (TriangleArea <= SMALL_NUMBER)
		{
			return false;
		}
	}

	return true;
}

bool FDatasmithTextureUtils::CalculateTextureHash(const TSharedPtr<class IDatasmithTextureElement>& TextureElement)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(TextureElement->GetFile()));
	if (Archive)
	{
		TextureElement->SetFileHash(FMD5Hash::HashFileFromArchive(Archive.Get()));
		return true;
	}
	return false;
}

void FDatasmithTextureUtils::CalculateTextureHashes(const TSharedPtr<IDatasmithScene>& Scene)
{
	for (int i = 0; i < Scene->GetTexturesCount(); ++i)
	{
		CalculateTextureHash(Scene->GetTexture(i));
	}
}

ETransformChannelComponents FDatasmithAnimationUtils::GetChannelTypeComponents(EDatasmithTransformChannels Channels, EDatasmithTransformType TransformType)
{
	static_assert((uint16)EDatasmithTransformChannels::TranslationX == 0x001, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::TranslationY == 0x002, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::TranslationZ == 0x004, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::RotationX == 0x008, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::RotationY == 0x010, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::RotationZ == 0x020, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::ScaleX == 0x040, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::ScaleY == 0x080, "INVALID_ENUM_VALUE");
	static_assert((uint16)EDatasmithTransformChannels::ScaleZ == 0x100, "INVALID_ENUM_VALUE");

	ETransformChannelComponents Result = ETransformChannelComponents::None;

	switch (TransformType)
	{
	case EDatasmithTransformType::Translation:
		Result |= (ETransformChannelComponents) (((uint16)Channels) & 0xFF);
		break;
	case EDatasmithTransformType::Rotation:
		Result |= (ETransformChannelComponents) (((uint16)Channels >> 3) & 0xFF);
		break;
	case EDatasmithTransformType::Scale:
		Result |= (ETransformChannelComponents) (((uint16)Channels >> 6) & 0xFF);
		break;
	default:
		break;
	}

	return Result;
}

EDatasmithTransformChannels FDatasmithAnimationUtils::SetChannelTypeComponents(ETransformChannelComponents Components, EDatasmithTransformType TransformType)
{
	EDatasmithTransformChannels Result = EDatasmithTransformChannels::None;

	switch (TransformType)
	{
	case EDatasmithTransformType::Translation:
		Result |= (EDatasmithTransformChannels) ((uint16)Components);
		break;
	case EDatasmithTransformType::Rotation:
		Result |= (EDatasmithTransformChannels) ((uint16)Components << 3);
		break;
	case EDatasmithTransformType::Scale:
		Result |= (EDatasmithTransformChannels) ((uint16)Components << 6);
		break;
	default:
		break;
	}

	return Result;
}

namespace DatasmithSceneUtilsImpl
{
	template<typename IElementType>
	void GetAllActorsChildRecursive(const TSharedPtr<IDatasmithActorElement>& ActorElement, EDatasmithElementType ElementType, TArray<TSharedPtr<IElementType>>& OutResult)
	{
		if (ActorElement->IsA(ElementType))
		{
			OutResult.Add(StaticCastSharedPtr<IElementType>(ActorElement));
		}

		const int32 ChildrenCount = ActorElement->GetChildrenCount();
		for (int32 ChildIndex = 0; ChildIndex < ChildrenCount; ++ChildIndex)
		{
			DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(ActorElement->GetChild(ChildIndex), ElementType, OutResult);
		}
	}

	template<typename IElementType>
	void GetAllActorsChildRecursive(const TSharedPtr<IDatasmithScene>& Scene, EDatasmithElementType ElementType, TArray<TSharedPtr<IElementType>>& OutResult)
	{
		const int32 ActorsCount = Scene->GetActorsCount();
		for (int32 ActorIndex = 0; ActorIndex < ActorsCount; ++ActorIndex)
		{
			DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(Scene->GetActor(ActorIndex), ElementType, OutResult);
		}
	}
}

TArray<TSharedPtr<IDatasmithCameraActorElement>> FDatasmithSceneUtils::GetAllCameraActorsFromScene(const TSharedPtr<IDatasmithScene>& Scene)
{
	TArray<TSharedPtr<IDatasmithCameraActorElement>> Result;
	Result.Reserve(Scene->GetActorsCount());
	DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(Scene, EDatasmithElementType::Camera, Result);
	return Result;
}

TArray<TSharedPtr<IDatasmithLightActorElement>> FDatasmithSceneUtils::GetAllLightActorsFromScene(const TSharedPtr<IDatasmithScene>& Scene)
{
	TArray<TSharedPtr<IDatasmithLightActorElement>> Result;
	Result.Reserve(Scene->GetActorsCount());
	DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(Scene, EDatasmithElementType::Light, Result);
	return Result;
}

TArray<TSharedPtr<IDatasmithMeshActorElement>> FDatasmithSceneUtils::GetAllMeshActorsFromScene(const TSharedPtr<IDatasmithScene>& Scene)
{
	TArray<TSharedPtr<IDatasmithMeshActorElement>> Result;
	Result.Reserve(Scene->GetActorsCount());
	DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(Scene, EDatasmithElementType::StaticMeshActor, Result);
	return Result;
}

TArray< TSharedPtr< IDatasmithCustomActorElement> > FDatasmithSceneUtils::GetAllCustomActorsFromScene(const TSharedPtr<IDatasmithScene>& Scene)
{
	TArray<TSharedPtr<IDatasmithCustomActorElement>> Result;
	Result.Reserve(Scene->GetActorsCount());
	DatasmithSceneUtilsImpl::GetAllActorsChildRecursive(Scene, EDatasmithElementType::CustomActor, Result);
	return Result;
}

namespace DatasmithSceneUtilsImpl
{
	bool FindActorHierarchy(const TSharedPtr<IDatasmithActorElement>& ActorElement, const TSharedPtr<IDatasmithActorElement>& ToFind, FDatasmithSceneUtils::TActorHierarchy& OutHierarchy)
	{
		if (ActorElement == ToFind)
		{
			return true;
		}

		int32 ChildrenCount = ActorElement->GetChildrenCount();
		for (int32 ChildIndex = 0; ChildIndex < ChildrenCount; ++ChildIndex)
		{
			const TSharedPtr< IDatasmithActorElement > Child = ActorElement->GetChild(ChildIndex);

			bool bFound = FindActorHierarchy(Child, ToFind, OutHierarchy);
			if (bFound)
			{
				OutHierarchy.Add(ActorElement);
				return true;
			}
		}
		return false;
	}
}

bool FDatasmithSceneUtils::FindActorHierarchy(const IDatasmithScene* Scene, const TSharedPtr<IDatasmithActorElement>& ToFind, TActorHierarchy& OutHierarchy)
{
	bool bResult = false;
	OutHierarchy.Reset();
	if (ToFind.IsValid())
	{
		const int32 ActorsCount = Scene->GetActorsCount();
		for (int32 ActorIndex = 0; ActorIndex  < ActorsCount; ++ActorIndex)
		{
			const TSharedPtr< IDatasmithActorElement>& ActorElement = Scene->GetActor(ActorIndex);
			bResult = DatasmithSceneUtilsImpl::FindActorHierarchy(ActorElement, ToFind, OutHierarchy);
			if (bResult)
			{
				OutHierarchy.Add(ActorElement);
				break;
			}
		}
	}

	if (bResult)
	{
		Algo::Reverse(OutHierarchy);
	}
	return bResult;
}

bool FDatasmithSceneUtils::IsMaterialIDUsedInScene(const TSharedPtr<IDatasmithScene>& Scene, const TSharedPtr<IDatasmithMaterialIDElement>& MaterialElement)
{
	TArray<TSharedPtr<IDatasmithMeshActorElement>> AllMeshActors = FDatasmithSceneUtils::GetAllMeshActorsFromScene(Scene);
	for (const TSharedPtr<IDatasmithMeshActorElement>& MeshActor : AllMeshActors)
	{
		const int32 MaterialCount = MeshActor->GetMaterialOverridesCount();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			if (MeshActor->GetMaterialOverride(MaterialIndex) == MaterialElement)
			{
				return true;
			}
		}
	}

	TArray<TSharedPtr<IDatasmithLightActorElement>> AllLightActors = FDatasmithSceneUtils::GetAllLightActorsFromScene(Scene);
	for (const TSharedPtr<IDatasmithLightActorElement>& LightActor : AllLightActors)
	{
		if (LightActor->GetLightFunctionMaterial() == MaterialElement)
		{
			return true;
		}
	}
	return false;
}

bool FDatasmithSceneUtils::IsPostProcessUsedInScene(const TSharedPtr<IDatasmithScene>& Scene, const TSharedPtr<IDatasmithPostProcessElement>& PostProcessElement)
{
	if (Scene->GetPostProcess() == PostProcessElement)
	{
		return true;
	}

	TArray<TSharedPtr<IDatasmithCameraActorElement>> AllCameraActors = GetAllCameraActorsFromScene(Scene);
	for (const TSharedPtr<IDatasmithCameraActorElement>& CameraActor : AllCameraActors)
	{
		if (CameraActor->GetPostProcess() == PostProcessElement)
		{
			return true;
		}
	}
	return false;
}

FString FDatasmithUniqueNameProviderBase::GenerateUniqueName(const FString& BaseName)
{
	const int32 FrequentlyUsedThreshold = 5; // don't saturate the table with uncommon names
	if (!Contains(BaseName))
	{
		AddExistingName(BaseName);
		return BaseName;
	}

	// use the frequently used label info to avoid useless indices iterations
	int32 LastKnownIndex = 1;
	int32* FreqIndexPtr = FrequentlyUsedNames.Find(BaseName);
	if (FreqIndexPtr != nullptr &&  *FreqIndexPtr > LastKnownIndex)
	{
		LastKnownIndex = *FreqIndexPtr;
	}

	// loop to find an available indexed name
	FString ModifiedName;
	do
	{
		++LastKnownIndex;
		ModifiedName = FString::Printf(TEXT("%s_%d"), *BaseName, LastKnownIndex);
	} while (Contains(ModifiedName));

	// update frequently used names
	if (FreqIndexPtr != nullptr)
	{
		*FreqIndexPtr = LastKnownIndex;
	}
	else if (LastKnownIndex > FrequentlyUsedThreshold)
	{
		FrequentlyUsedNames.Add(BaseName, LastKnownIndex);
	}

	AddExistingName(ModifiedName);

	return ModifiedName;
}


FTransform FDatasmithUtils::ConvertTransform(EModelCoordSystem SourceCoordSystem, const FTransform& LocalTransform)
{
	// convert to UE coords
	static const FTransform RightHanded(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(-1.0f, 1.0f, 1.0f));
	static const FTransform YUpMatrix(FMatrix(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f)));
	static const FTransform YUpMatrixInv(YUpMatrix.Inverse());

	switch (SourceCoordSystem)
	{
	case EModelCoordSystem::ZUp_RightHanded:
	{
		return RightHanded * LocalTransform * RightHanded;
	}
	case EModelCoordSystem::YUp_LeftHanded:
	{
		return YUpMatrix * LocalTransform * YUpMatrixInv;
	}
	case EModelCoordSystem::YUp_RightHanded:
	{
		return RightHanded * YUpMatrix * LocalTransform * YUpMatrixInv * RightHanded;
	}
	default:
	{
		return LocalTransform;
	}
	}
}


FMatrix FDatasmithUtils::GetSymmetricMatrix(const FVector& Origin, const FVector& Normal)
{
	//Calculate symmetry matrix
	//(Px, Py, Pz) = normal
	// -Px² + Pz² + Py²  |  - 2 * Px * Py     |  - 2 * Px * Pz
	// - 2 * Py * Px     |  - Py² + Px² + Pz² |  - 2 * Py * Pz
	// - 2 * Pz * Px     |  - 2 * Pz * Py     |  - Pz² + Py² + Px²

	FVector LocOrigin = Origin;

	float NormalXSqr, NormalYSqr, NormalZSqr;
	NormalXSqr = Normal.X * Normal.X;
	NormalYSqr = Normal.Y * Normal.Y;
	NormalZSqr = Normal.Z * Normal.Z;

	FMatrix OSymmetricMatrix;
	OSymmetricMatrix.SetIdentity();
	FVector Axis0(-NormalXSqr + NormalZSqr + NormalYSqr, -2 * Normal.X * Normal.Y, -2 * Normal.X * Normal.Z);
	FVector Axis1(-2 * Normal.Y * Normal.X, -NormalYSqr + NormalXSqr + NormalZSqr, -2 * Normal.Y * Normal.Z);
	FVector Axis2(-2 * Normal.Z * Normal.X, -2 * Normal.Z * Normal.Y, -NormalZSqr + NormalYSqr + NormalXSqr);
	OSymmetricMatrix.SetAxes(&Axis0, &Axis1, &Axis2);

	FMatrix SymmetricMatrix;
	SymmetricMatrix.SetIdentity();

	//Translate to 0, 0, 0
	LocOrigin *= -1.;
	SymmetricMatrix.SetOrigin(LocOrigin);

	//// Apply Symmetric
	SymmetricMatrix *= OSymmetricMatrix;

	//Translate to original position
	LocOrigin *= -1.;
	FMatrix OrigTranslation;
	OrigTranslation.SetIdentity();
	OrigTranslation.SetOrigin(LocOrigin);
	SymmetricMatrix *= OrigTranslation;

	return SymmetricMatrix;
}



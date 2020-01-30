// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxHairTranslator.h"

#include "GroomImportOptions.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#include <fbxsdk.h>
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace FbxHairFormat
{
	static const float RootRadius = 0.0001f; // m
	static const float TipRadius = 0.00005f; // m

	/** File unit to cm ratio */
	static constexpr const float UNIT_TO_CM = 100;
}

class FFbxHairImporter
{
public:
	FFbxHairImporter()
		: FileManager(nullptr)
		, FileScene(nullptr)
		, FileImporter(nullptr)
	{
		FileManager = FbxManager::Create();

		if (FileManager)
		{
			FbxIOSettings* FileSettings = FbxIOSettings::Create(FileManager, IOSROOT);

			FileManager->SetIOSettings(FileSettings);

			FileScene = FbxScene::Create(FileManager, "StrandAssetFbx");

			FileImporter = FbxImporter::Create(FileManager, "");
		}
	}

	~FFbxHairImporter()
	{
		if (FileImporter)
		{
			FileImporter->Destroy();
		}

		if (FileScene)
		{
			FileScene->Destroy();
		}

		if (FileManager)
		{
			FileManager->Destroy();
		}
	}

	bool ImportFile(const FString& FileName)
	{
		if (!FileManager)
		{
			return false;
		}

		FbxIOSettings* FileSettings = FileManager->GetIOSettings();

		// Initialize the importer by providing a filename.
		bool FileStatus = FileImporter->Initialize(TCHAR_TO_UTF8(*FileName), -1, FileSettings);

		if (FileStatus && FileImporter->IsFBX())
		{
			// Only load geometry data
			FileSettings->SetBoolProp(IMP_FBX_MATERIAL, false);
			FileSettings->SetBoolProp(IMP_FBX_TEXTURE, false);
			FileSettings->SetBoolProp(IMP_FBX_LINK, false);
			FileSettings->SetBoolProp(IMP_FBX_SHAPE, true);
			FileSettings->SetBoolProp(IMP_FBX_GOBO, false);
			FileSettings->SetBoolProp(IMP_FBX_ANIMATION, false);
			FileSettings->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, false);

			// Import the scene and destroy the importer.
			FileStatus = FileImporter->Import(FileScene);

			if (FileStatus)
			{
				ImportedFileName = FileName;
			}
		}

		return FileStatus;
	}

	const FString& GetImportedFileName() const
	{
		return ImportedFileName;
	}

	FbxScene* GetFbxScene() const
	{
		return FileScene;
	}

private:
	FString ImportedFileName;

	FbxManager* FileManager;
	FbxScene* FileScene;
	FbxImporter* FileImporter;
};

static void ParseFbxNode(FbxNode* FileNode, FHairDescription& HairDescription, const FMatrix& ConversionMatrix, float Scale)
{
	if (FileNode->GetNodeAttribute() != NULL && FileNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eNurbsCurve)
	{
		FbxNurbsCurve* NurbsCurve = static_cast<FbxNurbsCurve*>(FileNode->GetNodeAttribute());
		FbxArray<FbxVector4> PointArray;
		const uint32 PointCount = NurbsCurve->TessellateCurve(PointArray, 2);

		FStrandID StrandID = HairDescription.AddStrand();

		TStrandAttributesRef<int> StrandNumVertices = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::VertexCount);
		StrandNumVertices[StrandID] = PointCount;

		TVertexAttributesRef<FVector> VertexPositions = HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Position);
		TVertexAttributesRef<float> VertexWidths = HairDescription.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);

		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const float CoordU = PointIndex / static_cast<float>(PointCount - 1);
			const float Radius = FMath::Lerp(FbxHairFormat::RootRadius, FbxHairFormat::TipRadius, CoordU);
			const FVector Position(PointArray[PointIndex][0], PointArray[PointIndex][1], PointArray[PointIndex][2]);

			FVertexID VertexID = HairDescription.AddVertex();

			VertexPositions[VertexID] = ConversionMatrix.TransformPosition(Position);
			VertexWidths[VertexID] = Radius * Scale;
		}
	}
	//	#todo_hair
	// * Load user/custom properties (eccentricity, width/thickness, ...)
	// * Load root/hierarchy transform

	const uint32 NumChildren = FileNode->GetChildCount();
	if (NumChildren > 0)
	{
		for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount(); ++ChildIndex)
		{
			ParseFbxNode(FileNode->GetChild(ChildIndex), HairDescription, ConversionMatrix, Scale);
		}
	}
}

bool FFbxHairTranslator::Translate(const FString& FileName, FHairDescription& HairDescription, const FGroomConversionSettings& ConversionSettings)
{
	// Reuse the FbxHairImporter if there was one created previously by CanTranslate
	// There could be none if the translator is used for a re-import
	if (!FbxHairImporter.IsValid() || FileName != FbxHairImporter->GetImportedFileName())
	{
		FbxHairImporter = MakeShared<FFbxHairImporter>();
		if (!FbxHairImporter.IsValid())
		{
			return false;
		}

		bool bFileImported = FbxHairImporter->ImportFile(FileName);
		if (!bFileImported)
		{
			FbxHairImporter.Reset();
			return false;
		}
	}

	// Add required version attributes, first version 0.1
	FGroomID GroomID(0);
	HairDescription.GroomAttributes().RegisterAttribute<int>(HairAttribute::Groom::MajorVersion);
	TGroomAttributesRef<int> MajorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MajorVersion);
	MajorVersion.Set(GroomID, 0);

	HairDescription.GroomAttributes().RegisterAttribute<int>(HairAttribute::Groom::MinorVersion);
	TGroomAttributesRef<int> MinorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MinorVersion);
	MinorVersion.Set(GroomID, 1);

	// Handle width as a per-vertex attribute
	HairDescription.VertexAttributes().RegisterAttribute<float>(HairAttribute::Vertex::Width);

	if (FbxNode* FileNode = FbxHairImporter->GetFbxScene()->GetRootNode())
	{
		FMatrix ConversionMatrix = FScaleMatrix::Make(ConversionSettings.Scale) * FRotationMatrix::Make(FQuat::MakeFromEuler(ConversionSettings.Rotation));
		for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount(); ++ChildIndex)
		{
			ParseFbxNode(FileNode->GetChild(ChildIndex), HairDescription, ConversionMatrix, ConversionSettings.Scale.X);
		}
	}

	// Can now release the FbxHairImporter
	FbxHairImporter.Reset();

	return HairDescription.IsValid();
}

static void ValidateFbxNode(FbxNode* FileNode, int32& NumCurves, bool& bHasUntranslatableData)
{
	// Validate that the FBX has curves only
	// Reject any attribute type that could be translated by another translator/factory
	if (FileNode->GetNodeAttribute())
	{
		switch (FileNode->GetNodeAttribute()->GetAttributeType())
		{
		case FbxNodeAttribute::eSkeleton:
		case FbxNodeAttribute::eMesh:
			bHasUntranslatableData = true;
			break;
		case FbxNodeAttribute::eNurbsCurve:
			++NumCurves;
			break;
		}
	}

	for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount() && !bHasUntranslatableData; ++ChildIndex)
	{
		ValidateFbxNode(FileNode->GetChild(ChildIndex), NumCurves, bHasUntranslatableData);
	}
}

bool FFbxHairTranslator::CanTranslate(const FString& FilePath)
{
	if (!IsFileExtensionSupported(FPaths::GetExtension(FilePath)))
	{
		return false;
	}

	// Import the FBX file and check if it contains curves only
	FbxHairImporter = MakeShared<FFbxHairImporter>();
	if (!FbxHairImporter.IsValid())
	{
		return false;
	}

	bool bFileImported = FbxHairImporter->ImportFile(FilePath);
	bool bCanTranslate = false;

	if (bFileImported)
	{
		int32 NumCurves = 0;
		bool bHasUntranslatableData = false;
		if (FbxNode* FileNode = FbxHairImporter->GetFbxScene()->GetRootNode())
		{
			for (int32 ChildIndex = 0; ChildIndex < FileNode->GetChildCount() && !bHasUntranslatableData; ++ChildIndex)
			{
				ValidateFbxNode(FileNode->GetChild(ChildIndex), NumCurves, bHasUntranslatableData);
			}
		}

		bCanTranslate = !bHasUntranslatableData && NumCurves > 0;
	}

	// Keep the FbxHairImporter until the file is translated to prevent importing the file again by the actual Translate
	if (!bFileImported || !bCanTranslate)
	{
		FbxHairImporter.Reset();
	}

	return bCanTranslate;
}

bool FFbxHairTranslator::IsFileExtensionSupported(const FString& FileExtension) const
{
	return GetSupportedFormat().StartsWith(FileExtension);
}

FString FFbxHairTranslator::GetSupportedFormat() const
{
	return TEXT("fbx;Fbx hair strands file");
}

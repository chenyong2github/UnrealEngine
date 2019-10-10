// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairFormatTranslator.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "GroomImportOptions.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"

#define HAIR_FILE_SEGMENTS_BIT		1
#define HAIR_FILE_POINTS_BIT		2
#define HAIR_FILE_THICKNESS_BIT		4
#define HAIR_FILE_TRANSPARENCY_BIT	8
#define HAIR_FILE_COLORS_BIT		16  

namespace HairFormat
{
	static const float RootRadius = 0.0001f; // m
	static const float TipRadius = 0.00005f; // m

	/** File unit to cm ratio */
	static constexpr const float UNIT_TO_CM = 10;

	/* Hair file header */
	struct FFileHeader
	{
		/* This should be "HAIR" */
		char	FileSignature[4];	

		/* Number of hair strands */
		uint32	NumStrands = 0;	

		/* Total number of points of all strands */
		uint32	NumPoints = 0;

		/* Bit array of data in the file */
		uint32	BitArrays = 0;

		/* Default number of segments of each strand */
		uint32	StrandCount = 0;

		/* Default thickness of hair strands */
		float	StrandThickness = 1.f;

		/* Default transparency of hair strands */
		float	StrandTransparency = 0.f;	

		/* Default color of hair strands */
		float	StrandColor[3] = { 0.f,0.f,0.f };

		/* Information about the file */
		char	FileInfo[88];	
	};
}

bool FHairFormatTranslator::Translate(const FString& FileName, FHairDescription& HairDescription, const FGroomConversionSettings& ConversionSettings)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileHandle* FileHandle = PlatformFile.OpenRead(*FileName);

	bool bIsValid = false;

	if (FileHandle)
	{
		HairFormat::FFileHeader FileHeader;
		if (FileHandle->Read(reinterpret_cast<uint8*>(&FileHeader), sizeof(HairFormat::FFileHeader)))
		{
			if (strncmp(FileHeader.FileSignature, "HAIR", 4) == 0)
			{
				// Add required version attributes, first version 0.1
				FGroomID GroomID(0);
				HairDescription.GroomAttributes().RegisterAttribute<int>(HairAttribute::Groom::MajorVersion);
				TGroomAttributesRef<int> MajorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MajorVersion);
				MajorVersion.Set(GroomID, 0);

				HairDescription.GroomAttributes().RegisterAttribute<int>(HairAttribute::Groom::MinorVersion);
				TGroomAttributesRef<int> MinorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MinorVersion);
				MinorVersion.Set(GroomID, 1);

				int32 NumCurves = FileHeader.NumStrands;
				int32 NumVertices = FileHeader.NumPoints;

				HairDescription.InitializeStrands(NumCurves);
				HairDescription.InitializeVertices(NumVertices);

				// Read strand segment counts
				TStrandAttributesRef<int> StrandNumVertices = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::VertexCount);
				if (FileHeader.BitArrays & HAIR_FILE_SEGMENTS_BIT)
				{
					TArray<uint16> NumSegmentsPerCurve;
					NumSegmentsPerCurve.SetNum(NumCurves);

					FileHandle->Read(reinterpret_cast<uint8*>(NumSegmentsPerCurve.GetData()), sizeof(uint16) * NumCurves);

					for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
					{
						FStrandID StrandID(CurveIndex);
						StrandNumVertices[StrandID] = NumSegmentsPerCurve[CurveIndex] + 1; // a segment has a start and end vertex 
					}
				}
				else
				{
					for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
					{
						FStrandID StrandID(CurveIndex);
						StrandNumVertices[StrandID] = FileHeader.StrandCount;
					}
				}

				// Read strand vertex positions
				TVertexAttributesRef<FVector> VertexPositions = HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Position);
				if (FileHeader.BitArrays & HAIR_FILE_POINTS_BIT)
				{
					FMatrix ConversionMatrix = FScaleMatrix::Make(ConversionSettings.Scale) * FRotationMatrix::Make(FQuat::MakeFromEuler(ConversionSettings.Rotation));

					TArray<FVector> Positions;
					Positions.SetNum(NumVertices);

					FileHandle->Read(reinterpret_cast<uint8*>(&Positions.GetData()->X), sizeof(float) * NumVertices * 3);

					for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
					{
						FVertexID VertexID(VertexIndex);
						VertexPositions[VertexID] = ConversionMatrix.TransformPosition(Positions[VertexIndex]);
					}
				}

				// Read strand vertex thickness, with default value of StrandThickness if this bit is not set
				HairDescription.VertexAttributes().RegisterAttribute<float>(HairAttribute::Vertex::Width, 1, FileHeader.StrandThickness);
				TVertexAttributesRef<float> VertexWidths = HairDescription.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);
				if (FileHeader.BitArrays & HAIR_FILE_THICKNESS_BIT)
				{
					float Scale = ConversionSettings.Scale.X;

					TArray<float> Widths;
					Widths.SetNum(NumVertices);

					FileHandle->Read(reinterpret_cast<uint8*>(Widths.GetData()), sizeof(float) * NumVertices);

					for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
					{
						FVertexID VertexID(VertexIndex);
						VertexWidths[VertexID] = Widths[VertexIndex] * Scale;
					}
				}
				
				bIsValid = HairDescription.IsValid();
			}
		}
		delete FileHandle;
	}
	return bIsValid;
}

bool FHairFormatTranslator::CanTranslate(const FString& FilePath)
{
	return IsFileExtensionSupported(FPaths::GetExtension(FilePath));
}

bool FHairFormatTranslator::IsFileExtensionSupported(const FString& FileExtension) const
{
	return GetSupportedFormat().StartsWith(FileExtension);
}

FString FHairFormatTranslator::GetSupportedFormat() const
{
	return TEXT("hair;Hair format hair strands file");
}

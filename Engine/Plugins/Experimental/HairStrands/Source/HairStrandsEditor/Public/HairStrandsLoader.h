// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Containers/UnrealString.h"

/** .hair file format */
struct FHairFormat
{
  /** File unit to cm ratio */
  static constexpr const float UNIT_TO_CM = 10;

  /** Parse the .hair file */
  static void ParseFile(const FString& FileName, struct FHairStrandsDatas& HairStrands);

private :

	///* Hair file header */
	//struct HairFileHeader
	//{
	//	char	FileSignature[4];	/* This should be "HAIR" */
	//	uint32	NumStrands = 0;		/* number of hair strands */
	//	uint32	NumVertices = 0;	/* total number of points of all strands */
	//	uint32	BitArrays = 0;			/* bit array of data in the file */

	//	uint32	StrandSize = 0;		/* default number of segments of each strand */
	//	float	StrandThickness = 1.f;	/* default thickness of hair strands */
	//	float	StrandTransparency = 0.f;	/* default transparency of hair strands */
	//	float	StrandColor[3] = { 0.f,0.f,0.f };		/* default color of hair strands */
	//	char	FileInfo[HAIR_FILE_INFO_SIZE];	/* information about the file */
	//};

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
};

/** .fbx file format */
struct FFbxFormat
{
  /** File unit to cm ratio */
  static constexpr const float UNIT_TO_CM = 100;

  /** Parse the .fbx file */
  static void ParseFile(const FString& FileName, struct FHairStrandsDatas& HairStrands);
};

/** .abc file format */
struct FAbcFormat
{
   /** File unit to cm ratio */
  static constexpr const float UNIT_TO_CM = 1;

  /** Parse the .abc file */
  static void ParseFile(const FString& FileName, struct FHairStrandsDatas& HairStrands);
};

/** Hair strands loader */
template<typename FileFormat>
struct THairStrandsLoader
{
    static void LoadHairStrands(const FString& FileName, struct FHairStrandsDatas& HairStrands); 
};

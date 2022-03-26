// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection->cpp: FGeometryCollection methods.
=============================================================================*/

#include "ChaosFlesh/FleshCollectionUtility.h"

#include "ChaosFlesh/FleshCollection.h"
#include "CoreMinimal.h"
#include "Chaos/Real.h"
#include <fstream>
#include <string>

// logging
//#include "Logging\LogMacros.h"
DEFINE_LOG_CATEGORY(LogChaosFlesh);


namespace ChaosFlesh
{

	TUniquePtr<FFleshCollection> ImportTetFromFile(const FString& Filename)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Reading Path %s"), *Filename);

		TUniquePtr<FFleshCollection> Collection;

		TArray<FVector3f> Vertices;
		TArray<FIntVector4 > Elements;
		TArray<FIntVector3> SurfaceElements;

		std::ifstream File(std::string(TCHAR_TO_UTF8(*Filename)));
		{
			int NumParticles;
			{
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> NumParticles;
			}
			Vertices.SetNum(NumParticles);
			for (int i = 0; i < NumParticles; ++i)
			{
				Chaos::FReal x, y, z;
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> x;
				ss >> y;
				ss >> z;
				Vertices[i] = (FVector3f(x, y, z) + FVector3f(0, 4, 0));
			}
		}
		{
			int NumElements;
			{
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> NumElements;
			}
			Elements.SetNum(NumElements);
			for (int i = 0; i < NumElements; ++i)
			{
				int x, y, z, w;
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> x;
				ss >> y;
				ss >> z;
				ss >> w;
				Elements[i] = FIntVector4(x, y, z, w);
			}
		}
		{
			int NumElements;
			{
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> NumElements;
			}
			SurfaceElements.SetNum(NumElements);
			for (int i = 0; i < NumElements; ++i)
			{
				int x, y, z;
				std::string Input;
				std::stringstream ss;
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				if (!ensureMsgf(getline(File, Input, ','), TEXT("Failed to read tet file. (%s)"), *Filename))
				{
					return Collection;
				}
				ss << Input << " ";
				ss >> x;
				ss >> y;
				ss >> z;
				SurfaceElements[i] = FIntVector3(x, y, z);
			}
		}

		Collection.Reset(FFleshCollection::NewFleshCollection(Vertices, SurfaceElements, Elements));
		return Collection;
		

	}

}
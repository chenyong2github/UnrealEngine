// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosFlesh/PB.h"

#include "ChaosFlesh/ChaosFlesh.h"
#include "ChaosFlesh/FleshCollectionUtility.h" // for LogChaosFlesh
#include "ChaosFlesh/IFileStream.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"

#ifdef USE_ZLIB
#include "ChaosFlesh/ZIP.h"
#endif

#include "Misc/Paths.h"


#include "Trace/Trace.inl"
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sstream>

//============================================================================
// Helper functions
//============================================================================

std::shared_ptr<std::istream>
SafeOpenInput(const FString& filename, const bool binary = true)
{
#if !(UE_BUILD_SHIPPING && WITH_EDITOR)
	FPlatformFileManager& FileManager = FPlatformFileManager::Get();
	IPlatformFile& PlatformFile = FileManager.GetPlatformFile();
	if (!PlatformFile.FileExists(*filename))
	{
		return nullptr;
	}
	const bool compressed = filename.EndsWith(".gz");
	if (compressed || binary)
	{
#ifdef USE_ZLIB
		IFileHandle* infile = PlatformFile.OpenRead(*filename, false);
		return std::shared_ptr<std::istream>(new ZIP_FILE_ISTREAM(infile, false));
#else
		UE_LOG(LogChaosFlesh, Display, TEXT("ChaosFlesh not compiled with zlib support!"));
		return nullptr;
#endif
	}
	else
	{
		IFileHandle* infile = PlatformFile.OpenRead(*filename, false);
		return std::shared_ptr<std::istream>(new IFileStream(infile));
	}
#endif
	return nullptr;
}

//============================================================================
// Read
//============================================================================

template <class T>
void
Read(std::istream& InFile, T& x, T& y, T& z, T& w)
{
	std::string Input;
	std::stringstream ss;
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	ss >> x;
	ss >> y;
	ss >> z;
	ss >> w;
}

template <class T>
void
Read(std::istream& InFile, T& x, T& y, T& z)
{
	std::string Input;
	std::stringstream ss;
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	ss >> x;
	ss >> y;
	ss >> z;
}

template <class T>
void
Read(std::istream& InFile, T& x, T& y)
{
	std::string Input;
	std::stringstream ss;
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	ss >> x;
	ss >> y;
}

template <class T>
void
Read(std::istream& InFile, T& x)
{
	std::string Input;
	std::stringstream ss;
	check(std::getline(InFile, Input, ','));
	ss << Input << " ";
	ss >> x;
}

//============================================================================
// ReadBinary
//============================================================================

template <class T>
void
ReadBinary(std::istream& InFile, T& x)
{
	if (!InFile.eof())
		InFile.read(reinterpret_cast<char*>(&x), sizeof(T));
}

template <class T, int d>
void
ReadBinary(std::istream& InFile, Chaos::TVector<T, d>& Val)
{
	check(sizeof(Chaos::TVector<T, d>) == sizeof(T) * d);
	InFile.read(reinterpret_cast<char*>(&Val[0]), sizeof(T) * d);
}

template <class T, int d>
void
ReadBinary(std::istream& InFile, TArray<Chaos::TVector<T, d>>& Values)
{
	check(sizeof(Chaos::TVector<T, d>) == sizeof(T) * d);
	InFile.read(reinterpret_cast<char*>(&Values[0]), sizeof(T) * d * Values.Num());
}

// LWC safe double FReal specializations - read as floats
void
ReadBinary(std::istream& InFile, UE::Math::TVector<double>& Val)
{
	Chaos::TVector<float, 3> AsFloat;
	ReadBinary(InFile, AsFloat);
	Val[0] = AsFloat[0];
	Val[1] = AsFloat[1];
	Val[2] = AsFloat[2];
}

void
ReadBinary(std::istream& InFile, TArray<UE::Math::TVector<double>>& Values)
{
	// Per entry float -> double conversion
	for (int32 i = 0; i < Values.Num(); i++)
		ReadBinary(InFile, Values[i]);
}


template <class T>
void
ReadBinary(std::istream& InFile, TArray<T>& Values)
{
	InFile.read(reinterpret_cast<char*>(&Values[0]), sizeof(T) * Values.Num());
}

bool
ReadBinary(std::istream& InFile, std::string& str)
{
	int32 Size = -1;
	ReadBinary(InFile, Size);
	if (Size > 0 && Size < 1024 * 1000)
	{
		str.resize(Size);
		InFile.read(&str.front(), Size);
		return true;
	}
	else
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("ReadBinary: Invalid string length: %d"), Size);
		return false;
	}
}

bool
ReadBinary(std::istream& InFile, FString& str)
{
	int32 Size = -1;
	ReadBinary(InFile, Size);
	if (Size > 0 && Size < 1024 * 1000)
	{
		str.Reset(Size);
		InFile.read((char*) & str[0], Size);
		//InFile.Read(reinterpret_cast<uint8*>(&str[0]), Size);
		return true;
	}
	else
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("ReadBinary: Invalid string length: %d"), Size);
		return false;
	}
}


namespace ChaosFlesh {
namespace IO {

template <int d>
bool 
ReadStructure(const FString& filename, TArray<UE::Math::TVector<float>>& positions, TArray<Chaos::TVector<int32,d>>& mesh)
{
	std::shared_ptr<std::istream> InFile = SafeOpenInput(filename);
	if (!InFile)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open file: '%s'"), *filename);
		return false;
	}
	std::istream& InFileRef = *InFile;

	//
	// READ_WRITE_MESH_OBJECT implicitly forwards to READ_WRITE_SIMPLEX_MESH to read the mesh, then
	// reads the number of particles in the same line.
	//
	int NumberNodes = -2;               // Should be non-zero
	ReadBinary(InFileRef, NumberNodes);

	int BackwardsCompatible = -2;       // simplex dimension + 1 (3 = tri, 4 = tet)
	ReadBinary(InFileRef, BackwardsCompatible);
	if (BackwardsCompatible != d)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' opened as type '%s' but has simplex dimension %d not %d."), 
			*filename, (d == 4 ? "tet" : d == 3 ? "tri" : "curve"), BackwardsCompatible, d);
	}

	// READ_WRITE_ARRAY::Write_Prefix()
	int ArraySize = -1;                 // called "prefix"
	ReadBinary(InFileRef, ArraySize);
	if (ArraySize < 0)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', invalid array size: %d."),
			*filename, ArraySize);
		return false;
	}

	mesh.SetNum(ArraySize);
	for (int i = 0; i < ArraySize; i++)
		for(int j=0; j < d; j++)
		{
			int Idx = -1;
			ReadBinary(InFileRef, Idx);
			// Convert from 1 indexed to 0 indexed.
			mesh[i][j] = static_cast<int32>(Idx - 1);
		}

	int NumParticles = -1;
	ReadBinary(InFileRef, NumParticles);

	//return ReadPoints(InFile, filename, NumParticles, positions);
	positions.SetNum(NumParticles);
	for (int i = 0; i < NumParticles; i++)
		ReadBinary(InFileRef, positions[i]);
	return true;
}

template bool ReadStructure<2>(const FString&, TArray<UE::Math::TVector<float>>&, TArray<Chaos::TVector<int32, 2>>&);
template bool ReadStructure<3>(const FString&, TArray<UE::Math::TVector<float>>&, TArray<Chaos::TVector<int32, 3>>&);
template bool ReadStructure<4>(const FString&, TArray<UE::Math::TVector<float>>&, TArray<Chaos::TVector<int32, 4>>&);

DeformableGeometryCollectionReader::DeformableGeometryCollectionReader(
	const FString& BaseDir, 
	const FString& ColorFilePath, 
	const FString& ColorGeometryFilePath)
	: BaseDir(BaseDir)
	, CommonDir(BaseDir+"/common")
	, ColorFilePath(ColorFilePath)
	, ColorGeometryFilePath(ColorGeometryFilePath)
{}

DeformableGeometryCollectionReader::~DeformableGeometryCollectionReader()
{}

bool
DeformableGeometryCollectionReader::ReadPBScene()
{
	//FString ColorFilePath = ColorMap.FilePath;
	if (!FPaths::FileExists(ColorFilePath))
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM color map file not found: '%s'"), *ColorFilePath);
		// Don't fail.
	}
	else
	{
		// Read the color files to the PBVertexColors array, which if authored, will be used
		// at procedural mesh construction time for vertex colors.
		ReadColorFile(ColorFilePath);
	}

	//FString ColorGeometryFilePath = ColorMapGeometry.FilePath;
	if (!FPaths::FileExists(ColorFilePath))
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM color map geometry file not found: '%s'"), *ColorGeometryFilePath);
		// Don't fail.
	}
	else
	{
		// Read the color files to the PBVertexColors array, which if authored, will be used
		// at procedural mesh construction time for vertex colors.
		ReadColorGeometryFile(ColorGeometryFilePath);
	}

	//BaseDir = BaseDirectory.Path;
	if (!FPaths::DirectoryExists(BaseDir))
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM simulation base directory not found: '%s'"), *BaseDir);
		return false;
	}
	UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM simulation base directory: '%s'"), *BaseDir);
	CommonDir = BaseDir + TEXT("/common");
	if (!FPaths::DirectoryExists(CommonDir))
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM simulation common directory not found: '%s'"), *BaseDir);
		return false;
	}

	FString DefObjStructPath = CommonDir + TEXT("/deformable_object_structures");
	if (!FPaths::FileExists(DefObjStructPath))
	{
		DefObjStructPath = CommonDir + TEXT("/deformable_object_structures.gz");
		if (!FPaths::FileExists(DefObjStructPath))
		{
			UE_LOG(LogChaosFlesh, Display, TEXT("PhysBAM file not found: '%s'"), *DefObjStructPath);
			return false;
		}
	}
	if (!ReadDeformableGeometryCollection(DefObjStructPath))
		return false;

	const FString FaceControlPath = BaseDir + TEXT("/face_control_parameters_configuration.gz");
	if (FPaths::FileExists(FaceControlPath))
	{
		if (!ReadFaceControlParameters(FaceControlPath))
			return false;
	}

	return true;
}

TPair<int32, int32>
DeformableGeometryCollectionReader::ReadFrameRange()
{
	int32 FFrame = -std::numeric_limits<int32>::max();
	int32 LFrame = -std::numeric_limits<int32>::max();
	ReadFrameRange(CommonDir, FFrame, LFrame);
	return TPair<int32, int32>(FFrame, LFrame);
}

void
DeformableGeometryCollectionReader::ReadFrameRange(
	const FString& CommonDirIn,
	int32& InputFFrameIn,
	int32& InputLFrameIn) const
{
	if (InputFFrameIn == -std::numeric_limits<int32>::max())
	{
		const FString FFramePath = CommonDirIn + TEXT("/first_frame");
		if (FPaths::FileExists(FFramePath))
		{
			if (std::shared_ptr<std::istream> FFrameFile = SafeOpenInput(FFramePath, false))
			{
				float Tmp = -std::numeric_limits<float>::max();
				::Read<float>(*FFrameFile, Tmp); // Static analysis requires the fully qualified name
				if (Tmp >= 0)
				{
					InputFFrameIn = Tmp;
				}
			}
		}
	}

	// This file is frequently re-written as the sim progresses, so it's pretty common to get read errors!
	const FString LFramePath = CommonDirIn + TEXT("/last_frame");
	if (FPaths::FileExists(LFramePath))
	{
		if (std::shared_ptr<std::istream> LFrameFile = SafeOpenInput(LFramePath, false))
		{
			float Tmp = -std::numeric_limits<float>::max();
			::Read<float>(*LFrameFile, Tmp);  // Static analysis requires the fully qualified name
			if (Tmp >= InputFFrameIn)
			{
				InputLFrameIn = Tmp;
			}
		}
	}
}

int32
DeformableGeometryCollectionReader::ReadNumPoints(
	const int32 Frame,
	const FString& BaseDirIn) const
{
	const FString DefObjParticlesPath = FString::Printf(TEXT("%s/%d/deformable_object_particles.gz"), *BaseDirIn, Frame);
	if (FPaths::FileExists(DefObjParticlesPath))
	{
		if (std::shared_ptr<std::istream> InFile = SafeOpenInput(DefObjParticlesPath))
		{
			// READ_WRITE_POINT_CLOUD::Read()
			int32 Version = -1;
			ReadBinary(*InFile, Version);
			if (Version != 1)
				return 0;

			// READ_WRITE_ARRAY_COLLECTION::Read()
			int32 Size = -1;
			ReadBinary(*InFile, Size);
			return Size;
		}
	}
	return 0;
}

bool
DeformableGeometryCollectionReader::ReadPoints(
	const int32 Frame,
	TArray<UE::Math::TVector<float>>& Points)
{
	const FString DefObjParticlesPath = FString::Printf(TEXT("%s/%d/deformable_object_particles.gz"), *BaseDir, Frame);
	if (FPaths::FileExists(DefObjParticlesPath))
	{
		if (std::shared_ptr<std::istream> InFile = SafeOpenInput(DefObjParticlesPath))
		{
			// READ_WRITE_POINT_CLOUD::Read()
			int32 Version = -1;
			ReadBinary(*InFile, Version);
			if (Version != 1)
			{
				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Unrecognized particle version: %d"), *DefObjParticlesPath, Version);
				return false;
			}

			// READ_WRITE_ARRAY_COLLECTION::Read()
			int32 Size = -1;
			ReadBinary(*InFile, Size);
			if (Size <= 0)
			{
				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Invalid array collection size: %d"), *DefObjParticlesPath, Size);
				return false;
			}

			// READ_WRITE_ARRAY_COLLECTION::Read_Arrays()
			int32 SecondSize = -1;
			ReadBinary(*InFile, SecondSize);
			int32 NumAttributes = -1;
			ReadBinary(*InFile, NumAttributes);
			if (NumAttributes <= 0 || NumAttributes > 100)
			{
				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Invalid num attributes: %d"), *DefObjParticlesPath, NumAttributes);
				return false;
			}

			Points.SetNumUninitialized(Size);
			for (auto& Pt : Points)
				for (int32 i = 0; i < 3; i++)
				{
					Pt[i] = -std::numeric_limits<float>::max();
				}

			for (int32 i = 1; i <= NumAttributes; i++)
			{
				int32 AttributeId;
				ReadBinary(*InFile, AttributeId);
				AttributeId &= 0x0000FFFF;		// Type_Only(), strips lower bits

				// GEOMETRY_PARTICLES has X, ID, and maybe V

				if (AttributeId == 1) // ATTRIBUTE_ID_X
				{
					// READ_WRITE_ARRAY::Read()
					int32 ArraySize = -1;
					ReadBinary(*InFile, ArraySize);

					int32 ArraySize2 = -1;
					ReadBinary(*InFile, ArraySize2);

					//UE_LOG(LogChaosFlesh, Display, TEXT("Ryan - Size: %d, ArraySize: %d, ArraySize2: %d"),
					//	Size, ArraySize, ArraySize2);

					ReadBinary(*InFile, Points);

					// All we care about is X.  Return.
					return true;
				}
				else if (AttributeId == 2) // ATTRIBUTE_ID_V
				{
					// READ_WRITE_ARRAY::Read()
					int32 ArraySize = -1;
					ReadBinary(*InFile, ArraySize);
					if (ArraySize != Size)
					{
					}
					// We don't care about V, but it came first.  So just read it, 
					// and we'll write over it.
					ReadBinary(*InFile, Points);
				}
				else if (AttributeId == 20 || // ATTRIBUTE_ID_ID
					AttributeId == 6)		 // ATTRIBUTE_ID_STRUCTURE_IDS
				{
					// READ_WRITE_ARRAY::Read()
					int32 ArraySize = -1;
					ReadBinary(*InFile, ArraySize);
					if (ArraySize != Size)
					{
					}

					// Just read ID and then throw it away.
					TArray<int32> Id;
					Id.SetNumUninitialized(Size);
					ReadBinary(*InFile, Id);
				}
				else
				{
					UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Unsupported attribute id: %d"), *DefObjParticlesPath, AttributeId);
					return false;
				}
			}
		}
		else
		{
			UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open: '%s'"), *DefObjParticlesPath);
			return false;
		}
	}
	else
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("File not found: '%s'"), *DefObjParticlesPath);
		return false;
	}
	return false;
}

bool
DeformableGeometryCollectionReader::ReadDeformableGeometryCollection(const FString& FilePath)
{
	std::shared_ptr<std::istream> InFile = SafeOpenInput(FilePath);
	if (!InFile)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open file: '%s'"), *FilePath);
		return false;
	}

	// DEFORMABLE_GOMETRY_COLLECTION::Read_Static_Variables() uses TYPED_ISTREAM to switch between reading float and double.
	int32 NumStructures = -1;
	ReadBinary(*InFile, NumStructures);
	if (NumStructures <= 0)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Invalid num structures: %d"), *FilePath, NumStructures);
		return false;
	}
	UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s' - Reading num structures: %d"), *FilePath, NumStructures);

	for (int SIdx = 1; SIdx <= NumStructures; ++SIdx)
	{
		// READ_WRITE_STRUCTURE::Create_Structure()
		std::string TypeName;
		if (!ReadBinary(*InFile, TypeName))
		{
			return InputMeshes.Num() > 0;
		}

		// STRUCTURE::Create_From_Name() -> STRUCTURE_REGISTRY::Name_To_Factory()
		// STRUCTURE_REGISTRY is keyed by T_OBJECT::Static_Extension()
		//		tri = TRIANGUALTED_SURFACE
		//		tet = TETRAHEDRALIZED_VOLUME = SIMPLICIAL_OBJECT<T,VECTOR<T,3>,3>
		//		curve = SEGMENTED_CURVE
		// Geometry read routines forward to READ_WRITE_MESH_OBJECT.h

		if (TypeName == "SIMPLICIAL_OBJECT<T,VECTOR<T,3>,3>")
			TypeName = "tet";
		else if (TypeName == "SIMPLICIAL_OBJECT<T,VECTOR<T,3>,2>")
			TypeName = "tri";
		else if (TypeName == "SIMPLICIAL_OBJECT<T,VECTOR<T,3>,1>")
			TypeName = "curve";

		if (TypeName != "tet" && TypeName != "tri")
		{
			FString TmpStr(TypeName.c_str());
			UE_LOG(LogChaosFlesh, Display, TEXT("Unsupported PhysBAM structure type: '%s'"), *TmpStr);
		}
		else
		{
			//
			// READ_WRITE_MESH_OBJECT implicitly forwards to READ_WRITE_SIMPLEX_MESH to read the mesh, then
			// reads the number of particles in the same line.
			//
			int NumberNodes = -1;				// Should be non-zero
			ReadBinary(*InFile, NumberNodes);

			int BackwardsCompatible = -1;		// simplex dimension + 1 (3 = tri, 4 = tet)
			ReadBinary(*InFile, BackwardsCompatible);

			// READ_WRITE_ARRAY::Write_Prefix()
			int ArraySize = -1;					// called "prefix" 
			ReadBinary(*InFile, ArraySize);
			if (ArraySize < 0)
			{
				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', structure: %d of %d - Invalid array size: %d"),
					*FilePath, SIdx, NumStructures, ArraySize);
				return InputMeshes.Num() > 0;
			}

			Mesh* mesh = nullptr;
			int32 MinIdx = std::numeric_limits<int32>::max();
			int32 MaxIdx = -std::numeric_limits<int32>::max();
			int32 NumTriangles = 0;
			if (TypeName == "tet")
			{
				TetMesh* tetMesh = new TetMesh();
				mesh = static_cast<Mesh*>(tetMesh);
				InputMeshes.Push(mesh);
				TetMeshes.Push(tetMesh);

				tetMesh->Elements.SetNum(ArraySize);
				for (auto& Elem : tetMesh->Elements)
					for (int i = 0; i < 4; i++)
						Elem[i] = -std::numeric_limits<int32>::max();
				ReadBinary(*InFile, tetMesh->Elements);

				size_t BytesToEOF = 0;
				while (!InFile->eof() && !InFile->fail())
				{
					char ch;
					InFile->read(&ch, 1);
					BytesToEOF++;
				}
				UE_LOG(LogChaosFlesh, Display, TEXT("Done reading: '%s' (%d bytes remaining)"),
					*FilePath, BytesToEOF);

				// Convert Elements from 1 indexed to 0 indexed
				int32 ElemIdx = -1;
				for (auto& Elem : tetMesh->Elements)
				{
					ElemIdx++;
					for (int i = 0; i < 4; i++)
					{
						if (Elem[i] == -std::numeric_limits<int32>::max() || Elem[i] <= 0 || Elem[i] > NumberNodes)
						{
							UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', structure: %d of %d - File read error - elem %d of %d, index %d, coordinate value: %d"),
								*FilePath, ElemIdx, tetMesh->Elements.Num(), i, Elem[i]);
							continue;
						}
						Elem[i]--;
						check(Elem[i] >= 0);
					}
				}

				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', structure: %d of %d - Num elements: %d"),
					*FilePath, SIdx, NumStructures, ArraySize);

				for (auto& Elem : tetMesh->Elements)
				{
					for (int i = 0; i < 4; i++)
					{
						const int32 Idx = Elem[i];
						MinIdx = Idx < MinIdx ? Idx : MinIdx;
						MaxIdx = Idx > MaxIdx ? Idx : MaxIdx;
					}
				}
			}
			else //if(TypeName == "tri")
			{
				TriMesh* triMesh = new TriMesh();
				mesh = static_cast<Mesh*>(triMesh);
				InputMeshes.Push(mesh);
				TriMeshes.Push(triMesh);

				triMesh->SurfaceElements.SetNum(ArraySize);
				ReadBinary(*InFile, triMesh->SurfaceElements);

				// Convert Elements from 1 indexed to 0 indexed, and reverse orientation
				for (auto& Elem : triMesh->SurfaceElements)
				{
					std::swap(Elem[0], Elem[1]);
					for (int i = 0; i < 3; i++)
						Elem[i]--;
				}

				UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', structure: %d of %d - Num elements: %d"),
					*FilePath, SIdx, NumStructures, ArraySize);

				NumTriangles = triMesh->SurfaceElements.Num();
				for (auto& Elem : triMesh->SurfaceElements)
				{
					for (int i = 0; i < 3; i++)
					{
						const int32 Idx = Elem[i];
						MinIdx = Idx < MinIdx ? Idx : MinIdx;
						MaxIdx = Idx > MaxIdx ? Idx : MaxIdx;
					}
				}
			}

			//
			// Back to READ_WRITE_MESH_OBJECT::Read_Helper(), read points
			//

			int NumPoints = 0;
			ReadBinary(*InFile, NumPoints);
			if (NumPoints > 0)
			{
				mesh->Points.SetNum(NumPoints);
				ReadBinary(*InFile, mesh->Points);
			}

			UE_LOG(LogChaosFlesh, Display,
				TEXT("Read file '%s' structure %d of %d:\n"
					"    type: %s\n"
					"    num elements: %d\n"
					"    num triangles: %d\n"
					"    min/max point index: %d, %d\n"
					"    num nodes: %d\n"
					"    num points: %d"),
				*FilePath, SIdx, NumStructures,
				*FString(TypeName.c_str()),
				ArraySize,
				NumTriangles,
				MinIdx, MaxIdx,
				NumberNodes,
				NumPoints);
		}
	}
	return true;
}

bool
DeformableGeometryCollectionReader::ReadFaceControlParameters(const FString& FilePath)
{
	// See FACE_CONTROL_PARAMETERS::Read_Configuration_From_File()
	return true;
}

bool
DeformableGeometryCollectionReader::ReadColorFile(const FString& FilePath)
{
	std::shared_ptr<std::istream> InFile = SafeOpenInput(FilePath);
	if (!InFile)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open file: '%s'"), *FilePath);
		return false;
	}

	int32 Size = 0;
	ReadBinary(*InFile, Size);

	PBVertexColors.AddUninitialized(Size);
	ReadBinary(*InFile, PBVertexColors);

	Chaos::TVector<float, 3> MinPt(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	Chaos::TVector<float, 3> MaxPt(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
	for (const auto& Pt : PBVertexColors)
	{
		for (int j = 0; j < 3; j++)
		{
			if (Pt[j] < MinPt[j])
				MinPt[j] = Pt[j];
			if (Pt[j] > MaxPt[j])
				MaxPt[j] = Pt[j];
		}
	}

	UE_LOG(LogChaosFlesh, Display,
		TEXT("File: '%s' - Read %d vertex colors:\n"
			"    R range: %.6f, %.6f\n"
			"    G range: %.6f, %.6f\n"
			"    B range: %.6f, %.6f"),
		*FilePath, Size,
		MinPt[0], MaxPt[0],
		MinPt[1], MaxPt[1],
		MinPt[2], MaxPt[2]);
	return true;
}

bool
DeformableGeometryCollectionReader::ReadColorGeometryFile(const FString& FilePath)
{
	std::shared_ptr<std::istream> InFile = SafeOpenInput(FilePath);
	if (!InFile)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("Failed to open file: '%s'"), *FilePath);
		return false;
	}

	int32 NumNodes;
	ReadBinary(*InFile, NumNodes);

	int32 BackwardsCompatible; // simplex dimension + 1 (3 = tri, 4 = tet)
	ReadBinary(*InFile, BackwardsCompatible);

	if (BackwardsCompatible != 3)
	{
		UE_LOG(LogChaosFlesh, Display, TEXT("File: '%s', expected type 3 (tri), got %d"), *FilePath, BackwardsCompatible);
		return false;
	}

	int32 NumElements;
	ReadBinary(*InFile, NumElements);

	PBVertexColorsTriSurf.SurfaceElements.SetNumUninitialized(NumElements);
	ReadBinary(*InFile, PBVertexColorsTriSurf.SurfaceElements);

	int32 NumParticles;
	ReadBinary(*InFile, NumParticles);

	PBVertexColorsTriSurf.Points.SetNumUninitialized(NumParticles);
	ReadBinary(*InFile, PBVertexColorsTriSurf.Points);

	// Convert Elements from 1 indexed to 0 indexed
	for (auto& Elem : PBVertexColorsTriSurf.SurfaceElements)
	{
		for (int i = 0; i < 3; i++)
			Elem[i]--;
	}

	int32 MinIdx = std::numeric_limits<int32>::max();
	int32 MaxIdx = -std::numeric_limits<int32>::max();
	for (auto& Elem : PBVertexColorsTriSurf.SurfaceElements)
	{
		for (int i = 0; i < 3; i++)
		{
			const int32 Idx = Elem[i];
			MinIdx = Idx < MinIdx ? Idx : MinIdx;
			MaxIdx = Idx > MaxIdx ? Idx : MaxIdx;
		}
	}

	Chaos::TVector<float,3> MinPt(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	Chaos::TVector<float,3> MaxPt(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
	for (const auto& Pt : PBVertexColorsTriSurf.Points)
	{
		for (int j = 0; j < 3; j++)
		{
			if (Pt[j] < MinPt[j])
				MinPt[j] = Pt[j];
			if (Pt[j] > MaxPt[j])
				MaxPt[j] = Pt[j];
		}
	}

	UE_LOG(LogChaosFlesh, Display,
		TEXT("Read color geometry file: '%s':\n"
			"    num triangles: %d\n"
			"    min/max point index: %d, %d\n"
			"    num nodes: %d\n"
			"    num points: %d\n"
			"    domain: (%.2f, %.2f, %.2f)x(%.2f, %.2f, %.2f)"),
		*FilePath,
		NumElements,
		MinIdx, MaxIdx,
		NumNodes,
		NumParticles,
		MinPt[0], MinPt[1], MinPt[2],
		MaxPt[0], MaxPt[1], MaxPt[2]);
	return true;
}



} // namespace IO
} // namespace ChaosFlesh

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ShaderPipelineCacheToolsCommandlet.h"
#include "Misc/Paths.h"

#include "Algo/Accumulate.h"
#include "Async/ParallelFor.h"
#include "PipelineFileCache.h"
#include "ShaderCodeLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "ShaderPipelineCache.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "String/ParseLines.h"

DEFINE_LOG_CATEGORY_STATIC(LogShaderPipelineCacheTools, Log, All);

const TCHAR* STABLE_CSV_EXT = TEXT("stablepc.csv");
const TCHAR* STABLE_CSV_COMPRESSED_EXT = TEXT("stablepc.csv.compressed");
const TCHAR* STABLE_COMPRESSED_EXT = TEXT(".compressed");
const int32  STABLE_COMPRESSED_EXT_LEN = 11; // len of ".compressed";
const int32  STABLE_COMPRESSED_VER = 1;


void ExpandWildcards(TArray<FString>& Parts)
{
	TArray<FString> NewParts;
	for (const FString& OldPart : Parts)
	{
		if (OldPart.Contains(TEXT("*")) || OldPart.Contains(TEXT("?")))
		{
			FString CleanPath = FPaths::GetPath(OldPart);
			FString CleanFilename = FPaths::GetCleanFilename(OldPart);
			
			TArray<FString> ExpandedFiles;
			IFileManager::Get().FindFilesRecursive(ExpandedFiles, *CleanPath, *CleanFilename, true, false);
			
			if (CleanFilename.EndsWith(STABLE_CSV_EXT))
			{
				// look for stablepc.csv.compressed as well
				CleanFilename.Append(STABLE_COMPRESSED_EXT);
				IFileManager::Get().FindFilesRecursive(ExpandedFiles, *CleanPath, *CleanFilename, true, false, false);
			}
			
			UE_CLOG(!ExpandedFiles.Num(), LogShaderPipelineCacheTools, Warning, TEXT("Expanding %s....did not match anything."), *OldPart);
			UE_CLOG(ExpandedFiles.Num(), LogShaderPipelineCacheTools, Log, TEXT("Expanding matched %4d files: %s"), ExpandedFiles.Num(), *OldPart);
			for (const FString& Item : ExpandedFiles)
			{
				UE_LOG(LogShaderPipelineCacheTools, Log, TEXT("                             : %s"), *Item);
				NewParts.Add(Item);
			}
		}
		else
		{
			NewParts.Add(OldPart);
		}
	}
	Parts = NewParts;
}

static void LoadStableSCL(TArray<FStableShaderKeyAndValue>& StableArray, const FStringView& FileName)
{
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loading %.*s..."), FileName.Len(), FileName.GetData());

	FString SourceFileContents;
	TArray<FStringView> SourceFileLines;
	if (FFileHelper::LoadFileToString(SourceFileContents, *FString(FileName)))
	{
		UE::String::ParseLines(SourceFileContents, [&SourceFileLines](FStringView Line) { if (!Line.IsEmpty()) { SourceFileLines.Add(Line); } });
	}

	if (SourceFileLines.Num() < 1)
	{
		UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not load %.*s..."), FileName.Len(), FileName.GetData());
		return;
	}

	const int32 StableArrayOffset = StableArray.Num();
	StableArray.AddZeroed(SourceFileLines.Num() - 1);
	ParallelFor(SourceFileLines.Num() - 1, [&StableArray, &SourceFileLines, StableArrayOffset](int32 Index)
	{
		FStableShaderKeyAndValue& Item = StableArray[StableArrayOffset + Index];
		Item.ParseFromString(SourceFileLines[Index + 1]);
		check(!(Item.OutputHash == FSHAHash()));
	});

	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d shader info lines from %.*s."), StableArray.Num(), FileName.Len(), FileName.GetData());
}

static void LoadStableSCLs(TMultiMap<FStableShaderKeyAndValue, FSHAHash>& StableMap, TArrayView<const FStringView> FileNames)
{
	TArray<TArray<FStableShaderKeyAndValue>> StableArrays;
	StableArrays.AddDefaulted(FileNames.Num());
	ParallelFor(FileNames.Num(), [&StableArrays, &FileNames](int32 Index) { LoadStableSCL(StableArrays[Index], FileNames[Index]); });

	const int32 StableArrayCount = Algo::TransformAccumulate(StableArrays, &TArray<FStableShaderKeyAndValue>::Num, 0);
	StableMap.Reserve(StableMap.Num() + StableArrayCount);
	for (const TArray<FStableShaderKeyAndValue>& StableArray : StableArrays)
	{
		for (const FStableShaderKeyAndValue& Item : StableArray)
		{
			StableMap.AddUnique(Item, Item.OutputHash);
		}
	}
}

static bool LoadAndDecompressStableCSV(const FString& Filename, TArray<uint8>& UncompressedData)
{
	bool bResult = false;
	FArchive* Ar = IFileManager::Get().CreateFileReader(*Filename);
	if (Ar)
	{
		if (Ar->TotalSize() > 8)
		{
			int32 CompressedVersion = 0;
			int32 UncompressedSize = 0;
			int32 CompressedSize = 0;
			
			Ar->Serialize(&CompressedVersion, sizeof(int32));
			Ar->Serialize(&UncompressedSize, sizeof(int32));
			Ar->Serialize(&CompressedSize, sizeof(int32));
			
			TArray<uint8> CompressedData;
			CompressedData.SetNumUninitialized(CompressedSize);
			Ar->Serialize(CompressedData.GetData(), CompressedSize);

			UncompressedData.SetNumUninitialized(UncompressedSize);
			bResult = FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize);
			if (!bResult)
			{
				UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Failed to decompress file %s"), *Filename);
			}
		}
		else
		{
			UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Corrupted file %s"), *Filename);
		}
	
		delete Ar;
	}
	else
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Failed to open file %s"), *Filename);
	}

	return bResult;
}

struct FRawStableCSV
{
	TArray<uint8> SerializedData;
	FString CSV;
};

static bool LoadStableCSV(const FString& FileName, FRawStableCSV& RawStableCSV)
{
	if (FileName.EndsWith(STABLE_CSV_COMPRESSED_EXT))
	{
		return LoadAndDecompressStableCSV(FileName, RawStableCSV.SerializedData);
	}
	else
	{
		return FFileHelper::LoadFileToString(RawStableCSV.CSV, *FileName);
	}
}

static void ReadStableCSV(const FRawStableCSV& RawStableCSV, const TFunctionRef<void(FStringView)>& LineVisitor)
{
	if (RawStableCSV.SerializedData.Num())
	{
		FMemoryReader MemArchive(RawStableCSV.SerializedData);
		FString LineCSV;
		while (!MemArchive.AtEnd())
		{
			MemArchive << LineCSV;
			LineVisitor(LineCSV);
		}
	}
	else
	{
		UE::String::ParseLines(RawStableCSV.CSV, LineVisitor);
	}
}

static bool LoadStableCSV(const FString& FileName, TArray<FString>& OutputLines)
{
	FRawStableCSV RawStableCSV;
	if (LoadStableCSV(FileName, RawStableCSV))
	{
		ReadStableCSV(RawStableCSV, [&OutputLines](FStringView Line) { OutputLines.Emplace(Line); });
		return true;
	}
	return false;
}

static int64 SaveStableCSV(const FString& Filename, const TArray<uint8>& UncompressedData)
{
	if (Filename.EndsWith(STABLE_CSV_COMPRESSED_EXT))
	{
		int32 UncompressedSize = UncompressedData.Num();
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Compressing output, size = %.1fKB"), UncompressedSize/1024.f);
		int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, UncompressedSize);
		TArray<uint8> CompressedData;
		CompressedData.SetNumZeroed(CompressedSize);

		if (FCompression::CompressMemory(NAME_Zlib, CompressedData.GetData(), CompressedSize, UncompressedData.GetData(), UncompressedSize))
		{
			FArchive* Ar = IFileManager::Get().CreateFileWriter(*Filename);
			if (!Ar)
			{
				UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Failed to open %s"), *Filename);
				return -1;
			}
			
			int32 CompressedVersion = STABLE_COMPRESSED_VER;
			
			Ar->Serialize(&CompressedVersion, sizeof(int32));
			Ar->Serialize(&UncompressedSize, sizeof(int32));
			Ar->Serialize(&CompressedSize, sizeof(int32));
			Ar->Serialize(CompressedData.GetData(), CompressedSize);
			delete Ar;
		}
		else
		{
			UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Failed to compress (%.1f KB)"), UncompressedSize/1024.f);
			return -1;
		}
	}
	else
	{
		FMemoryReader MemArchive(UncompressedData);
		FString CombinedCSV;
		FString LineCSV;
		while (!MemArchive.AtEnd())
		{
			MemArchive << LineCSV;
			CombinedCSV.Append(LineCSV);
			CombinedCSV.Append(LINE_TERMINATOR);
		}

		FFileHelper::SaveStringToFile(CombinedCSV, *Filename);
	}
	
	int64 Size = IFileManager::Get().FileSize(*Filename);
	if (Size < 1)
	{
		UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Failed to write %s"), *Filename);
	}

	return Size;
}

static void PrintShaders(const TMap<FSHAHash, TArray<FString>>& InverseMap, const FSHAHash& Shader)
{
	if (Shader == FSHAHash())
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    null"));
		return;
	}
	const TArray<FString>* Out = InverseMap.Find(Shader);
	if (!Out)
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    No shaders found with hash %s"), *Shader.ToString());
		return;
	}

	for (const FString& Item : *Out)
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    %s"), *Item);
	}
}

bool CheckPSOStringInveribility(const FPipelineCacheFileFormatPSO& Item)
{
	FPipelineCacheFileFormatPSO TempItem(Item);
	TempItem.Hash = 0;

	FString StringRep;
	if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
	{
		StringRep = TempItem.ComputeDesc.ToString();
	}
	else
	{
		StringRep = TempItem.GraphicsDesc.ToString();
	}
	FPipelineCacheFileFormatPSO DupItem;
	FMemory::Memzero(DupItem.GraphicsDesc);
	DupItem.Type = Item.Type;
	DupItem.UsageMask = Item.UsageMask;
	if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
	{
		DupItem.ComputeDesc.FromString(StringRep);
	}
	else
	{
		DupItem.GraphicsDesc.FromString(StringRep);
	}
	UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("CheckPSOStringInveribility: %s"), *StringRep);

	return (DupItem == TempItem) && (GetTypeHash(DupItem) == GetTypeHash(TempItem));
}

int32 DumpPSOSC(FString& Token)
{
	TSet<FPipelineCacheFileFormatPSO> PSOs;

	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loading %s...."), *Token);
	if (!FPipelineFileCache::LoadPipelineFileCacheInto(Token, PSOs))
	{
		UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("Could not load %s or it was empty."), *Token);
		return 1;
	}

	for (const FPipelineCacheFileFormatPSO& Item : PSOs)
	{
		FString StringRep;
		if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
		{
			check(!(Item.ComputeDesc.ComputeShader == FSHAHash()));
			StringRep = Item.ComputeDesc.ToString();
		}
		else
		{
			check(!(Item.GraphicsDesc.VertexShader == FSHAHash()));
			StringRep = Item.GraphicsDesc.ToString();
		}
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("%s"), *StringRep);
	}
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("%s"), *FPipelineCacheFileFormatPSO::GraphicsDescriptor::HeaderLine());

	for (const FPipelineCacheFileFormatPSO& Item : PSOs)
	{
		CheckPSOStringInveribility(Item);
	}

	return 0;
}

static void PrintShaders(const TMap<FSHAHash, TArray<FStableShaderKeyAndValue>>& InverseMap, const FSHAHash& Shader, const TCHAR *Label)
{
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT(" -- %s"), Label);

	if (Shader == FSHAHash())
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    null"));
		return;
	}
	const TArray<FStableShaderKeyAndValue>* Out = InverseMap.Find(Shader);
	if (!Out)
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    No shaders found with hash %s"), *Shader.ToString());
		return;
	}
	for (const FStableShaderKeyAndValue& Item : *Out)
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    %s"), *Item.ToString());
	}
}

static bool GetStableShadersAndZeroHash(const TMap<FSHAHash, TArray<FStableShaderKeyAndValue>>& InverseMap, const FSHAHash& Shader, TArray<FStableShaderKeyAndValue>& StableShaders, bool& bOutAnyActiveButMissing)
{
	if (Shader == FSHAHash())
	{
		return false;
	}
	const TArray<FStableShaderKeyAndValue>* Out = InverseMap.Find(Shader);
	if (!Out)
	{
		UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("No shaders found with hash %s"), *Shader.ToString());
		UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("If you can find the old .scl.csv file for this build, adding it will allow these PSOs to be usable."));
		bOutAnyActiveButMissing = true;
		return false;
	}
	StableShaders.Reserve(Out->Num());
	for (const FStableShaderKeyAndValue& Item : *Out)
	{
		FStableShaderKeyAndValue Temp = Item;
		Temp.OutputHash = FSHAHash();
		if (StableShaders.Contains(Temp))
		{
			UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("Duplicate stable shader. This is bad because it means our stable key is not exhaustive."));
			UE_LOG(LogShaderPipelineCacheTools, Error, TEXT(" %s"), *Item.ToString());
			continue;
		}
		StableShaders.Add(Temp);
	}
	return true;
}

static void StableShadersSerializationSelfTest(const TMultiMap<FStableShaderKeyAndValue, FSHAHash>& StableMap)
{
	TAnsiStringBuilder<384> TestString;
	for (const auto& Pair : StableMap)
	{
		TestString.Reset();
		FStableShaderKeyAndValue Item(Pair.Key);
		Item.OutputHash = Pair.Value;
		check(Pair.Value != FSHAHash());
		Item.AppendString(TestString);
		FStableShaderKeyAndValue TestItem;
		TestItem.ParseFromString(UTF8_TO_TCHAR(TestString.ToString()));
		check(Item == TestItem);
		check(GetTypeHash(Item) == GetTypeHash(TestItem));
		check(Item.OutputHash == TestItem.OutputHash);
	}
}

// return true if these two shaders could be part of the same stable PSO
// for example, if they come from two different vertex factories, we return false because that situation cannot occur
bool CouldBeUsedTogether(const FStableShaderKeyAndValue& A, const FStableShaderKeyAndValue& B)
{
	static FName NAME_FDeferredDecalVS("FDeferredDecalVS");
	static FName NAME_FWriteToSliceVS("FWriteToSliceVS");
	static FName NAME_FPostProcessVS("FPostProcessVS");
	if (
		A.ShaderType == NAME_FDeferredDecalVS || B.ShaderType == NAME_FDeferredDecalVS ||
		A.ShaderType == NAME_FWriteToSliceVS || B.ShaderType == NAME_FWriteToSliceVS ||
		A.ShaderType == NAME_FPostProcessVS || B.ShaderType == NAME_FPostProcessVS
		)
	{
		// oddball mix and match with any material shader.
		return true;
	}
	if (A.ShaderClass != B.ShaderClass)
	{
		return false;
	}
	if (A.VFType != B.VFType)
	{
		return false;
	}
	if (A.FeatureLevel != B.FeatureLevel)
	{
		return false;
	}
	if (A.QualityLevel != B.QualityLevel)
	{
		return false;
	}
	if (A.TargetPlatform != B.TargetPlatform)
	{
		return false;
	}
	if (!(A.ClassNameAndObjectPath == B.ClassNameAndObjectPath))
	{
		return false;
	}
	return true;
}

int32 DumpSCLCSV(const FString& Token)
{
	const FStringView File = Token;
	TMultiMap<FStableShaderKeyAndValue, FSHAHash> StableMap;
	LoadStableSCLs(StableMap, MakeArrayView(&File, 1));
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    %s"), *FStableShaderKeyAndValue::HeaderLine());
	for (const auto& Pair : StableMap)
	{
		FStableShaderKeyAndValue Temp(Pair.Key);
		Temp.OutputHash = Pair.Value;
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    %s"), *Temp.ToString());
	}
	return 0;
}

void IntersectSets(TSet<FCompactFullName>& Intersect, const TSet<FCompactFullName>& ShaderAssets)
{
	if (!Intersect.Num() && ShaderAssets.Num())
	{
		Intersect = ShaderAssets;
	}
	else if (Intersect.Num() && ShaderAssets.Num())
	{
		Intersect  = Intersect.Intersect(ShaderAssets);
	}
}

struct FPermuation
{
	FStableShaderKeyAndValue Slots[SF_NumFrequencies];
};

void GeneratePermuations(TArray<FPermuation>& Permutations, FPermuation& WorkingPerm, int32 SlotIndex , const TArray<FStableShaderKeyAndValue> StableShadersPerSlot[SF_NumFrequencies], const bool ActivePerSlot[SF_NumFrequencies])
{
	check(SlotIndex >= 0 && SlotIndex <= SF_NumFrequencies);
	while (SlotIndex < SF_NumFrequencies && !ActivePerSlot[SlotIndex])
	{
		SlotIndex++;
	}
	if (SlotIndex >= SF_NumFrequencies)
	{
		Permutations.Add(WorkingPerm);
		return;
	}
	for (int32 StableIndex = 0; StableIndex < StableShadersPerSlot[SlotIndex].Num(); StableIndex++)
	{
		bool bKeep = true;
		// check compatibility with shaders in the working perm
		for (int32 SlotIndexInner = 0; SlotIndexInner < SlotIndex; SlotIndexInner++)
		{
			if (SlotIndex == SlotIndexInner || !ActivePerSlot[SlotIndexInner])
			{
				continue;
			}
			check(SlotIndex != SF_Compute && SlotIndexInner != SF_Compute); // there is never any matching with compute shaders
			if (!CouldBeUsedTogether(StableShadersPerSlot[SlotIndex][StableIndex], WorkingPerm.Slots[SlotIndexInner]))
			{
				bKeep = false;
				break;
			}
		}
		if (!bKeep)
		{
			continue;
		}
		WorkingPerm.Slots[SlotIndex] = StableShadersPerSlot[SlotIndex][StableIndex];
		GeneratePermuations(Permutations, WorkingPerm, SlotIndex + 1, StableShadersPerSlot, ActivePerSlot);
	}
}

int32 ExpandPSOSC(const TArray<FString>& Tokens)
{
	check(Tokens.Last().EndsWith(STABLE_CSV_EXT) || Tokens.Last().EndsWith(STABLE_CSV_COMPRESSED_EXT));

	TArray<FStringView, TInlineAllocator<16>> StableCSVs;
	for (int32 Index = 0; Index < Tokens.Num() - 1; Index++)
	{
		if (Tokens[Index].EndsWith(TEXT(".scl.csv")))
		{
			StableCSVs.Add(Tokens[Index]);
		}
	}

	TMultiMap<FStableShaderKeyAndValue, FSHAHash> StableMap;
	LoadStableSCLs(StableMap, StableCSVs);
	if (!StableMap.Num())
	{
		UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("No .scl.csv found or they were all empty. Nothing to do."));
		return 0;
	}
	if (UE_LOG_ACTIVE(LogShaderPipelineCacheTools, Verbose))
	{
		UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("    %s"), *FStableShaderKeyAndValue::HeaderLine());
		for (const auto& Pair : StableMap)
		{
			FStableShaderKeyAndValue Temp(Pair.Key);
			Temp.OutputHash = Pair.Value;
			UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("    %s"), *Temp.ToString());
		}
		StableShadersSerializationSelfTest(StableMap);
	}
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d unique shader info lines total."), StableMap.Num());

	TSet<FPipelineCacheFileFormatPSO> PSOs;
	
	uint32 MergeCount = 0;

	for (int32 Index = 0; Index < Tokens.Num() - 1; Index++)
	{
		if (Tokens[Index].EndsWith(TEXT(".upipelinecache")))
		{
			UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loading %s...."), *Tokens[Index]);
			TSet<FPipelineCacheFileFormatPSO> TempPSOs;
			if (!FPipelineFileCache::LoadPipelineFileCacheInto(Tokens[Index], TempPSOs))
			{
				UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("Could not load %s or it was empty."), *Tokens[Index]);
				continue;
			}
			UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d PSOs"), TempPSOs.Num());

			// We need to merge otherwise we'll lose usage masks on exact same PSO but in different files
			for(auto& TempPSO : TempPSOs)
			{
				auto* ExistingPSO = PSOs.Find(TempPSO);
				if(ExistingPSO != nullptr)
				{
					// Existing PSO must have already gone through verify and invertibility checks
					check(*ExistingPSO == TempPSO);
					
					// Get More accurate stats by testing for diff - we could just merge and be done
					if((ExistingPSO->UsageMask & TempPSO.UsageMask) != TempPSO.UsageMask)
					{
						ExistingPSO->UsageMask |= TempPSO.UsageMask;
						++MergeCount;
					}
					// Raw data files are not bind count averaged - just ensure we have captured max value
					ExistingPSO->BindCount = FMath::Max(ExistingPSO->BindCount, TempPSO.BindCount);
				}
				else
				{
					bool bInvertibilityResult = CheckPSOStringInveribility(TempPSO);
					bool bVerifyResult = TempPSO.Verify();
					if(bInvertibilityResult && bVerifyResult)
					{
						PSOs.Add(TempPSO);
					}
					else
					{
						// Log Found Bad PSO, this is in the context of the logged current file above so we can see where this has come from
						UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Bad PSO found discarding [Invertibility=%s Verify=%s in: %s]"), bInvertibilityResult ? TEXT("PASS") :  TEXT("FAIL") , bVerifyResult ? TEXT("PASS") :  TEXT("FAIL"), *Tokens[Index]);
					}
				}
			}
		}
		else
		{
			check(Tokens[Index].EndsWith(TEXT(".scl.csv")));
		}
	}
	if (!PSOs.Num())
	{
		UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("No .upipelinecache files found or they were all empty. Nothing to do."));
		return 0;
	}
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d PSOs total [Usage Mask Merged = %d]."), PSOs.Num(), MergeCount);

	if (UE_LOG_ACTIVE(LogShaderPipelineCacheTools, Verbose))
	{
		TMap<FSHAHash, TArray<FString>> InverseMap;

		for (const auto& Pair : StableMap)
		{
			FStableShaderKeyAndValue Temp(Pair.Key);
			Temp.OutputHash = Pair.Value;
			InverseMap.FindOrAdd(Pair.Value).Add(Temp.ToString());
		}

		for (const FPipelineCacheFileFormatPSO& Item : PSOs)
		{
			if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("ComputeShader"));
				PrintShaders(InverseMap, Item.ComputeDesc.ComputeShader);
			}
			else
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("VertexShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.VertexShader);
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("FragmentShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.FragmentShader);
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("GeometryShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.GeometryShader);
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("HullShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.HullShader);
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("DomainShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.DomainShader);
			}
		}
	}
	TMap<FSHAHash, TArray<FStableShaderKeyAndValue>> InverseMap;

	for (const auto& Pair : StableMap)
	{
		FStableShaderKeyAndValue Item(Pair.Key);
		Item.OutputHash = Pair.Value;
		InverseMap.FindOrAdd(Item.OutputHash).AddUnique(Item);
	}

	int32 TotalStablePSOs = 0;

	struct FPermsPerPSO
	{
		const FPipelineCacheFileFormatPSO* PSO;
		bool ActivePerSlot[SF_NumFrequencies];
		TArray<FPermuation> Permutations;

		FPermsPerPSO()
			: PSO(nullptr)
		{
			for (int32 Index = 0; Index < SF_NumFrequencies; Index++)
			{
				ActivePerSlot[Index] = false;
			}
		}
	};

	TArray<FPermsPerPSO> StableResults;
	StableResults.Reserve(PSOs.Num());
	int32 NumSkipped = 0;
	int32 NumExamined = 0;

	for (const FPipelineCacheFileFormatPSO& Item : PSOs)
	{ 
		NumExamined++;
		check(SF_Vertex == 0 && SF_Compute == 5);
		TArray<FStableShaderKeyAndValue> StableShadersPerSlot[SF_NumFrequencies];
		bool ActivePerSlot[SF_NumFrequencies] = { false };

		bool OutAnyActiveButMissing = false;

		if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
		{
			ActivePerSlot[SF_Compute] = GetStableShadersAndZeroHash(InverseMap, Item.ComputeDesc.ComputeShader, StableShadersPerSlot[SF_Compute], OutAnyActiveButMissing);
		}
		else
		{
			ActivePerSlot[SF_Vertex] = GetStableShadersAndZeroHash(InverseMap, Item.GraphicsDesc.VertexShader, StableShadersPerSlot[SF_Vertex], OutAnyActiveButMissing);
			ActivePerSlot[SF_Pixel] = GetStableShadersAndZeroHash(InverseMap, Item.GraphicsDesc.FragmentShader, StableShadersPerSlot[SF_Pixel], OutAnyActiveButMissing);
			ActivePerSlot[SF_Geometry] = GetStableShadersAndZeroHash(InverseMap, Item.GraphicsDesc.GeometryShader, StableShadersPerSlot[SF_Geometry], OutAnyActiveButMissing);
			ActivePerSlot[SF_Hull] = GetStableShadersAndZeroHash(InverseMap, Item.GraphicsDesc.HullShader, StableShadersPerSlot[SF_Hull], OutAnyActiveButMissing);
			ActivePerSlot[SF_Domain] = GetStableShadersAndZeroHash(InverseMap, Item.GraphicsDesc.DomainShader, StableShadersPerSlot[SF_Domain], OutAnyActiveButMissing);
		}


		if (OutAnyActiveButMissing)
		{
			UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("PSO had an active shader slot that did not match any current shaders, ignored."));
			if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				PrintShaders(InverseMap, Item.ComputeDesc.ComputeShader, TEXT("ComputeShader"));
			}
			else
			{
				UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("   %s"), *Item.GraphicsDesc.StateToString());
				PrintShaders(InverseMap, Item.GraphicsDesc.VertexShader, TEXT("VertexShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.FragmentShader, TEXT("FragmentShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.GeometryShader, TEXT("GeometryShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.HullShader, TEXT("HullShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.DomainShader, TEXT("DomainShader"));
			}
			continue;
		}
		if (Item.Type != FPipelineCacheFileFormatPSO::DescriptorType::Compute)
		{
			check(!ActivePerSlot[SF_Compute]); // this is NOT a compute shader
			bool bRemovedAll = false;
			bool bAnyActive = false;
			// Quite the nested loop. It isn't clear if this could be made faster, but the thing to realize is that the same set of shaders will be used in multiple PSOs we could take advantage of that...we don't.
			for (int32 SlotIndex = 0; SlotIndex < SF_NumFrequencies; SlotIndex++)
			{
				if (!ActivePerSlot[SlotIndex])
				{
					check(!StableShadersPerSlot[SlotIndex].Num());
					continue;
				}
				bAnyActive = true;
				for (int32 StableIndex = 0; StableIndex < StableShadersPerSlot[SlotIndex].Num(); StableIndex++)
				{
					bool bKeep = true;
					for (int32 SlotIndexInner = 0; SlotIndexInner < SF_Compute; SlotIndexInner++) //SF_Compute here because this is NOT a compute shader
					{
						if (SlotIndex == SlotIndexInner || !ActivePerSlot[SlotIndexInner])
						{
							continue;
						}
						bool bFoundCompat = false;
						for (int32 StableIndexInner = 0; StableIndexInner < StableShadersPerSlot[SlotIndexInner].Num(); StableIndexInner++)
						{
							if (CouldBeUsedTogether(StableShadersPerSlot[SlotIndex][StableIndex], StableShadersPerSlot[SlotIndexInner][StableIndexInner]))
							{
								bFoundCompat = true;
								break;
							}
						}
						if (!bFoundCompat)
						{
							bKeep = false;
							break;
						}
					}
					if (!bKeep)
					{
						StableShadersPerSlot[SlotIndex].RemoveAt(StableIndex--);
					}
				}
				if (!StableShadersPerSlot[SlotIndex].Num())
				{
					bRemovedAll = true;
				}
			}
			if (!bAnyActive)
			{
				NumSkipped++;
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("PSO did not create any stable PSOs! (no active shader slots)"));
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("   %s"), *Item.GraphicsDesc.StateToString());
				continue;
			}
			if (bRemovedAll)
			{
				UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("PSO did not create any stable PSOs! (no cross shader slot compatibility)"));
				UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("   %s"), *Item.GraphicsDesc.StateToString());

				PrintShaders(InverseMap, Item.GraphicsDesc.VertexShader, TEXT("VertexShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.FragmentShader, TEXT("FragmentShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.GeometryShader, TEXT("GeometryShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.HullShader, TEXT("HullShader"));
				PrintShaders(InverseMap, Item.GraphicsDesc.DomainShader, TEXT("DomainShader"));

				continue;
			}
			// We could have done this on the fly, but that loop was already pretty complicated. Here we generate all plausible permutations and write them out
		}

		StableResults.AddDefaulted();
		FPermsPerPSO& Current = StableResults.Last();
		Current.PSO = &Item;

		for (int32 Index = 0; Index < SF_NumFrequencies; Index++)
		{
			Current.ActivePerSlot[Index] = ActivePerSlot[Index];
		}

		TArray<FPermuation>& Permutations(Current.Permutations);
		FPermuation WorkingPerm;
		GeneratePermuations(Permutations, WorkingPerm, 0, StableShadersPerSlot, ActivePerSlot);
		if (!Permutations.Num())
		{
			UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("PSO did not create any stable PSOs! (somehow)"));
			// this is fatal because now we have a bogus thing in the list
			UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("   %s"), *Item.GraphicsDesc.StateToString());
			continue;
		}

		UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("----- PSO created %d stable permutations --------------"), Permutations.Num());
		TotalStablePSOs += Permutations.Num();
	}
	UE_CLOG(NumSkipped > 0, LogShaderPipelineCacheTools, Warning, TEXT("%d/%d PSO did not create any stable PSOs! (no active shader slots)"), NumSkipped, NumExamined);
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Generated %d stable PSOs total"), TotalStablePSOs);
	if (!TotalStablePSOs || !StableResults.Num())
	{
		UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("No stable PSOs created."));
		return 1;
	}

	int32 NumLines = 0;
	TArray<uint8> UncomressedOutputLines;
	FMemoryWriter OutputLinesAr(UncomressedOutputLines);
	TSet<FString> DeDup;

	{
		FString PSOLine = FString::Printf(TEXT("\"%s\""), *FPipelineCacheFileFormatPSO::CommonHeaderLine());
		PSOLine += FString::Printf(TEXT(",\"%s\""), *FPipelineCacheFileFormatPSO::GraphicsDescriptor::StateHeaderLine());
		for (int32 SlotIndex = 0; SlotIndex < SF_Compute; SlotIndex++) // SF_Compute here because the stablepc.csv file format does not have a compute slot
		{
			PSOLine += FString::Printf(TEXT(",\"shaderslot%d: %s\""), SlotIndex, *FStableShaderKeyAndValue::HeaderLine());
		}

		OutputLinesAr << PSOLine;
		NumLines++;
	}

	for (const FPermsPerPSO& Item : StableResults)
	{
		if (UE_LOG_ACTIVE(LogShaderPipelineCacheTools, Verbose))
		{
			if (Item.PSO->Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT(" Compute"));
			}
			else
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT(" %s"), *Item.PSO->GraphicsDesc.StateToString());
			}
			int32 PermIndex = 0;
			for (const FPermuation& Perm : Item.Permutations)
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("  ----- perm %d"), PermIndex);
				for (int32 SlotIndex = 0; SlotIndex < SF_NumFrequencies; SlotIndex++)
				{
					if (!Item.ActivePerSlot[SlotIndex])
					{
						continue;
					}
					UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("   %s"), *Perm.Slots[SlotIndex].ToString());
				}
				PermIndex++;
			}

			UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("-----"));
		}
		for (const FPermuation& Perm : Item.Permutations)
		{
			// because it is a CSV, and for backward compat, compute shaders will just be a zeroed graphics desc with the shader in the hull shader slot.
			FString PSOLine = Item.PSO->CommonToString();
			PSOLine += TEXT(",");
			if (Item.PSO->Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				FPipelineCacheFileFormatPSO::GraphicsDescriptor Zero;
				FMemory::Memzero(Zero);
				PSOLine += FString::Printf(TEXT("\"%s\""), *Zero.StateToString());
				for (int32 SlotIndex = 0; SlotIndex < SF_Compute; SlotIndex++)  // SF_Compute here because the stablepc.csv file format does not have a compute slot
				{
					check(!Item.ActivePerSlot[SlotIndex]); // none of these should be active for a compute shader
					if (SlotIndex == SF_Hull)
					{
						PSOLine += FString::Printf(TEXT(",\"%s\""), *Perm.Slots[SF_Compute].ToString());
					}
					else
					{
						PSOLine += FString::Printf(TEXT(",\"\""));
					}
				}
			}
			else
			{
				PSOLine += FString::Printf(TEXT("\"%s\""), *Item.PSO->GraphicsDesc.StateToString());
				for (int32 SlotIndex = 0; SlotIndex < SF_Compute; SlotIndex++) // SF_Compute here because the stablepc.csv file format does not have a compute slot
				{
					if (!Item.ActivePerSlot[SlotIndex])
					{
						PSOLine += FString::Printf(TEXT(",\"\""));
						continue;
					}
					PSOLine += FString::Printf(TEXT(",\"%s\""), *Perm.Slots[SlotIndex].ToString());
				}
			}


			if (!DeDup.Contains(PSOLine))
			{
				DeDup.Add(PSOLine);
				OutputLinesAr << PSOLine;
				NumLines++;
			}
		}
	}

	const FString& OutputFilename = Tokens.Last();
	const bool bCompressed = OutputFilename.EndsWith(STABLE_CSV_COMPRESSED_EXT);
	
	FString CompressedFilename;
	FString UncompressedFilename;
	if (bCompressed)
	{
		CompressedFilename = OutputFilename;
		UncompressedFilename = CompressedFilename.LeftChop(STABLE_COMPRESSED_EXT_LEN); // remove the ".compressed"
	}
	else
	{
		UncompressedFilename = OutputFilename;
		CompressedFilename = UncompressedFilename + STABLE_COMPRESSED_EXT;  // add the ".compressed"
	}
	
	// delete both compressed and uncompressed files
	if (IFileManager::Get().FileExists(*UncompressedFilename))
	{
		IFileManager::Get().Delete(*UncompressedFilename, false, true);
		if (IFileManager::Get().FileExists(*UncompressedFilename))
		{
			UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not delete %s"), *UncompressedFilename);
		}
	}
	if (IFileManager::Get().FileExists(*CompressedFilename))
	{
		IFileManager::Get().Delete(*CompressedFilename, false, true);
		if (IFileManager::Get().FileExists(*CompressedFilename))
		{
			UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not delete %s"), *CompressedFilename);
		}
	}

	int64 FileSize = SaveStableCSV(OutputFilename, UncomressedOutputLines);
	if (FileSize < 1)
	{
		return 1;
	}
	
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Wrote stable PSOs, %d lines (%.1f KB) to %s"), NumLines, FileSize / 1024.f, *OutputFilename);
	return 0;
}

template <uint32 InlineSize>
static void ParseQuoteComma(const FStringView& InLine, TArray<FStringView, TInlineAllocator<InlineSize>>& OutParts)
{
	FStringView Line = InLine;
	while (true)
	{
		int32 QuoteLoc = 0;
		if (!Line.FindChar(TCHAR('\"'), QuoteLoc))
		{
			break;
		}
		Line.RightChopInline(QuoteLoc + 1);
		if (!Line.FindChar(TCHAR('\"'), QuoteLoc))
		{
			break;
		}
		OutParts.Add(Line.Left(QuoteLoc));
		Line.RightChopInline(QuoteLoc + 1);
	}
}

static TSet<FPipelineCacheFileFormatPSO> ParseStableCSV(const FString& FileName, const FRawStableCSV& RawStableCSV, const TMultiMap<FStableShaderKeyAndValue, FSHAHash>& StableMap, FName& TargetPlatform)
{
	TSet<FPipelineCacheFileFormatPSO> PSOs;

	int32 LineIndex = 0;
	bool bParsed = true;
	ReadStableCSV(RawStableCSV, [&FileName, &StableMap, &TargetPlatform, &PSOs, &LineIndex, &bParsed](FStringView Line)
	{
		// Skip the header line.
		if (LineIndex++ == 0)
		{
			return;
		}

		// Only attempt to parse the current line if previous lines succeeded.
		if (!bParsed)
		{
			return;
		}

		TArray<FStringView, TInlineAllocator<2 + SF_Compute>> Parts;
		ParseQuoteComma(Line, Parts);

		if (Parts.Num() != 2 + SF_Compute) // SF_Compute here because the stablepc.csv file format does not have a compute slot
		{
			// Assume the rest of the file csv lines are are bad or are in an out of date format. If one is, they probably all are.
			UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("File %s is not in the correct format ignoring the rest of its contents."), *FileName);
			bParsed = false;
			return;
		}

		FPipelineCacheFileFormatPSO PSO;
		FMemory::Memzero(PSO);
		PSO.Type = FPipelineCacheFileFormatPSO::DescriptorType::Graphics; // we will change this to compute later if needed
		PSO.CommonFromString(Parts[0]);
		bool bValidGraphicsDesc = PSO.GraphicsDesc.StateFromString(Parts[1]);
		if (!bValidGraphicsDesc)
		{
			// Failed to parse graphics descriptor, most likely format was changed, skip whole file.
			UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("File %s is not in the correct format (GraphicsDesc) ignoring the rest of its contents."), *FileName);
			bParsed = false;
			return;
		}

		// For backward compatibility, compute shaders are stored as a zeroed graphics desc with the shader in the hull shader slot.
		static FName NAME_SF_Compute("SF_Compute");
		for (int32 SlotIndex = 0; SlotIndex < SF_Compute; ++SlotIndex)
		{
			if (Parts[SlotIndex + 2].IsEmpty())
			{
				continue;
			}

			FStableShaderKeyAndValue Shader;
			Shader.ParseFromString(Parts[SlotIndex + 2]);

			int32 AdjustedSlotIndex = SlotIndex;
			if (SlotIndex == SF_Hull)
			{
				if (Shader.TargetFrequency == NAME_SF_Compute)
				{
					PSO.Type = FPipelineCacheFileFormatPSO::DescriptorType::Compute;
					AdjustedSlotIndex = SF_Compute;
				}
			}
			else
			{
				check(Shader.TargetFrequency != NAME_SF_Compute);
			}

			FSHAHash Match;
			int32 Count = 0;
			for (auto Iter = StableMap.CreateConstKeyIterator(Shader); Iter; ++Iter)
			{
				check(Iter.Value() != FSHAHash());
				Match = Iter.Value();
				if (TargetPlatform == NAME_None)
				{
					TargetPlatform = Iter.Key().TargetPlatform;
				}
				else
				{
					check(TargetPlatform == Iter.Key().TargetPlatform);
				}
				++Count;
			}

			if (!Count)
			{
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("Stable PSO not found, rejecting %s"), *Shader.ToString());
				return;
			}

			if (Count > 1)
			{
				UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("Stable PSO maps to multiple shaders. This is usually a bad thing and means you used .scl.csv files from multiple builds. Ignoring all but the last %s"), *Shader.ToString());
			}

			switch (AdjustedSlotIndex)
			{
			case SF_Vertex:
				PSO.GraphicsDesc.VertexShader = Match;
				break;
			case SF_Pixel:
				PSO.GraphicsDesc.FragmentShader = Match;
				break;
			case SF_Geometry:
				PSO.GraphicsDesc.GeometryShader = Match;
				break;
			case SF_Hull:
				PSO.GraphicsDesc.HullShader = Match;
				break;
			case SF_Domain:
				PSO.GraphicsDesc.DomainShader = Match;
				break;
			case SF_Compute:
				PSO.ComputeDesc.ComputeShader = Match;
				break;
			}
		}

		if (PSO.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
		{
			check(PSO.ComputeDesc.ComputeShader != FSHAHash() &&
				PSO.GraphicsDesc.VertexShader == FSHAHash() &&
				PSO.GraphicsDesc.FragmentShader == FSHAHash() &&
				PSO.GraphicsDesc.GeometryShader == FSHAHash() &&
				PSO.GraphicsDesc.HullShader == FSHAHash() &&
				PSO.GraphicsDesc.DomainShader == FSHAHash());
		}
		else
		{
			check(PSO.ComputeDesc.ComputeShader == FSHAHash());
		}

		if (!PSO.Verify())
		{
			UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Bad PSO found. Verify failed. PSO discarded [Line %d in: %s]"), LineIndex, *FileName);
			return;
		}

		// Merge duplicate PSO lines together.
		if (FPipelineCacheFileFormatPSO* ExistingPSO = PSOs.Find(PSO))
		{
			check(*ExistingPSO == PSO);
			ExistingPSO->UsageMask |= PSO.UsageMask;
			ExistingPSO->BindCount = FMath::Max(ExistingPSO->BindCount, PSO.BindCount);
		}
		else
		{
			PSOs.Add(PSO);
		}
	});

	return PSOs;
}

typedef TFunction<bool(const FString&)> FilenameFilterFN;

void BuildDateSortedListOfFiles(const TArray<FString>& TokenList, FilenameFilterFN FilterFn, TArray<FString>& Result)
{
	struct FDateSortableFileRef
	{
		FDateTime SortTime;
		FString FileName;
	};
	
	TArray<FDateSortableFileRef> DateFileList;
	for (int32 TokenIndex = 0; TokenIndex < TokenList.Num() - 1; TokenIndex++)
	{
		if (FilterFn(TokenList[TokenIndex]))
		{
			FDateSortableFileRef DateSortEntry;
			DateSortEntry.SortTime = FDateTime::Now();
			DateSortEntry.FileName = TokenList[TokenIndex];
			
			FFileStatData StatData = IFileManager::Get().GetStatData(*TokenList[TokenIndex]);
			if(StatData.bIsValid && StatData.CreationTime != FDateTime::MinValue())
			{
				DateSortEntry.SortTime = StatData.CreationTime;
			}
			
			DateFileList.Add(DateSortEntry);
		}
	}
	
	DateFileList.Sort([](const FDateSortableFileRef& A, const FDateSortableFileRef& B) {return A.SortTime > B.SortTime;});
	
	for(auto& FileRef : DateFileList)
	{
		Result.Add(FileRef.FileName);
	}
}

int32 BuildPSOSC(const TArray<FString>& Tokens)
{
	check(Tokens.Last().EndsWith(TEXT(".upipelinecache")));

	TArray<FStringView, TInlineAllocator<16>> StableSCLs;
	TArray<FString> StablePipelineCacheFiles;

	for (int32 Index = 0; Index < Tokens.Num() - 1; Index++)
	{
		if (Tokens[Index].EndsWith(TEXT(".scl.csv")))
		{
			StableSCLs.Add(Tokens[Index]);
		}
	}

	// Get the stable PC files in date order - least to most important(!?)
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Sorting input stablepc.csv files into chronological order for merge processing..."));
	FilenameFilterFN ExtensionFilterFn = [](const FString& Filename)
	{
		return Filename.EndsWith(STABLE_CSV_EXT) || Filename.EndsWith(STABLE_CSV_COMPRESSED_EXT);
	};
	BuildDateSortedListOfFiles(Tokens, ExtensionFilterFn, StablePipelineCacheFiles);

	// Start populating the stable SCLs in a task.
	TMultiMap<FStableShaderKeyAndValue, FSHAHash> StableMap;
	FGraphEventRef StableMapTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&StableSCLs, &StableMap]
	{
		LoadStableSCLs(StableMap, StableSCLs);
		if (UE_LOG_ACTIVE(LogShaderPipelineCacheTools, Verbose))
		{
			UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("    %s"), *FStableShaderKeyAndValue::HeaderLine());
			for (const auto& Pair : StableMap)
			{
				FStableShaderKeyAndValue Temp(Pair.Key);
				Temp.OutputHash = Pair.Value;
				UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("    %s"), *Temp.ToString());
			}
			StableShadersSerializationSelfTest(StableMap);
		}
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d unique shader info lines total."), StableMap.Num());
	}, TStatId());

	// Read the stable PSO sets in parallel with the stable shaders.
	FGraphEventArray LoadPSOTasks;
	TArray<FRawStableCSV> RawStableCSVs;
	LoadPSOTasks.Reserve(StablePipelineCacheFiles.Num());
	RawStableCSVs.AddDefaulted(StablePipelineCacheFiles.Num());
	for (int32 FileIndex = 0; FileIndex < StablePipelineCacheFiles.Num(); ++FileIndex)
	{
		LoadPSOTasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([&RawStableCSV = RawStableCSVs[FileIndex], &FileName = StablePipelineCacheFiles[FileIndex]]
		{
			if (!LoadStableCSV(FileName, RawStableCSV))
			{
				UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not load %s"), *FileName);
			}
		}, TStatId()));
	}

	// Parse the stable PSO sets in parallel once both the stable shaders and the corresponding read are complete.
	FGraphEventArray ParsePSOTasks;
	TArray<TSet<FPipelineCacheFileFormatPSO>> PSOsByFile;
	TArray<FName> TargetPlatformByFile;
	ParsePSOTasks.Reserve(StablePipelineCacheFiles.Num());
	PSOsByFile.AddDefaulted(StablePipelineCacheFiles.Num());
	TargetPlatformByFile.AddDefaulted(StablePipelineCacheFiles.Num());
	for (int32 FileIndex = 0; FileIndex < StablePipelineCacheFiles.Num(); ++FileIndex)
	{
		const FGraphEventArray PreReqs{StableMapTask, LoadPSOTasks[FileIndex]};
		ParsePSOTasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&PSOs = PSOsByFile[FileIndex],
			 &FileName = StablePipelineCacheFiles[FileIndex],
			 &RawStableCSV = RawStableCSVs[FileIndex],
			 &StableMap,
			 &TargetPlatform = TargetPlatformByFile[FileIndex]]
			{
				PSOs = ParseStableCSV(FileName, RawStableCSV, StableMap, TargetPlatform);
				RawStableCSV = FRawStableCSV();
				UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d stable PSO lines from %s."), PSOs.Num(), *FileName);
			}, TStatId(), &PreReqs));
	}

	// Always wait for these tasks before returning from this function.
	// This is necessary if there is an error or if nothing consumes the stable map.
	ON_SCOPE_EXIT
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(StableMapTask);
		FTaskGraphInterface::Get().WaitUntilTasksComplete(ParsePSOTasks);
	};

	// Validate and merge the stable PSO sets sequentially as they finish.
	TSet<FPipelineCacheFileFormatPSO> PSOs;
	TMap<uint32,int64> PSOAvgIterations;
	uint32 MergeCount = 0;
	FName TargetPlatform;

	for (int32 FileIndex = 0; FileIndex < StablePipelineCacheFiles.Num(); ++FileIndex)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(ParsePSOTasks[FileIndex]);

		if (!PSOsByFile[FileIndex].Num())
		{
			return 1;
		}

		check(TargetPlatform == NAME_None || TargetPlatform == TargetPlatformByFile[FileIndex]);
		TargetPlatform = TargetPlatformByFile[FileIndex];

		TSet<FPipelineCacheFileFormatPSO>& CurrentFilePSOs = PSOsByFile[FileIndex];

		if (!CurrentFilePSOs.Num())
		{
			continue;
		}

		// Now merge this file PSO set with main PSO set (this is going to be slow as we need to incrementally reprocess each existing PSO per file to get reasonable bindcount averages).
		// Can't sum all and avg: A) Overflow and B) Later ones want to remain high so only start to get averaged from the point they are added onwards:
		// 1) New PSO goes in with it's bindcount intact for this iteration - if it's the last file then it keeps it bindcount
		// 2) Existing PSO from older file gets incrementally averaged with PSO bindcount from new file
		// 3) Existing PSO from older file not in new file set gets incrementally averaged with zero - now less important
		// 4) PSOs are incrementally averaged from the point they are seen - i.e. a PSO seen in an earler file will get averaged more times than one
		//		seen in a later file using:  NewAvg = OldAvg + (NewValue - OldAvg) / CountFromPSOSeen
		//
		// Proof for incremental averaging:
		//	DataSet = {25 65 95 128}; Standard Average = (sum(25, 65, 95, 128) / 4) = 78.25
		//	Incremental:
		//	=> 25
		//	=> 25 + (65 - 25) / 2 = A 		==> 25 + (65 - 25) / 2 		= 45
		//	=>  A + (95 -  A) / 3 = B 		==> 45 + (95 - 45) / 3 		= 61 2/3
		//	=>  B + (128 - B) / 4 = Answer 	==> 61 2/3 + (128 - B) / 4 	= 78.25

		for (FPipelineCacheFileFormatPSO& PSO : PSOs)
		{
			// Already existing PSO in the next file round - increase its average iteration
			int64& PSOAvgIteration = PSOAvgIterations.FindChecked(GetTypeHash(PSO));
			++PSOAvgIteration;

			// Default the bindcount
			int64 NewBindCount = 0ll;

			// If you have the same PSO in the new file set
			if (FPipelineCacheFileFormatPSO* NewFilePSO = CurrentFilePSOs.Find(PSO))
			{
				// Sanity check!
				check(*NewFilePSO == PSO);

				// Get More accurate stats by testing for diff - we could just merge and be done
				if ((PSO.UsageMask & NewFilePSO->UsageMask) != NewFilePSO->UsageMask)
				{
					PSO.UsageMask |= NewFilePSO->UsageMask;
					++MergeCount;
				}

				NewBindCount = NewFilePSO->BindCount;

				// Remove from current file set - it's already there and we don't want any 'overwrites'
				CurrentFilePSOs.Remove(*NewFilePSO);
			}

			// Incrementally average this PSO bindcount - if not found in this set then avg will be pulled down
			PSO.BindCount += (NewBindCount - PSO.BindCount) / PSOAvgIteration;
		}

		// Add the leftover PSOs from the current file and initialize their iteration count.
		for (const FPipelineCacheFileFormatPSO& PSO : CurrentFilePSOs)
		{
			PSOAvgIterations.Add(GetTypeHash(PSO), 1ll);
		}
		PSOs.Append(MoveTemp(CurrentFilePSOs));
	}
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Re-deduplicated into %d binary PSOs [Usage Mask Merged = %d]."), PSOs.Num(), MergeCount);

	if (PSOs.Num() < 1)
	{
		UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("No PSOs were created!"));
		return 0;
	}

	if (UE_LOG_ACTIVE(LogShaderPipelineCacheTools, Verbose))
	{
		for (const FPipelineCacheFileFormatPSO& Item : PSOs)
		{
			FString StringRep;
			if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Compute)
			{
				check(!(Item.ComputeDesc.ComputeShader == FSHAHash()));
				StringRep = Item.ComputeDesc.ToString();
			}
			else
			{
				check(!(Item.GraphicsDesc.VertexShader == FSHAHash()));
				StringRep = Item.GraphicsDesc.ToString();
			}
			UE_LOG(LogShaderPipelineCacheTools, Verbose, TEXT("%s"), *StringRep);
		}
	}

	check(TargetPlatform != NAME_None);
	EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(TargetPlatform);
	check(Platform != SP_NumPlatforms);

	if (IsOpenGLPlatform(Platform))
	{
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("OpenGL detected, reducing PSOs to be BSS only as OpenGL doesn't care about the state at all when compiling shaders."));

		TSet<FPipelineCacheFileFormatPSO> KeptPSOs;

		// N^2 not good. 
		for (const FPipelineCacheFileFormatPSO& Item : PSOs)
		{
			bool bMatchedKept = false;
			if (Item.Type == FPipelineCacheFileFormatPSO::DescriptorType::Graphics)
			{
				for (const FPipelineCacheFileFormatPSO& TestItem : KeptPSOs)
				{
					if (TestItem.Type == FPipelineCacheFileFormatPSO::DescriptorType::Graphics)
					{
						if (
							TestItem.GraphicsDesc.VertexShader == Item.GraphicsDesc.VertexShader &&
							TestItem.GraphicsDesc.FragmentShader == Item.GraphicsDesc.FragmentShader &&
							TestItem.GraphicsDesc.GeometryShader == Item.GraphicsDesc.GeometryShader &&
							TestItem.GraphicsDesc.HullShader == Item.GraphicsDesc.HullShader &&
							TestItem.GraphicsDesc.DomainShader == Item.GraphicsDesc.DomainShader
							)
						{
							bMatchedKept = true;
							break;
						}
					}
				}
			}
			if (!bMatchedKept)
			{
				KeptPSOs.Add(Item);
			}
		}
		Exchange(PSOs, KeptPSOs);
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("BSS only reduction produced %d binary PSOs."), PSOs.Num());

		if (PSOs.Num() < 1)
		{
			UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("No PSOs were created!"));
			return 0;
		}

	}

	if (IFileManager::Get().FileExists(*Tokens.Last()))
	{
		IFileManager::Get().Delete(*Tokens.Last(), false, true);
	}
	if (IFileManager::Get().FileExists(*Tokens.Last()))
	{
		UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not delete %s"), *Tokens.Last());
	}
	if (!FPipelineFileCache::SavePipelineFileCacheFrom(FShaderPipelineCache::GetGameVersionForPSOFileCache(), Platform, Tokens.Last(), PSOs))
	{
		UE_LOG(LogShaderPipelineCacheTools, Error, TEXT("Failed to save %s"), *Tokens.Last());
		return 1;
	}
	int64 Size = IFileManager::Get().FileSize(*Tokens.Last());
	if (Size < 1)
	{
		UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Failed to write %s"), *Tokens.Last());
	}
	UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Wrote binary PSOs, (%lldKB) to %s"), (Size + 1023) / 1024, *Tokens.Last());
	return 0;
}


int32 DiffStable(const TArray<FString>& Tokens)
{
	TArray<TSet<FString>> Sets;
	for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); TokenIndex++)
	{
		const FString& Filename = Tokens[TokenIndex];
		bool bCompressed = Filename.EndsWith(STABLE_CSV_COMPRESSED_EXT);
		if (!bCompressed && !Filename.EndsWith(STABLE_CSV_EXT))
		{
			check(0);
			continue;
		}
			   
		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loading %s...."), *Filename);
		TArray<FString> SourceFileContents;
		if (LoadStableCSV(Filename, SourceFileContents) || SourceFileContents.Num() < 2)
		{
			UE_LOG(LogShaderPipelineCacheTools, Fatal, TEXT("Could not load %s"), *Filename);
			return 1;
		}

		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("Loaded %d stable PSO lines."), SourceFileContents.Num() - 1);

		Sets.AddDefaulted();

		for (int32 Index = 1; Index < SourceFileContents.Num(); Index++)
		{
			Sets.Last().Add(SourceFileContents[Index]);
		}
	}
	TSet<FString> Inter;
	for (int32 TokenIndex = 0; TokenIndex < Sets.Num(); TokenIndex++)
	{
		if (TokenIndex)
		{
			Inter = Sets[TokenIndex];
		}
		else
		{
			Inter = Inter.Intersect(Sets[TokenIndex]);
		}
	}

	for (int32 TokenIndex = 0; TokenIndex < Sets.Num(); TokenIndex++)
	{
		TSet<FString> InterSet = Sets[TokenIndex].Difference(Inter);

		UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("********************* Loaded %d not in others %s"), InterSet.Num(), *Tokens[TokenIndex]);
		for (const FString& Item : InterSet)
		{
			UE_LOG(LogShaderPipelineCacheTools, Display, TEXT("    %s"), *Item);
		}
	}
	return 0;
}

int32 DecompressCSV(const TArray<FString>& Tokens)
{
	TArray<uint8> DecompressedData;
	for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); TokenIndex++)
	{
		const FString& CompressedFilename = Tokens[TokenIndex];
		if (!CompressedFilename.EndsWith(STABLE_CSV_COMPRESSED_EXT))
		{
			continue;
		}
		
		FString CombinedCSV;
		DecompressedData.Reset();
		if (LoadAndDecompressStableCSV(CompressedFilename, DecompressedData))
		{
			FMemoryReader MemArchive(DecompressedData);
			FString LineCSV;
			while (!MemArchive.AtEnd())
			{
				MemArchive << LineCSV;
				CombinedCSV.Append(LineCSV);
				CombinedCSV.Append(LINE_TERMINATOR);
			}

			FString FilenameCSV = CompressedFilename.LeftChop(STABLE_COMPRESSED_EXT_LEN);
			FFileHelper::SaveStringToFile(CombinedCSV, *FilenameCSV);
		}
	}

	return 0;
}

UShaderPipelineCacheToolsCommandlet::UShaderPipelineCacheToolsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UShaderPipelineCacheToolsCommandlet::Main(const FString& Params)
{
	return StaticMain(Params);
}

int32 UShaderPipelineCacheToolsCommandlet::StaticMain(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	if (Tokens.Num() >= 1)
	{
		ExpandWildcards(Tokens);
		if (Tokens[0] == TEXT("Expand") && Tokens.Num() >= 4)
		{
			Tokens.RemoveAt(0);
			return ExpandPSOSC(Tokens);
		}
		else if (Tokens[0] == TEXT("Build") && Tokens.Num() >= 4)
		{
			Tokens.RemoveAt(0);
			return BuildPSOSC(Tokens);
		}
		else if (Tokens[0] == TEXT("Diff") && Tokens.Num() >= 3)
		{
			Tokens.RemoveAt(0);
			return DiffStable(Tokens);
		}
		else if (Tokens[0] == TEXT("Dump") && Tokens.Num() >= 2)
		{
			Tokens.RemoveAt(0);
			for (int32 Index = 0; Index < Tokens.Num(); Index++)
			{
				if (Tokens[Index].EndsWith(TEXT(".upipelinecache")))
				{
					return DumpPSOSC(Tokens[Index]);
				}
				if (Tokens[Index].EndsWith(TEXT(".scl.csv")))
				{
					return DumpSCLCSV(Tokens[Index]);
				}
			}
		}
		else if (Tokens[0] == TEXT("Decompress") && Tokens.Num() >= 2)
		{
			Tokens.RemoveAt(0);
			return DecompressCSV(Tokens);
		}
	}
	
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: Dump ShaderCache1.upipelinecache SCLInfo2.scl.csv [...]]\n"));
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: Diff ShaderCache1.stablepc.csv ShaderCache1.stablepc.csv [...]]\n"));
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: Expand Input1.upipelinecache Dir2/*.upipelinecache InputSCLInfo1.scl.csv Dir2/*.scl.csv InputSCLInfo3.scl.csv [...] Output.stablepc.csv\n"));
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: Build Input.stablepc.csv InputDir2/*.stablepc.csv InputSCLInfo1.scl.csv Dir2/*.scl.csv InputSCLInfo3.scl.csv [...] Output.upipelinecache\n"));
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: Decompress Input1.stablepc.csv.compressed Input2.stablepc.csv.compressed [...]\n"));
	UE_LOG(LogShaderPipelineCacheTools, Warning, TEXT("Usage: All commands accept stablepc.csv.compressed instead of stablepc.csv for compressing output\n"));
	return 0;
}

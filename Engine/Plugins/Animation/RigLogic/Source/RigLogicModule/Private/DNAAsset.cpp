// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DNAAsset.h"
#include "DNAUtils.h"

#include "DNAReaderAdapter.h"
#include "FMemoryResource.h"
#include "RigLogicMemoryStream.h"
#include "SkelMeshDNAReader.h"

#if WITH_EDITORONLY_DATA
    #include "EditorFramework/AssetImportData.h"
#endif
#include "Engine/AssetUserData.h"
#include "Runtime/Core/Public/Serialization/BufferArchive.h"
#include "Runtime/Core/Public/Serialization/MemoryReader.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "Runtime/Core/Public/Misc/FileHelper.h"
#include "Runtime/CoreUObject/Public/UObject/UObjectGlobals.h"

#include "riglogic/RigLogic.h"

DEFINE_LOG_CATEGORY(LogDNAAsset);

static constexpr uint32 AVG_BEHAVIOR_SIZE = 5 * 1024 * 1024;
static constexpr uint32 AVG_GEOMETRY_SIZE = 150 * 1024 * 1024;

void UDNAAsset::WriteToBuffer( IDNAReader* SourcePartialDNAReader, TArray<uint8>* WriteBuffer, uint32 PredictedSize)
{	
	WriteBuffer->Empty();
	WriteBuffer->Reserve(PredictedSize);
	// Use that stream to write data into TArray, so UE can serialize it with this asset
	FRigLogicMemoryStream PartialDataWriteStream(WriteBuffer);
	rl4::ScopedPtr<dna::StreamWriter> PartialStreamWriter = rl4::makeScoped<dna::StreamWriter>(&PartialDataWriteStream, FMemoryResource::Instance()); //where to write
	PartialStreamWriter->setFrom(SourcePartialDNAReader->Unwrap()); //from where to read
	PartialStreamWriter->write(); //DNAData TArray now holds and serializes behavior data
}

bool UDNAAsset::Init(const FString DNAFilename)
{
	if (!rl4::Status::isOk()) {
		UE_LOG(LogDNAAsset, Warning, TEXT("%s"), ANSI_TO_TCHAR(rl4::Status::get().message));
	}

	this->DNAFileName = DNAFilename; //memorize for re-import
	
	if (!FPaths::FileExists(DNAFilename))
	{
		UE_LOG(LogDNAAsset, Error, TEXT("DNA file %s doesn't exist!"), *DNAFilename);
		return false;
	}
	
	// Temporary buffer for the DNA file
	TArray<uint8> TempFileBuffer;
	
	if (!FFileHelper::LoadFileToArray(TempFileBuffer, *DNAFilename)) //load entire DNA file into the array
	{
		UE_LOG(LogDNAAsset, Error, TEXT("Couldn't read DNA file %s!"), *DNAFilename);
		return false;
	}
	
	// Load run-time data (behavior) from whole-DNA buffer into BehaviorStreamReader
	TSharedPtr<IDNAReader> DNAReaderBehavior = ReadDNALayerFromBuffer(&TempFileBuffer, EDNADataLayer::Behavior, 0u); //0u = MaxLOD

	if (!DNAReaderBehavior.IsValid())
	{
		return false;
	}

	BehaviorStreamReader = DNAReaderBehavior;
	WriteToBuffer(DNAReaderBehavior.Get(), &BehaviorData, AVG_BEHAVIOR_SIZE);

#if WITH_EDITORONLY_DATA
	//We use geometry part of the data in MHC only (for updating the SkeletalMesh with
	//result of GeneSplicer), so we can drop geometry part when cooking for runtime
	TSharedPtr<IDNAReader> DNAReaderGeometry = ReadDNALayerFromBuffer(&TempFileBuffer, EDNADataLayer::Geometry, 0u); //0u = MaxLOD
	GeometryStreamReader = DNAReaderGeometry;
	WriteToBuffer(DNAReaderGeometry.Get(), &DesignTimeData, AVG_GEOMETRY_SIZE);
	//Note: in future, we will want to load geometry data in-game too 
	//to enable GeneSplicer to read geometry directly from SkeletalMeshes, as
	//a way to save memory, as on consoles the "database" will be exactly the set of characters
	//used in the game
#endif // #if WITH_EDITORONLY_DATA

	return true;
}

void UDNAAsset::PostLoad()
{
	Super::PostLoad();

	if (BehaviorData.Num() == 0)
	{
		return;
	}

	BehaviorStreamReader = ReadDNALayerFromBuffer(&BehaviorData, EDNADataLayer::Behavior, 0u); //0u = max LOD

#if WITH_EDITORONLY_DATA
	if (DesignTimeData.Num() == 0)
	{
		return;
	}
	GeometryStreamReader = ReadDNALayerFromBuffer(&DesignTimeData, EDNADataLayer::Geometry, 0u); //0u = max LOD
#endif // #if WITH_EDITORONLY_DATA
}

TSharedPtr<IDNAReader> UDNAAsset::CopyDNALayer(const IDNAReader* Source, EDNADataLayer DNADataLayer, uint32 PredictedSize )
{
	// To avoid lots of reallocations in `FRigLogicMemoryStream`, reserve an approximate size
	// that we know would cause at most one reallocation in the worst case (but none for the average DNA)
	
	TArray<uint8> MemoryBuffer;
	MemoryBuffer.Reserve(PredictedSize);

	FRigLogicMemoryStream MemoryStream(&MemoryBuffer);
	auto DNAWriter = rl4::makeScoped<dna::StreamWriter>(&MemoryStream, FMemoryResource::Instance());
	DNAWriter->setFrom(Source->Unwrap(), static_cast<dna::DataLayer>(DNADataLayer));
	DNAWriter->write();

	MemoryStream.seek(0ul);

	return ReadDNALayerFromBuffer(&MemoryBuffer, DNADataLayer);
}

void UDNAAsset::SetBehaviorReader(const TSharedPtr<IDNAReader> SourceDNAReader)
{

	BehaviorStreamReader = CopyDNALayer(SourceDNAReader.Get(), EDNADataLayer::Behavior, AVG_BEHAVIOR_SIZE);
}

void UDNAAsset::SetGeometryReader(const TSharedPtr<IDNAReader> SourceDNAReader)
{
#if WITH_EDITORONLY_DATA
	GeometryStreamReader = CopyDNALayer(SourceDNAReader.Get(), EDNADataLayer::Geometry, AVG_GEOMETRY_SIZE);
#endif // #if WITH_EDITORONLY_DATA
}
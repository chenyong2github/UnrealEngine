// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistryArchive.h"
#include "AssetRegistryPrivate.h"
#include "AssetRegistry/AssetRegistryState.h"


constexpr uint32 AssetRegistryNumberedNameBit = 0x80000000;

static void SerializeBundleEntries(FArchive& Ar, TArray<FAssetBundleEntry>& Entries)
{
	for (FAssetBundleEntry& Entry : Entries)
	{
		Ar << Entry.BundleName;

		int32 Num = Entry.BundleAssets.Num();
		Ar << Num;
		Entry.BundleAssets.SetNum(Num);

		for (FSoftObjectPath& Path : Entry.BundleAssets)
		{
			Path.SerializePath(Ar);
		}
	}
}

static void SaveBundles(FArchive& Ar, const TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>& Bundles)
{
	TArray<FAssetBundleEntry> Empty;
	TArray<FAssetBundleEntry>& Entries = Bundles ? Bundles->Bundles : Empty;

	int32 Num = Entries.Num();
	Ar << Num;

	SerializeBundleEntries(Ar, Entries);
}

static TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe> LoadBundles(FArchive& Ar)
{
	int32 Num;
	Ar << Num;

	if (Num > 0)
	{
		FAssetBundleData Temp;
		Temp.Bundles.SetNum(Num);
		SerializeBundleEntries(Ar, Temp.Bundles);

		return MakeShared<FAssetBundleData, ESPMode::ThreadSafe>(MoveTemp(Temp));
	}

	return TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>();
}

#if ALLOW_NAME_BATCH_SAVING

FAssetRegistryWriterOptions::FAssetRegistryWriterOptions(const FAssetRegistrySerializationOptions& Options)
: Tags({Options.CookTagsAsName, Options.CookTagsAsPath})
{}

FAssetRegistryWriter::FAssetRegistryWriter(const FAssetRegistryWriterOptions& Options, FArchive& Out)
: FArchiveProxy(MemWriter)
, Tags(Options.Tags)
, TargetAr(Out)
{
	check(!IsLoading());
}

static TArray<FNameEntryId> FlattenIndex(const TMap<FNameEntryId, uint32>& Names)
{
	TArray<FNameEntryId> Out;
	Out.SetNumZeroed(Names.Num());
	for (TPair<FNameEntryId, uint32> Pair : Names)
	{
		Out[Pair.Value] = Pair.Key;
	}
	return Out;
}

FAssetRegistryWriter::~FAssetRegistryWriter()
{
	// Save store data and collect FNames
	int32 BodySize = MemWriter.TotalSize();
	SaveStore(Tags.Finalize(), *this);

	// Save in load-friendly order - names, store then body / tag maps
	SaveNameBatch(FlattenIndex(Names), TargetAr);
	TargetAr.Serialize(MemWriter.GetData() + BodySize, MemWriter.TotalSize() - BodySize);
	TargetAr.Serialize(MemWriter.GetData(), BodySize);
}

FArchive& FAssetRegistryWriter::operator<<(FName& Value)
{
	FNameEntryId EntryId = Value.GetDisplayIndex();

	uint32 Index = Names.FindOrAdd(EntryId, Names.Num());
	check((Index & AssetRegistryNumberedNameBit) == 0);

	if (Value.GetNumber() != NAME_NO_NUMBER_INTERNAL)
	{
		Index |= AssetRegistryNumberedNameBit;
		uint32 Number = Value.GetNumber();
		return *this << Index << Number;
	}

	return *this << Index;
}

void SaveTags(FAssetRegistryWriter& Writer, const FAssetDataTagMapSharedView& Map)
{
	uint64 MapHandle = Writer.Tags.AddTagMap(Map).ToInt();
	Writer << MapHandle;
}

void FAssetRegistryWriter::SerializeTagsAndBundles(const FAssetData& Out)
{
	SaveTags(*this, Out.TagsAndValues);
	SaveBundles(*this, Out.TaggedAssetBundles);
}

#endif

FAssetRegistryReader::FAssetRegistryReader(FArchive& Inner, int32 NumWorkers)
	: FArchiveProxy(Inner)
{
	check(IsLoading());

	if (NumWorkers > 0)
	{
		TFunction<TArray<FNameEntryId> ()> GetFutureNames = LoadNameBatchAsync(*this, NumWorkers);

		FixedTagPrivate::FAsyncStoreLoader StoreLoader;
		Task = StoreLoader.ReadInitialDataAndKickLoad(*this, NumWorkers);
		
		Names = GetFutureNames();
		Tags = StoreLoader.LoadFinalData(*this);
	}
	else
	{
		Names = LoadNameBatch(Inner);
		Tags = FixedTagPrivate::LoadStore(*this);
	}
}

FAssetRegistryReader::~FAssetRegistryReader()
{
	WaitForTasks();
}

void FAssetRegistryReader::WaitForTasks()
{
	if (Task.IsValid())
	{
		Task.Wait();
	}
}

FArchive& FAssetRegistryReader::operator<<(FName& Out)
{
	checkf(Names.Num() > 0, TEXT("Attempted to load FName before name batch loading has finished"));

	uint32 Index = 0;
	uint32 Number = NAME_NO_NUMBER_INTERNAL;
	
	*this << Index;

	if (Index & AssetRegistryNumberedNameBit)
	{
		Index -= AssetRegistryNumberedNameBit;
		*this << Number;
	}

	Out = FName::CreateFromDisplayId(Names[Index], Number);

	return *this;
}

FAssetDataTagMapSharedView LoadTags(FAssetRegistryReader& Reader)
{
	uint64 MapHandle;
	Reader << MapHandle;
	return FAssetDataTagMapSharedView(FixedTagPrivate::FPartialMapHandle::FromInt(MapHandle).MakeFullHandle(Reader.Tags->Index));
}

void FAssetRegistryReader::SerializeTagsAndBundles(FAssetData& Out)
{
	Out.TagsAndValues = LoadTags(*this);
	Out.TaggedAssetBundles = LoadBundles(*this);
}

////////////////////////////////////////////////////////////////////////////

#if WITH_DEV_AUTOMATION_TESTS 

#include "Misc/AutomationTest.h"
#include "NameTableArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRegistryTagSerializationTest, "Engine.AssetRegistry.SerializeTagMap", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

FAssetDataTagMapSharedView MakeLooseMap(std::initializer_list<TPairInitializer<const char*, FString>> Pairs)
{
	FAssetDataTagMap Out;
	Out.Reserve(Pairs.size());
	for (TPair<const char*, FString> Pair : Pairs)
	{
		Out.Add(FName(Pair.Key), Pair.Value);
	}
	return FAssetDataTagMapSharedView(MoveTemp(Out));
}


bool FAssetRegistryTagSerializationTest::RunTest(const FString& Parameters)
{
	TArray<FAssetDataTagMapSharedView> LooseMaps;
	LooseMaps.Add(FAssetDataTagMapSharedView());
	LooseMaps.Add(MakeLooseMap({{"Key",			"StringValue"}, 
								{"Key_0",		"StringValue_0"}}));
	LooseMaps.Add(MakeLooseMap({{"Name",		"NameValue"}, 
								{"Name_0",		"NameValue_0"}}));
	LooseMaps.Add(MakeLooseMap({{"FullPath",	"C\'P.O\'"}, 
								{"PkgPath",		"P.O"},
								{"ObjPath",		"O"}}));
	LooseMaps.Add(MakeLooseMap({{"NumPath_0",	"C\'P.O_0\'"}, 
								{"NumPath_1",	"C\'P_0.O\'"},
								{"NumPath_2",	"C_0\'P.O\'"},
								{"NumPath_3",	"C\'P_0.O_0\'"},
								{"NumPath_4",	"C_0\'P_0.O\'"},
								{"NumPath_5",	"C_0\'P.O_0\'"},
								{"NumPath_6",	"C_0\'P_0.O_0\'"}}));
	LooseMaps.Add(MakeLooseMap({{"SameSame",	"SameSame"}, 
								{"AlsoSame",	"SameSame"}}));
	LooseMaps.Add(MakeLooseMap({{"FilterKey1",	"FilterValue1"}, 
								{"FilterKey2",	"FilterValue2"}}));
	LooseMaps.Add(MakeLooseMap({{"Localized",	"NSLOCTEXT(\"\", \"5F8411BA4D1A349F6E2C56BB04A1A810\", \"Content Browser Walkthrough\")"}}));
	LooseMaps.Add(MakeLooseMap({{"Wide",		TEXT("Wide\x00DF")}}));

	TArray<uint8> Data;

#if ALLOW_NAME_BATCH_SAVING
	FAssetRegistryWriterOptions Options;
	Options.Tags.StoreAsName = {	"Name", "Name_0"};
	Options.Tags.StoreAsPath = {	"FullPath", "PkgPath", "ObjPath",
									"NumPath_0", "NumPath_1", "NumPath_2",
									"NumPath_3", "NumPath_4", "NumPath_5", "NumPath_6"};
	{
		FMemoryWriter DataWriter(Data);
		FAssetRegistryWriter RegistryWriter(Options, DataWriter);
		for (const FAssetDataTagMapSharedView& LooseMap : LooseMaps)
		{
			SaveTags(RegistryWriter, LooseMap);
		}
	}
#endif

	TArray<FAssetDataTagMapSharedView> FixedMaps;
	FixedMaps.SetNum(LooseMaps.Num());

	{
		FMemoryReader DataReader(Data);
		FAssetRegistryReader RegistryReader(DataReader);
		for (FAssetDataTagMapSharedView& FixedMap : FixedMaps)
		{
			FixedMap = LoadTags(RegistryReader);
		}
	}

	TestTrue("SerializeTagsAndBundles round-trip", FixedMaps == LooseMaps);

	// Re-create second fixed tag store to test operator==(FMapHandle, FMapHandle)
	{
		FMemoryReader DataReader(Data);
		FAssetRegistryReader RegistryReader(DataReader);

		for (const FAssetDataTagMapSharedView& FixedMap1 : FixedMaps)
		{
			FAssetDataTagMapSharedView FixedMap2 = LoadTags(RegistryReader);
			TestTrue("Fixed tag map equality", FixedMap1 == FixedMap2);
		}
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
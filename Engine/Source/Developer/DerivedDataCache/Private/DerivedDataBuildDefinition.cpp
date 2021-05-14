// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildDefinition.h"

#include "Algo/AllOf.h"
#include "Containers/Map.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataBuildPrivate.h"
#include "Misc/Guid.h"
#include "Misc/TVariant.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include <atomic>

namespace UE::DerivedData::Private
{

class FBuildDefinitionBuilderInternal final : public IBuildDefinitionBuilderInternal
{
public:
	inline FBuildDefinitionBuilderInternal(FStringView InName, FStringView InFunction)
		: Name(InName)
		, Function(InFunction)
	{
		checkf(!Name.IsEmpty(), TEXT("A build definition requires a non-empty name."));
		checkf(!Function.IsEmpty(), TEXT("A build definition requires a non-empty function name for build of '%s'."), *Name);
		checkf(Algo::AllOf(Function, FChar::IsAlnum), TEXT("Definition requires an alphanumeric function name for ")
			TEXT("build of '%s' by %s."), *Name, *Function);
	}

	~FBuildDefinitionBuilderInternal() final = default;

	void AddConstant(FStringView Key, const FCbObject& Value) final
	{
		Add<FCbObject>(Key, Value);
	}

	void AddInputBuild(FStringView Key, const FBuildPayloadKey& PayloadKey) final
	{
		Add<FBuildPayloadKey>(Key, PayloadKey);
	}

	void AddInputBulkData(FStringView Key, const FGuid& BulkDataId) final
	{
		Add<FGuid>(Key, BulkDataId);
	}

	void AddInputFile(FStringView Key, FStringView Path) final
	{
		Add<FString>(Key, Path);
	}

	void AddInputHash(FStringView Key, const FIoHash& RawHash) final
	{
		Add<FIoHash>(Key, RawHash);
	}

	FBuildDefinition Build() final;

	using InputType = TVariant<FCbObject, FBuildPayloadKey, FGuid, FString, FIoHash>;

	FString Name;
	FString Function;
	TMap<FString, InputType> Inputs;

private:
	template <typename ValueType, typename ArgType>
	inline void Add(FStringView Key, ArgType&& Value)
	{
		const uint32 KeyHash = GetTypeHash(Key);
		checkf(!Key.IsEmpty(), TEXT("Empty key used in definition for build of '%s' by %s."), *Name, *Function);
		checkf(!Inputs.ContainsByHash(KeyHash, Key), TEXT("Duplicate key '%.*s' used in definition for "),
			TEXT("build of '%s' by %s."), Key.Len(), Key.GetData(), *Name, *Function);
		Inputs.EmplaceByHash(KeyHash, Key, InputType(TInPlaceType<ValueType>(), Forward<ArgType>(Value)));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildDefinitionInternal final : public IBuildDefinitionInternal
{
public:
	explicit FBuildDefinitionInternal(FBuildDefinitionBuilderInternal&& DefinitionBuilder);
	explicit FBuildDefinitionInternal(FStringView Name, FCbObject&& Definition, bool& bOutIsValid);

	~FBuildDefinitionInternal() final = default;

	const FBuildKey& GetKey() const final { return Key; }

	FStringView GetName() const final { return Name; }
	FStringView GetFunction() const final { return Function; }

	bool HasConstants() const final;
	bool HasInputs() const final;

	void IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const final;
	void IterateInputBuilds(TFunctionRef<void (FStringView Key, const FBuildPayloadKey& PayloadKey)> Visitor) const final;
	void IterateInputBulkData(TFunctionRef<void (FStringView Key, const FGuid& BulkDataId)> Visitor) const final;
	void IterateInputFiles(TFunctionRef<void (FStringView Key, FStringView Path)> Visitor) const final;
	void IterateInputHashes(TFunctionRef<void (FStringView Key, const FIoHash& RawHash)> Visitor) const final;

	void Save(FCbWriter& Writer) const final;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	mutable std::atomic<uint32> ReferenceCount{0};
	FString Name;
	FString Function;
	FCbObject Definition;
	FBuildKey Key;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildDefinitionInternal::FBuildDefinitionInternal(FBuildDefinitionBuilderInternal&& DefinitionBuilder)
	: Name(MoveTemp(DefinitionBuilder.Name))
	, Function(MoveTemp(DefinitionBuilder.Function))
{
	DefinitionBuilder.Inputs.KeySort(TLess<>());

	bool bHasConstants = false;
	bool bHasBuilds = false;
	bool bHasBulkData = false;
	bool bHasFiles = false;
	bool bHasHashes = false;

	for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
	{
		if (Pair.Value.IsType<FCbObject>())
		{
			bHasConstants = true;
		}
		else if (Pair.Value.IsType<FBuildPayloadKey>())
		{
			bHasBuilds = true;
		}
		else if (Pair.Value.IsType<FGuid>())
		{
			bHasBulkData = true;
		}
		else if (Pair.Value.IsType<FString>())
		{
			bHasFiles = true;
		}
		else if (Pair.Value.IsType<FIoHash>())
		{
			bHasHashes = true;
		}
	}

	const bool bHasInputs = bHasBuilds | bHasBulkData | bHasFiles | bHasHashes;

	TCbWriter<2048> Writer;
	Writer.BeginObject();
	Writer.AddString("Function"_ASV, Function);

	if (bHasConstants)
	{
		Writer.BeginObject("Constants"_ASV);
		for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FCbObject>())
			{
				Writer.AddObject(FTCHARToUTF8(Pair.Key), Pair.Value.Get<FCbObject>());
			}
		}
		Writer.EndObject();
	}

	if (bHasInputs)
	{
		Writer.BeginObject("Inputs"_ASV);
	}

	if (bHasBuilds)
	{
		Writer.BeginObject("Builds"_ASV);
		for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FBuildPayloadKey>())
			{
				const FBuildPayloadKey& PayloadKey = Pair.Value.Get<FBuildPayloadKey>();
				Writer.BeginObject(FTCHARToUTF8(Pair.Key));
				Writer.AddHash("Build"_ASV, PayloadKey.BuildKey.Hash);
				Writer.AddObjectId("Payload"_ASV, FCbObjectId(PayloadKey.Id.GetView()));
				Writer.EndObject();
			}
		}
		Writer.EndObject();
	}

	if (bHasBulkData)
	{
		Writer.BeginObject("BulkData"_ASV);
		for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FGuid>())
			{
				Writer.AddUuid(FTCHARToUTF8(Pair.Key), Pair.Value.Get<FGuid>());
			}
		}
		Writer.EndObject();
	}

	if (bHasFiles)
	{
		Writer.BeginObject("Files"_ASV);
		for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FString>())
			{
				Writer.AddString(FTCHARToUTF8(Pair.Key), Pair.Value.Get<FString>());
			}
		}
		Writer.EndObject();
	}

	if (bHasHashes)
	{
		Writer.BeginObject("Hashes"_ASV);
		for (const TPair<FString, FBuildDefinitionBuilderInternal::InputType>& Pair : DefinitionBuilder.Inputs)
		{
			if (Pair.Value.IsType<FIoHash>())
			{
				Writer.AddBinaryAttachment(FTCHARToUTF8(Pair.Key), Pair.Value.Get<FIoHash>());
			}
		}
		Writer.EndObject();
	}

	if (bHasInputs)
	{
		Writer.EndObject();
	}

	Writer.EndObject();
	Definition = Writer.Save().AsObject();
	Key.Hash = Definition.GetHash();
}

FBuildDefinitionInternal::FBuildDefinitionInternal(FStringView InName, FCbObject&& InDefinition, bool& bOutIsValid)
	: Name(InName)
	, Function(InDefinition.FindView("Function"_ASV).AsString())
	, Definition(MoveTemp(InDefinition))
	, Key{Definition.GetHash()}
{
	checkf(!InName.IsEmpty(), TEXT("A build definition requires a non-empty name."));
	Definition.MakeOwned();
	bOutIsValid = Definition
		&& !Function.IsEmpty() && Algo::AllOf(Function, FChar::IsAlnum)
		&& Algo::AllOf(Definition.FindView("Constants"_ASV),
			[](FCbFieldView Field) { return Field.GetName().Len() > 0 && Field.IsObject(); })
		&& Algo::AllOf(Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Builds"_ASV), [](FCbFieldView Field)
			{
				return Field.GetName().Len() > 0 && Field.IsObject()
					&& Field.AsObjectView()["Build"_ASV].IsHash()
					&& Field.AsObjectView()["Payload"_ASV].IsObjectId();
			})
		&& Algo::AllOf(Definition.FindView("Inputs"_ASV).AsObjectView().FindView("BulkData"_ASV),
			[](FCbFieldView Field) { return Field.GetName().Len() > 0 && Field.IsUuid(); })
		&& Algo::AllOf(Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Files"_ASV),
			[](FCbFieldView Field) { return Field.GetName().Len() > 0 && Field.AsString().Len() > 0; })
		&& Algo::AllOf(Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Hashes"_ASV),
			[](FCbFieldView Field) { return Field.GetName().Len() > 0 && Field.IsBinaryAttachment(); });
}

bool FBuildDefinitionInternal::HasConstants() const
{
	return Definition["Constants"_ASV].HasValue();
}

bool FBuildDefinitionInternal::HasInputs() const
{
	return Definition["Inputs"_ASV].HasValue();
}

void FBuildDefinitionInternal::IterateConstants(TFunctionRef<void (FStringView Key, FCbObject&& Value)> Visitor) const
{
	for (FCbField Field : Definition["Constants"_ASV])
	{
		Visitor(FUTF8ToTCHAR(Field.GetName()), Field.AsObject());
	}
}

void FBuildDefinitionInternal::IterateInputBuilds(TFunctionRef<void (FStringView Key, const FBuildPayloadKey& PayloadKey)> Visitor) const
{
	for (FCbFieldView Field : Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Builds"_ASV))
	{
		FCbObjectView Build = Field.AsObjectView();
		const FBuildKey BuildKey{Build["Build"_ASV].AsHash()};
		const FPayloadId Id = FPayloadId(Build["Payload"_ASV].AsObjectId().GetView());
		Visitor(FUTF8ToTCHAR(Field.GetName()), FBuildPayloadKey{BuildKey, Id});
	}
}

void FBuildDefinitionInternal::IterateInputBulkData(TFunctionRef<void (FStringView Key, const FGuid& BulkDataId)> Visitor) const
{
	for (FCbFieldView Field : Definition.FindView("Inputs"_ASV).AsObjectView().FindView("BulkData"_ASV))
	{
		Visitor(FUTF8ToTCHAR(Field.GetName()), Field.AsUuid());
	}
}

void FBuildDefinitionInternal::IterateInputFiles(TFunctionRef<void (FStringView Key, FStringView Path)> Visitor) const
{
	for (FCbFieldView Field : Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Files"_ASV))
	{
		Visitor(FUTF8ToTCHAR(Field.GetName()), FUTF8ToTCHAR(Field.AsString()));
	}
}

void FBuildDefinitionInternal::IterateInputHashes(TFunctionRef<void (FStringView Key, const FIoHash& RawHash)> Visitor) const
{
	for (FCbFieldView Field : Definition.FindView("Inputs"_ASV).AsObjectView().FindView("Hashes"_ASV))
	{
		Visitor(FUTF8ToTCHAR(Field.GetName()), Field.AsBinaryAttachment());
	}
}

void FBuildDefinitionInternal::Save(FCbWriter& Writer) const
{
	Writer.AddObject(Definition);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildDefinition FBuildDefinitionBuilderInternal::Build()
{
	return CreateBuildDefinition(new FBuildDefinitionInternal(MoveTemp(*this)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildDefinition CreateBuildDefinition(IBuildDefinitionInternal* Definition)
{
	return FBuildDefinition(Definition);
}

FBuildDefinitionBuilder CreateBuildDefinitionBuilder(IBuildDefinitionBuilderInternal* DefinitionBuilder)
{
	return FBuildDefinitionBuilder(DefinitionBuilder);
}

FBuildDefinitionBuilder CreateBuildDefinition(FStringView Name, FStringView Function)
{
	return CreateBuildDefinitionBuilder(new FBuildDefinitionBuilderInternal(Name, Function));
}

FOptionalBuildDefinition LoadBuildDefinition(FStringView Name, FCbObject&& Definition)
{
	bool bIsValid = false;
	FOptionalBuildDefinition Out = CreateBuildDefinition(new FBuildDefinitionInternal(Name, MoveTemp(Definition), bIsValid));
	if (!bIsValid)
	{
		Out.Reset();
	}
	return Out;
}

} // UE::DerivedData::Private

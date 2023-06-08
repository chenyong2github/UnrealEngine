// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCompileHash.h"
#include "NiagaraGraphDigest.h"
#include "UObject/GCObject.h"

class UNiagaraCompilationGraph;
class UNiagaraGraph;
class UNiagaraParameterCollection;
class UNiagaraScriptSourceBase;

// base class used to reference a digested version of a UObject.  
template<typename AssetType, typename DigestType, typename HashType>
class FNiagaraDigestedAssetHandle
{
public:
	using FDigestPtr = TSharedPtr<DigestType, ESPMode::ThreadSafe>;
	FNiagaraDigestedAssetHandle() = default;
	virtual ~FNiagaraDigestedAssetHandle() = default;
	FNiagaraDigestedAssetHandle(const FNiagaraDigestedAssetHandle& Handle) = default;

	bool IsValid() const
	{
		return Resolve().IsValid();
	}
	virtual FDigestPtr Resolve() const = 0;

protected:
	TObjectKey<AssetType> AssetKey;
	HashType Hash;

	friend FORCEINLINE uint32 GetTypeHash(const FNiagaraDigestedAssetHandle& CompilationCopy)
	{
		return HashCombine(GetTypeHash(CompilationCopy.AssetKey), GetTypeHash(CompilationCopy.Hash));
	}

	friend FORCEINLINE bool operator==(const FNiagaraDigestedAssetHandle& Lhs, const FNiagaraDigestedAssetHandle& Rhs)
	{
		return Lhs.AssetKey == Rhs.AssetKey && Lhs.Hash == Rhs.Hash;
	}
};

class FNiagaraCompilationGraphHandle : public FNiagaraDigestedAssetHandle<UNiagaraGraph, FNiagaraCompilationGraph, FGuid>
{
public:
	using Super = FNiagaraDigestedAssetHandle<UNiagaraGraph, FNiagaraCompilationGraph, FGuid>;
	using FGraphPtr = Super::FDigestPtr;

	FNiagaraCompilationGraphHandle() = default;
	FNiagaraCompilationGraphHandle(const FNiagaraCompilationGraphHandle& Handle) = default;
	FNiagaraCompilationGraphHandle(const UNiagaraScriptSourceBase* ScriptSource);
	FNiagaraCompilationGraphHandle(const UNiagaraGraph* Graph);

	virtual FGraphPtr Resolve() const override;
};

class FNiagaraCompilationNPC
{
public:
	void Create(const UNiagaraParameterCollection* Collection);
	void AddReferencedObjects(FReferenceCollector& Collector);

	UNiagaraDataInterface* GetDataInterface(const FNiagaraVariableBase& Variable) const;

	TArray<FNiagaraVariableBase> Variables;
	FName Namespace;
	TWeakObjectPtr<const UNiagaraParameterCollection> SourceCollection;
	FString CollectionPath;
	FString CollectionFullName;
	TMap<FNiagaraVariableBase, UNiagaraDataInterface*> DefaultDataInterfaces;
};

class FNiagaraCompilationNPCHandle : public FNiagaraDigestedAssetHandle<UNiagaraParameterCollection, FNiagaraCompilationNPC, FNiagaraCompileHash>
{
public:
	using Super = FNiagaraDigestedAssetHandle<UNiagaraParameterCollection, FNiagaraCompilationNPC, FNiagaraCompileHash>;

	FNiagaraCompilationNPCHandle() = default;
	FNiagaraCompilationNPCHandle(const FNiagaraCompilationNPCHandle& Handle) = default;
	FNiagaraCompilationNPCHandle(const UNiagaraParameterCollection* Collection);

	virtual FDigestPtr Resolve() const override;

	FName Namespace = NAME_None;
};

class FNiagaraDigestedParameterCollections
{
public:
	TArray<FNiagaraCompilationNPCHandle>& EditCollections() { return Collections; };

	FNiagaraCompilationNPCHandle FindMatchingCollection(FName VariableName, bool bAllowPartialMatch, FNiagaraVariable& OutVar) const;
	FNiagaraCompilationNPCHandle FindCollection(const FNiagaraVariable& Variable) const;

protected:
	FNiagaraCompilationNPCHandle FindCollectionByName(FName VariableName) const;

	TArray<FNiagaraCompilationNPCHandle> Collections;
};

class FNiagaraDigestDatabase : public FGCObject
{
public:
	FNiagaraDigestDatabase();
	virtual ~FNiagaraDigestDatabase();

	static FNiagaraDigestDatabase& Get();
	static void Shutdown();

	FNiagaraCompilationGraphHandle CreateCompilationCopy(const UNiagaraGraph* Graph);
	FNiagaraCompilationGraphHandle::FGraphPtr Resolve(const FNiagaraCompilationGraphHandle& Handle) const;

	FNiagaraCompilationNPCHandle CreateCompilationCopy(const UNiagaraParameterCollection* Collection);
	FNiagaraCompilationNPCHandle::FDigestPtr Resolve(const FNiagaraCompilationNPCHandle& Handle) const;

	void ReleaseDatabase();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	
	virtual FString GetReferencerName() const override;

protected:
	uint32 GraphCacheHits = 0;
	uint32 GraphCacheMisses = 0;
	uint32 CollectionCacheHits = 0;
	uint32 CollectionCacheMisses = 0;

	// todo - replace these maps with some kind of two tiered system where we've got an TLruCache backing
	// those objects we've digested recently and then another layer of digested objects that we've got pinned
	// for the lifetime of a compilation task.  For now we'll just have an ever growing map

	using FCompilationGraphCache = TMap<FNiagaraCompilationGraphHandle, FNiagaraCompilationGraphHandle::FGraphPtr>;
	FCompilationGraphCache CompilationGraphCache;

	using FCompilationNPCCache = TMap<FNiagaraCompilationNPCHandle, FNiagaraCompilationNPCHandle::FDigestPtr>;
	FCompilationNPCCache CompilationNPCCache;

	mutable FRWLock DigestCacheLock;
};
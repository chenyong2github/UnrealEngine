// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDigestDatabase.h"

#include "Misc/LazySingleton.h"
#include "NiagaraEditorModule.h"
#include "NiagaraGraph.h"
#include "NiagaraGraphDigest.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"

namespace NiagaraGraphDigestDatabaseImpl
{

// todo - two tiered cache for digested objects (see NiagaraDigestDatabase.h)
//static int GDigestGraphCacheSize = 256;
//static FAutoConsoleVariableRef CVarDigestGraphCacheSize(
//	TEXT("fx.Niagara.DigestGraphCacheSize"),
//	GDigestGraphCacheSize,
//	TEXT("Defines the size of the cache for digested Niagara graphs."),
//	ECVF_ReadOnly
//);
//
//static int GDigestCollectionCacheSize = 32;
//static FAutoConsoleVariableRef CVarDigestCollectionCacheSize(
//	TEXT("fx.Niagara.DigestCollectionCacheSize"),
//	GDigestCollectionCacheSize,
//	TEXT("Defines the size of the cache for digested Niagara parameter collections."),
//	ECVF_ReadOnly
//);

FNiagaraDigestDatabase* GDigestDatabase = nullptr;

} // NiagaraGraphDigestDatabaseImpl::

FNiagaraDigestDatabase& FNiagaraDigestDatabase::Get()
{
	if (!NiagaraGraphDigestDatabaseImpl::GDigestDatabase)
	{
		NiagaraGraphDigestDatabaseImpl::GDigestDatabase = &TLazySingleton<FNiagaraDigestDatabase>::Get();
	}

	return *NiagaraGraphDigestDatabaseImpl::GDigestDatabase;
}

void FNiagaraDigestDatabase::Shutdown()
{
	Get().ReleaseDatabase();
}

FNiagaraDigestDatabase::FNiagaraDigestDatabase()
	// todo - two tiered cache for digested objects (see NiagaraDigestDatabase.h)
	//: CompilationGraphCache(NiagaraGraphDigestDatabaseImpl::GDigestGraphCacheSize)
	//, CompilationNPCCache(NiagaraGraphDigestDatabaseImpl::GDigestCollectionCacheSize)
{

}

FNiagaraDigestDatabase::~FNiagaraDigestDatabase()
{
	ReleaseDatabase();
}

void FNiagaraDigestDatabase::ReleaseDatabase()
{
	FWriteScopeLock WriteScope(DigestCacheLock);

	CompilationGraphCache.Empty();
	CompilationNPCCache.Empty();
}

void FNiagaraDigestDatabase::AddReferencedObjects(FReferenceCollector& Collector)
{
	FReadScopeLock ReadScope(DigestCacheLock);

	for (FCompilationGraphCache::TIterator It(CompilationGraphCache); It; ++It)
	{
		It.Value()->AddReferencedObjects(Collector);
	}

	for (FCompilationNPCCache::TIterator It(CompilationNPCCache); It; ++It)
	{
		It.Value()->AddReferencedObjects(Collector);
	}
}

FString FNiagaraDigestDatabase::GetReferencerName() const
{
	static const FString ReferencerName = TEXT("NiagaraDigestDatabsae");
	return ReferencerName;
}

//////////////////////////////////////////////////////////////////////////
/// Digested graphs

FNiagaraCompilationGraphHandle FNiagaraDigestDatabase::CreateCompilationCopy(const UNiagaraGraph* Graph)
{
	check(IsInGameThread());
	FNiagaraCompilationGraphHandle GraphHash(Graph);
	FNiagaraCompilationGraph* PendingGraph = nullptr;

	{
		FWriteScopeLock WriteScope(DigestCacheLock);

		if (const FNiagaraCompilationGraphHandle::FGraphPtr* CompilationGraph = CompilationGraphCache.Find(GraphHash))
		{
			++GraphCacheHits;
			return GraphHash;
		}

		++GraphCacheMisses;
		FNiagaraCompilationGraphHandle::FGraphPtr CompilationGraph = MakeShared<FNiagaraCompilationGraphHandle::FGraphPtr::ElementType, FNiagaraCompilationGraphHandle::FGraphPtr::Mode>();
		PendingGraph = CompilationGraph.Get();

		CompilationGraphCache.Add(GraphHash, CompilationGraph);
	}

	if (PendingGraph)
	{
		PendingGraph->Create(Graph);
	}

	return GraphHash;
}

FNiagaraCompilationGraphHandle::FGraphPtr FNiagaraDigestDatabase::Resolve(const FNiagaraCompilationGraphHandle& Handle) const
{
	FNiagaraCompilationGraphHandle::FGraphPtr GraphRef;

	{
		FReadScopeLock ReadScope(DigestCacheLock);
		GraphRef = CompilationGraphCache.FindRef(Handle);
	}

	check(GraphRef.IsValid());

	return GraphRef;
}

FNiagaraCompilationGraphHandle::FNiagaraCompilationGraphHandle(const UNiagaraScriptSourceBase* ScriptSourceBase)
{
	if (const UNiagaraScriptSource* ScriptSource = CastChecked<const UNiagaraScriptSource>(ScriptSourceBase))
	{
		if (const UNiagaraGraph* Graph = ScriptSource->NodeGraph)
		{
			AssetKey = Graph;
			Hash = Graph->GetChangeID();
		}
	}
}

FNiagaraCompilationGraphHandle::FNiagaraCompilationGraphHandle(const UNiagaraGraph* Graph)
{
	AssetKey = Graph;
	Hash = Graph ? Graph->GetChangeID() : FGuid();
}

FNiagaraCompilationGraphHandle::FGraphPtr FNiagaraCompilationGraphHandle::Resolve() const
{
	return FNiagaraDigestDatabase::Get().Resolve(*this);
}

//////////////////////////////////////////////////////////////////////////
/// Digested parameter collection

FNiagaraCompilationNPCHandle FNiagaraDigestDatabase::CreateCompilationCopy(const UNiagaraParameterCollection* Collection)
{
	check(IsInGameThread());
	FNiagaraCompilationNPCHandle CollectionHash(Collection);
	FNiagaraCompilationNPC* PendingCollection = nullptr;

	{
		FWriteScopeLock WriteScope(DigestCacheLock);

		if (const FNiagaraCompilationNPCHandle::FDigestPtr* CompilationCollection = CompilationNPCCache.Find(CollectionHash))
		{
			++CollectionCacheHits;
			return CollectionHash;
		}

		++CollectionCacheMisses;
		FNiagaraCompilationNPCHandle::FDigestPtr CompilationCollection = MakeShared<FNiagaraCompilationNPCHandle::FDigestPtr::ElementType, FNiagaraCompilationNPCHandle::FDigestPtr::Mode>();
		PendingCollection = CompilationCollection.Get();

		CompilationNPCCache.Add(CollectionHash, CompilationCollection);
	}

	if (PendingCollection)
	{
		PendingCollection->Create(Collection);
	}

	return CollectionHash;
}

FNiagaraCompilationNPCHandle::FDigestPtr FNiagaraDigestDatabase::Resolve(const FNiagaraCompilationNPCHandle& Handle) const
{
	FReadScopeLock ReadScope(DigestCacheLock);

	return CompilationNPCCache.FindRef(Handle);
}

void FNiagaraCompilationNPC::Create(const UNiagaraParameterCollection* Collection)
{
	SourceCollection = Collection;
	Namespace = Collection->GetNamespace();
	CollectionPath = FSoftObjectPath(Collection).ToString();
	CollectionFullName = GetFullNameSafe(Collection);

	Variables.Reserve(Collection->GetParameters().Num());
	for (const FNiagaraVariable& Parameter : Collection->GetParameters())
	{
		Variables.Emplace(Parameter);
	}

	{ // we also need to deal with any data interfaces that might be stored in the NPC
		UPackage* TransientPackage = GetTransientPackage();

		const FNiagaraParameterStore& DefaultParamStore = Collection->GetDefaultInstance()->GetParameterStore();
		for (const FNiagaraVariableBase& Variable : Variables)
		{
			const int32 VariableOffset = DefaultParamStore.IndexOf(Variable);
			if (VariableOffset != INDEX_NONE)
			{
				if (UNiagaraDataInterface* DefaultDataInterface = DefaultParamStore.GetDataInterface(VariableOffset))
				{
					UNiagaraDataInterface* DuplicateDataInterface = DuplicateObject<UNiagaraDataInterface>(DefaultDataInterface, TransientPackage);

					DefaultDataInterfaces.Add(Variable, DuplicateDataInterface);
				}
			}
		}
	}
}

void FNiagaraCompilationNPC::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddStableReferenceMap(DefaultDataInterfaces);
}

UNiagaraDataInterface* FNiagaraCompilationNPC::GetDataInterface(const FNiagaraVariableBase& Variable) const
{
	return DefaultDataInterfaces.FindRef(Variable);
}

FNiagaraCompilationNPCHandle::FNiagaraCompilationNPCHandle(const UNiagaraParameterCollection* Connection)
	: Namespace(Connection ? Connection->GetNamespace() : NAME_None)
{
	AssetKey = Connection;
	Hash = Connection->GetCompileHash();
}

FNiagaraCompilationNPCHandle::FDigestPtr FNiagaraCompilationNPCHandle::Resolve() const
{
	return FNiagaraDigestDatabase::Get().Resolve(*this);
}
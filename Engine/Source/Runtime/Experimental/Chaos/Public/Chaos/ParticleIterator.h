// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{

template <typename ParticleView, typename Lambda>
void ParticlesParallelFor(const ParticleView& Particles, const Lambda& Func, bool bForceSingleThreaded = false)
{
	//todo: make it actually parallel
	int32 Index = 0;
	for (auto& Particle : Particles)
	{
		Func(Particle, Index++);
	}
}

template <typename TSOA>
class TConstHandleIterator
{
public:
	using THandle = typename TSOA::THandleType;
	TConstHandleIterator(const TArray<THandle*>& InHandles, int32 StartOffset = 0)
		: Handles(InHandles)
		, CurIdx(0)
	{
		Advance(StartOffset);
	}

	operator bool() const { return CurIdx < Handles.Num(); }
	TConstHandleIterator<TSOA>& operator++()
	{
		++CurIdx;
		return *this;
	}

	TConstHandleIterator<TSOA>& Advance(int32 Offset)
	{
		CurIdx = FMath::Min(CurIdx + Offset, Handles.Num());
		return *this;
	}

	const THandle& operator*() const
	{
		return static_cast<const THandle&>(*Handles[CurIdx]);
	}

	const THandle* operator->() const
	{
		return static_cast<const THandle*>(Handles[CurIdx]);
	}

	int32 ComputeSize() const
	{
		return Handles.Num();
	}

	template <typename Lambda>
	void ParallelFor(const Lambda& Func) const
	{
		ParticlesParallelFor(*this, Func);
	}

protected:
	template <typename TSOA2>
	friend class TConstHandleView;

	const TArray<THandle*>& Handles;
	int32 CurIdx;
};

template <typename TSOA>
class THandleIterator : public TConstHandleIterator<TSOA>
{
public:
	using Base = TConstHandleIterator<TSOA>;
	using typename Base::THandle;
	using Base::Handles;
	using Base::CurIdx;

	THandleIterator(const TArray<THandle*>& InHandles, int32 StartOffset = 0)
		: Base(InHandles, StartOffset)
	{
	}

	THandle& operator*() const
	{
		return static_cast<THandle&>(*Handles[CurIdx]);
	}

	THandle* operator->() const
	{
		return static_cast<THandle*>(Handles[CurIdx]);
	}

	template <typename Lambda>
	void ParallelFor(const Lambda& Func) const
	{
		ParticlesParallelFor(*this, Func);
	}

	template <typename TSOA2>
	friend class THandleView;
};

template <typename THandle>
TConstHandleIterator<typename THandle::TSOAType> MakeConstHandleIterator(const TArray<THandle*>& Handles)
{
	return TConstHandleIterator<typename THandle::TSOAType>(Handles);
}

template <typename THandle>
THandleIterator<typename THandle::TSOAType> MakeHandleIterator(const TArray<THandle*>& Handles)
{
	return THandleIterator<typename THandle::TSOAType>(Handles);
}

template <typename TSOA>
class TConstHandleView
{
public:
	using THandle = typename TSOA::THandleType;
	TConstHandleView()
	{
	}

	TConstHandleView(const TArray<THandle*>& InHandles)
		: Handles(InHandles)
	{
	}

	int32 Num() const
	{
		return Handles.Num();
	}

	TConstHandleIterator<TSOA> Begin() const { return MakeConstHandleIterator(Handles); }
	TConstHandleIterator<TSOA> begin() const { return Begin(); }

	TConstHandleIterator<TSOA> End() const { return TConstHandleIterator<TSOA>(Handles, Num()); }
	TConstHandleIterator<TSOA> end() const { return End(); }

	template <typename Lambda>
	void ParallelFor(const Lambda& Func) const
	{
		ParticlesParallelFor(*this, Func);
	}

protected:

	const TArray<THandle*>& Handles;
};

template <typename TSOA>
class THandleView : public TConstHandleView<TSOA>
{
public:
	using Base = TConstHandleView<TSOA>;
	using typename Base::THandle;
	using Base::Handles;
	using Base::Num;

	THandleView()
		: Base()
	{
	}

	THandleView(const TArray<THandle*>& InHandles)
		: Base(InHandles)
	{
	}

	THandleIterator<TSOA> Begin() const { return MakeHandleIterator(Handles); }
	THandleIterator<TSOA> begin() const { return Begin(); }

	THandleIterator<TSOA> End() const { return THandleIterator<TSOA>(Handles, Num()); }
	THandleIterator<TSOA> end() const { return End(); }

	template <typename Lambda>
	void ParallelFor(const Lambda& Func) const
	{
		ParticlesParallelFor(*this, Func);
	}
};

template <typename THandle>
TConstHandleView<typename THandle::TSOAType> MakeConstHandleView(const TArray<THandle*>& Handles)
{
	return TConstHandleView<typename THandle::TSOAType>(Handles);
}

template <typename THandle>
THandleView<typename THandle::TSOAType> MakeHandleView(const TArray<THandle*>& Handles)
{
	return THandleView<typename THandle::TSOAType>(Handles);
}

template <typename TSOA>
struct TSOAView
{
	using THandle = typename TSOA::THandleType;
	TSOAView(TSOA* InSOA)
		: SOA(InSOA)
		, HandlesArray(nullptr)
	{}

	template <typename TDerivedHandle>
	TSOAView(TArray<TDerivedHandle*>* Handles)
		: SOA(nullptr)
	{
		static_assert(TIsDerivedFrom< TDerivedHandle, THandle >::IsDerived, "Trying to create a derived view on a base type");

		//This is safe because the view is strictly read only in terms of the pointers
		//I.e. we will never be in a case where we create a new base type and assign it to a derived pointer
		//We are only using this to read data from the pointers and so having a more base API (which cannot modify the pointer) is safe
		HandlesArray = reinterpret_cast<TArray<THandle*>*>(Handles);
	}

	TSOA* SOA;
	TArray<THandle*>* HandlesArray;

	int32 Size() const
	{
		return HandlesArray ? HandlesArray->Num() : SOA->Size();
	}
};

template <typename TSOA>
class TConstParticleIterator
{
public:
	using THandle = typename TSOA::THandleType;
	using THandleBase = typename THandle::THandleBase;
	using TTransientHandle = typename THandle::TTransientHandle;

	TConstParticleIterator()
	{
		MoveToEnd();
	}

	TConstParticleIterator(const TArray<TSOAView<TSOA>>& InSOAViews)
		: SOAViews(&InSOAViews)
		, CurHandlesArray(nullptr)
		, SOAIdx(0)
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		, DirtyValidationCount(INDEX_NONE)
#endif
	{
		SeekNonEmptySOA();
	}

	TConstParticleIterator(const TConstParticleIterator& Rhs) = default;

	operator bool() const { return TransientHandle.GeometryParticles != nullptr; }
	TConstParticleIterator<TSOA>& operator++()
	{
		RangedForValidation();
		if (CurHandlesArray == nullptr)
		{
			//SOA is packed efficiently for iteration
			++TransientHandle.ParticleIdx;
			if (TransientHandle.ParticleIdx >= static_cast<int32>(CurSOASize))
			{
				IncSOAIdx();
			}
		}
		else
		{
			++CurHandleIdx;
			if (CurHandleIdx < CurHandlesArray->Num())
			{
				TransientHandle.ParticleIdx = (*CurHandlesArray)[CurHandleIdx]->ParticleIdx;
			}
			else
			{
				IncSOAIdx();
			}
		}

		return *this;
	}

	
	const TTransientHandle& operator*() const
	{
		RangedForValidation();
		return static_cast<const TTransientHandle&>(TransientHandle);
	}

	const TTransientHandle* operator->() const
	{
		RangedForValidation();
		return static_cast<const TTransientHandle*>(&TransientHandle);
	}

protected:

	void MoveToEnd()
	{
		SOAIdx = 0;
		CurSOASize = 0;
		CurHandleIdx = 0;
		TransientHandle = THandleBase();
		CurHandlesArray = nullptr;
	}

	void SeekNonEmptySOA()
	{
		while (SOAIdx < SOAViews->Num() && ((*SOAViews)[SOAIdx].Size() == 0))
		{
			++SOAIdx;
		}
		CurHandleIdx = 0;
		if (SOAIdx < SOAViews->Num())
		{
			CurHandlesArray = (*SOAViews)[SOAIdx].HandlesArray;
			TransientHandle = CurHandlesArray ? THandleBase((*CurHandlesArray)[0]->GeometryParticles, (*CurHandlesArray)[0]->ParticleIdx) : THandleBase((*SOAViews)[SOAIdx].SOA, 0);
			CurSOASize = TransientHandle.GeometryParticles->Size();
		}
		else
		{
			MoveToEnd();
		}
		SyncDirtyValidationCount();
		RangedForValidation();
	}

	const TArray<TSOAView<TSOA>>* SOAViews;
	const TArray<THandle*>* CurHandlesArray;
	THandleBase TransientHandle;
	int32 SOAIdx;
	int32 CurSOASize;
	int32 CurHandleIdx;

	void RangedForValidation() const
	{
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		if (TransientHandle.GeometryParticles)
		{
			check(DirtyValidationCount != INDEX_NONE);
			check(TransientHandle.GeometryParticles->DirtyValidationCount() == DirtyValidationCount && TEXT("Iterating over particles while modifying the underlying SOA. Consider delaying any operations that require a Handle*"));
		}
#endif
	}

	void IncSOAIdx()
	{
		++SOAIdx;
		SeekNonEmptySOA();
	}

	void SyncDirtyValidationCount()
	{

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		DirtyValidationCount = TransientHandle.GeometryParticles ? TransientHandle.GeometryParticles->DirtyValidationCount() : INDEX_NONE;
#endif
	}

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
	int32 DirtyValidationCount;
#endif
};

template <typename TSOA>
class TParticleIterator : public TConstParticleIterator<TSOA>
{
public:
	using Base = TConstParticleIterator<TSOA>;
	using TTransientHandle = typename Base::TTransientHandle;
	using Base::TransientHandle;
	using Base::RangedForValidation;

	TParticleIterator()
		: Base()
	{
	}

	TParticleIterator(const TArray<TSOAView<TSOA>>& InSOAs)
		: Base(InSOAs)
	{
	}

	TTransientHandle& operator*() const
	{
		RangedForValidation();
		//const_cast ok because the const is operation is really about the iterator.The transient handle cannot change
		return const_cast<TTransientHandle&>(static_cast<const TTransientHandle&>(TransientHandle));
	}

	TTransientHandle* operator->() const
	{
		RangedForValidation();
		//const_cast ok because the const is operation is really about the iterator. The transient handle cannot change
		return const_cast<TTransientHandle*>(static_cast<const TTransientHandle*>(&TransientHandle));
	}
};

template <typename TSOA>
TConstParticleIterator<TSOA> MakeConstParticleIterator(const TArray<TSOAView<TSOA>>& SOAs)
{
	return TConstParticleIterator<TSOA>(SOAs);
}

template <typename TSOA>
TParticleIterator<TSOA> MakeParticleIterator(const TArray<TSOAView<TSOA>>& SOAs)
{
	return TParticleIterator<TSOA>(SOAs);
}


template <typename TSOA>
class TConstParticleView
{
public:
	using THandle = typename TSOA::THandleType;
	TConstParticleView()
		: Size(0)
	{
	}

	TConstParticleView(TArray<TSOAView<TSOA>>&& InSOAViews)
		: SOAViews(MoveTemp(InSOAViews))
		, Size(0)
	{
		for (const auto& SOAView : SOAViews)
		{
			Size += SOAView.Size();
		}
	}

	int32 Num() const
	{
		return Size;
	}

	TConstParticleIterator<TSOA> Begin() const { return MakeConstParticleIterator(SOAViews); }
	TConstParticleIterator<TSOA> begin() const { return Begin(); }

	TConstParticleIterator<TSOA> End() const { return TConstParticleIterator<TSOA>(); }
	TConstParticleIterator<TSOA> end() const { return End(); }

	template <typename Lambda>
	void ParallelFor(const Lambda& Func, bool bForceSingleThreaded = false) const
	{
		ParticlesParallelFor(*this, Func, bForceSingleThreaded);
	}

protected:

	TArray<TSOAView<TSOA>> SOAViews;
	int32 Size;
};

template <typename TSOA>
class TParticleView : public TConstParticleView<TSOA>
{
public:
	using Base = TConstParticleView<TSOA>;
	using Base::SOAViews;
	using Base::Num;

	TParticleView()
		: Base()
	{
	}

	TParticleView(TArray<TSOAView<TSOA>>&& InSOAViews)
		: Base(MoveTemp(InSOAViews))
	{
	}

	TParticleIterator<TSOA> Begin() const { return MakeParticleIterator(SOAViews); }
	TParticleIterator<TSOA> begin() const { return Begin(); }

	TParticleIterator<TSOA> End() const { return TParticleIterator<TSOA>(); }
	TParticleIterator<TSOA> end() const { return End(); }

	template <typename Lambda>
	void ParallelFor(const Lambda& Func, bool bForceSingleThreaded = false) const
	{
		ParticlesParallelFor(*this, Func, bForceSingleThreaded);
	}
};

template <typename TSOA>
TConstParticleView<TSOA> MakeConstParticleView(TArray<TSOAView<TSOA>>&& SOAViews)
{
	return TConstParticleView<TSOA>(MoveTemp(SOAViews));
}

template <typename TSOA>
TParticleView<TSOA> MakeParticleView(TArray<TSOAView<TSOA>>&& SOAViews)
{
	return TParticleView<TSOA>(MoveTemp(SOAViews));
}

template <typename TSOA>
TConstParticleView<TSOA> MakeConstParticleView(TSOA* SOA)
{
	TArray<TSOAView<TSOA>> SOAs;
	SOAs.Add({ SOA });
	return TConstParticleView<TSOA>(MoveTemp(SOAs));
}

template <typename TSOA>
TParticleView<TSOA> MakeParticleView(TSOA* SOA)
{
	TArray<TSOAView<TSOA>> SOAs;
	SOAs.Add({ SOA });
	return TParticleView<TSOA>(MoveTemp(SOAs));
}

}

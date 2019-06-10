// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollection.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Vector.h"
#include "ChaosArchive.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDRigidsEvolution;

	template<class T, int d>
	class TParticles : public TArrayCollection
	{
	public:
		TParticles()
		    : MXView(MX.GetData(), MX.Num())
		{
			AddArray(&MX);
			MXView = TArrayCollectionArrayView<TVector<T, d>>(MX.GetData(), MX.Num());
		}
		TParticles(const TParticles<T, d>& Other) = delete;
		TParticles(TParticles<T, d>&& Other)
		    : TArrayCollection(), MX(MoveTemp(Other.MX)), MXView(MoveTemp(Other.MXView))
		{
			AddParticles(Other.Size());
			AddArray(&MX);
			Other.MSize = 0;
		}

		/**
		 * Constructor that replaces the positions array with a view of @param Points.
		 */
		TParticles(TArray<TVector<T, d>>& Points)
		    : TArrayCollection()
		    , MXView(Points.GetData(), Points.Num())
		{
			AddElementsHelper(MXView.Num());
			AddArray(&MXView);
		}
		TParticles(TVector<T, d>* Data, const int32 Num)
		    : TArrayCollection()
		    , MXView(Data, Num)
		{
			AddElementsHelper(MXView.Num());
			AddArray(&MXView);
		}

		~TParticles()
		{}

		/**
		 * Add (or remove) particles.  If this class is currently using a non-local view for 
		 * positions, they are copied into a local positions array and resized.
		 */
		void AddElements(const int32 Num)
		{
			if (Num == 0) // Can be negative
			{
				return;
			}
			const bool UsingLocalXArray = IsUsingLocalXArray();
			if (!UsingLocalXArray)
			{
				// Don't add MX yet, so we avoid an extra memory allocation
				RemoveArray(&MXView);
			}
			// Add elements, which may allocate new memory
			AddElementsHelper(Num);
			if (!UsingLocalXArray)
			{
				AddArray(&MX); // Reuses MXView's position in the TArrayCollection
				for (int32 It = 0, EndIt = FMath::Min(MX.Num(), MXView.Num()); It < EndIt; ++It)
				{
					MX[It] = MXView[It];
				}
			}
			// MX's base pointer may have changed
			MXView = TArrayCollectionArrayView<TVector<T, d>>(MX.GetData(), MX.Num());
		}
		void AddParticles(const int32 Num)
		{
			AddElements(Num);
		}

		void Resize(const int32 Num)
		{
			AddElements(Num - Size());
		}

		void RemoveAt(const int32 Index, const int32 Count)
		{
			if (Count <= 0 || Index < 0 || Index >= static_cast<int32>(Size()))
			{
				return;
			}
			const bool UsingLocalXArray = IsUsingLocalXArray();
			if (!UsingLocalXArray)
			{
				RemoveArray(&MXView);
			}
			RemoveAtHelper(Index, Count);
			if (!UsingLocalXArray)
			{
				AddArray(&MX);
				int32 It = 0;
				for (int32 EndIt = FMath::Min(Index, MXView.Num()); It < EndIt; ++It)
				{
					MX[It] = MXView[It];
				}
				for (int32 EndIt=MXView.Num(); It < EndIt; ++It)
				{
					MX[It] = MXView[It + Count];
				}
			}
			MXView = TArrayCollectionArrayView<TVector<T, d>>(MX.GetData(), MX.Num());
		}

		TParticles& operator=(TParticles<T, d>&& Other)
		{
			MX = MoveTemp(Other.MX);
			MXView = MoveTemp(Other.MXView);
			AddParticles(Other.Size());
			Other.MSize = 0;
			return *this;
		}

		const TArrayCollectionArrayView<TVector<T, d>>& X() const
		{
			return MXView;
		}
		
		void Serialize(FChaosArchive& Ar)
		{
			bool bSerialize = IsUsingLocalXArray();
			Ar << bSerialize;
			if (ensureMsgf(bSerialize, TEXT("Cannot serialize shared views. Refactor needed to reduce memory")))	//todo(ocohen): give better way to re-use simplicial
			{
				Ar << MX;
				ResizeHelper(MX.Num());
				MXView = TArrayCollectionArrayView<TVector<T, d>>(MX.GetData(), MX.Num());
			}
		}
		const TArrayCollectionArray<TVector<T, d>>& XArray() const
		{
			return MX;
		}
		const TArrayCollectionArrayView<TVector<T, d>>& XView() const
		{
			return MXView;
		}
		const bool IsUsingLocalXArray() const
		{
			return MXView.GetData() == MX.GetData();
		}

		const TVector<T, d>& X(const int32 Index) const
		{
			check(IsUsingLocalXArray() && MXView.Num() == MX.Num() || !IsUsingLocalXArray());
			return MXView[Index];
		}

		TVector<T, d>& X(const int32 Index)
		{
			check(IsUsingLocalXArray() && MXView.Num() == MX.Num() || !IsUsingLocalXArray());
			return MXView[Index];
		}

		FString ToString(int32 index) const
		{
			return FString::Printf(TEXT("MX:%s"), *X(index).ToString());
		}

	private:
		TArrayCollectionArray<TVector<T, d>> MX;
		TArrayCollectionArrayView<TVector<T, d>> MXView;
	};
} // namespace Chaos

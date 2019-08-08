// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Chaos
{
	// There is a dirty flag for every user-settable particle property.
	// Dirty property values will get copied from game to physics thread buffers,
	// but clean property values will get overridden with physics thread results.
	enum EParticleFlags : int32
	{
		X =				1 << 0,
		R =				1 << 1,
		V =				1 << 2,
		W =				1 << 3,
		CollisionGroup =1 << 4,
		Disabled =		1 << 5,
		PreV =			1 << 6,
		PreW =			1 << 7,
		P =				1 << 8,
		Q =				1 << 9,
		F =				1 << 10,
		Torque =		1 << 11,
		I =				1 << 12,
		InvI =			1 << 13,
		M =				1 << 14,
		InvM =			1 << 15,
		ObjectState =	1 << 16,
		Geometry =		1 << 17
	};

	class FParticleDirtyFlags
	{
	public:
		FParticleDirtyFlags() : Bits(0) { }
		~FParticleDirtyFlags() { }

		bool IsDirty() const
		{
			return Bits != 0;
		}

		bool IsDirty(const EParticleFlags CheckBits) const
		{
			return (Bits & (int32)CheckBits) != 0;
		}

		void MarkDirty(const EParticleFlags DirtyBits)
		{
			Bits |= (int32)DirtyBits;
		}

		void MarkClean(const EParticleFlags CleanBits)
		{
			Bits |= ~(int32)CleanBits;
		}

		void Clear()
		{
			Bits = 0;
		}

	private:
		int32 Bits;
	};
}

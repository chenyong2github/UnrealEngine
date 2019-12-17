// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Chaos
{
	// There is a dirty flag for every user-settable particle property.
	// Dirty property values will get copied from game to physics thread buffers,
	// but clean property values will get overridden with physics thread results.
	enum EParticleFlags : int32
	{
		X =				 1 << 0,
		R =				 1 << 1,
		V =				 1 << 2,
		W =				 1 << 3,
		CenterOfMass =   1 << 4,
		CollisionGroup = 1 << 5,
		Disabled =		 1 << 6,
		PreV =			 1 << 7,
		PreW =			 1 << 8,
		P =				 1 << 9,
		Q =				 1 << 10,
		F =				 1 << 11,
		Torque =		 1 << 12,
		I =				 1 << 13,
		InvI =			 1 << 14,
		M =				 1 << 15,
		InvM =			 1 << 16,
		ObjectState =	 1 << 17,
		Geometry =		 1 << 18,
		ExternalForce =  1 << 19,
		ExternalTorque = 1 << 20,
		GravityEnabled = 1 << 21,
		SpatialIdx     = 1 << 22,
		HashResult     = 1 << 23
#if CHAOS_CHECKED
		, DebugName    = 1 << 24
#endif
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

		bool IsDirty(const int32 CheckBits) const
		{
			return (Bits & CheckBits) != 0;
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

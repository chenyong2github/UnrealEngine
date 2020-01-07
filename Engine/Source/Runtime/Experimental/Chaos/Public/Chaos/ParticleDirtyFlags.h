// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Chaos
{
	// There is a dirty flag for every user-settable particle property.
	// Dirty property values will get copied from game to physics thread buffers,
	// but clean property values will get overridden with physics thread results.
	enum class EParticleFlags : int32
	{
		X						= 1 << 0,
		R						= 1 << 1,
		V						= 1 << 2,
		W						= 1 << 3,
		CenterOfMass			= 1 << 4,
		RotationOfMass			= 1 << 5,
		CollisionGroup			= 1 << 6,
		Disabled				= 1 << 7,
		PreV					= 1 << 8,
		PreW					= 1 << 9,
		P						= 1 << 10,
		Q						= 1 << 11,
		F						= 1 << 12,
		Torque					= 1 << 13,
		I						= 1 << 14,
		InvI					= 1 << 15,
		M						= 1 << 16,
		InvM					= 1 << 17,
		LinearEtherDrag			= 1 << 18,
		AngularEtherDrag			= 1 << 19,
		ObjectState				= 1 << 20,
		Geometry				= 1 << 21,
		LinearImpulse			= 1 << 22,
		AngularImpulse			= 1 << 23,
		GravityEnabled			= 1 << 24,
		SpatialIdx				= 1 << 25,
		HashResult				= 1 << 26,
		ShapeDisableCollision	= 1 << 27
#if CHAOS_CHECKED
		, DebugName				= 1 << 28
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

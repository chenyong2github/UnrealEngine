// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FValidPFMPoint
{
	int X;
	int Y;

	FValidPFMPoint(const FVector4* InData, int InW, int InH)
		: Data(InData)
		, W(InW)
		, H(InH)
	{ }

	inline int GetSavedPointIndex()
	{
		return GetPointIndex(X, Y);
	}

	inline int GetPointIndex(int InX, int InY) const
	{
		return InX + InY * W;
	}

	inline const FVector4& GetPoint(int InX, int InY) const
	{
		return Data[GetPointIndex(InX, InY)];
	}

	inline const FVector4& GetPoint(int PointIndex) const
	{
		return Data[PointIndex];
	}

	inline bool IsValidPoint(int InX, int InY) const
	{
		return GetPoint(InX, InY).W > 0;
	}

	inline bool FindValidPoint(int InX, int InY)
	{
		X0 = InX;
		Y0 = InY;

		for (int Range = 1; Range < W; Range++)
		{
			if (FindValidPointInRange(Range))
			{
				return true;
			}
		}

		return false;
	}

	FVector GetSurfaceViewNormal() const
	{
		int Ncount = 0;
		double Nxyz[3] = { 0,0,0 };

		for (int ItY = 0; ItY < (H - 2); ++ItY)
		{
			for (int ItX = 0; ItX < (W - 2); ++ItX)
			{
				const FVector4& Pts0 = GetPoint(ItX, ItY);
				const FVector4& Pts1 = GetPoint(ItX + 1, ItY);
				const FVector4& Pts2 = GetPoint(ItX, ItY + 1);

				if (Pts0.W > 0 && Pts1.W > 0 && Pts2.W > 0)
				{
					const FVector N1 = Pts1 - Pts0;
					const FVector N2 = Pts2 - Pts0;
					const FVector N = FVector::CrossProduct(N2, N1).GetSafeNormal();

					for (int i = 0; i < 3; i++)
					{
						Nxyz[i] += N[i];
					}

					Ncount++;
				}
			}
		}

		double Scale = double(1) / Ncount;
		for (int i = 0; i < 3; i++)
		{
			Nxyz[i] *= Scale;
		}

		return FVector(Nxyz[0], Nxyz[1], Nxyz[2]).GetSafeNormal();
	}

	FVector GetSurfaceViewPlane()
	{
		const FVector4& Pts0 = GetValidPoint(0, 0);
		const FVector4& Pts1 = GetValidPoint(W - 1, 0);
		const FVector4& Pts2 = GetValidPoint(0, H - 1);

		const FVector N1 = Pts1 - Pts0;
		const FVector N2 = Pts2 - Pts0;
		return FVector::CrossProduct(N2, N1).GetSafeNormal();
	}

private:
	const FVector4* Data;
	int X0;
	int Y0;
	int W;
	int H;

private:
	inline const FVector4& GetValidPoint(int InX, int InY)
	{
		if (!IsValidPoint(InX, InY))
		{
			if (FindValidPoint(InX, InY))
			{
				return GetPoint(GetSavedPointIndex());
			}
		}

		return GetPoint(InX, InY);
	}

	inline bool FindValidPointInRange(int Range)
	{
		for (int i = -Range; i <= Range; i++)
		{
			// Top or bottom rows
			if (IsValid(X0 + i, Y0 - Range) || IsValid(X0 + i, Y0 + Range))
			{
				return true;
			}

			// Left or Right columns
			if (IsValid(X0 - Range, Y0 + i) || IsValid(X0 + Range, Y0 + i))
			{
				return true;
			}
		}

		return false;
	}

	inline bool IsValid(int newX, int newY)
	{
		if (newX < 0 || newY < 0 || newX >= W || newY >= H)
		{
			// Out of texture
			return false;
		}

		if (Data[GetPointIndex(newX, newY)].W > 0)
		{
			// Store valid result
			X = newX;
			Y = newY;

			return true;
		}

		return false;
	}
};

inline bool CalcFrustumFromVertex(const FVector4& PFMVertice, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right)
{
	bool bResult = true;

	if (PFMVertice.W > 0)
	{
		FVector4 PrjectedVertice = World2Local.TransformFVector4(PFMVertice);

		float Scale = 1.0f / PrjectedVertice.X;
		if (Scale <= 0)
		{
			// This point out of view plane
			bResult = false;
			return bResult;
		}

		// Use only points over view plane, ignore backside pts
		PrjectedVertice.Y *= Scale;
		PrjectedVertice.Z *= Scale;

		if (PrjectedVertice.Z > Top)
		{
			Top = PrjectedVertice.Z;
		}

		if (PrjectedVertice.Z < Bottom)
		{
			Bottom = PrjectedVertice.Z;
		}

		if (PrjectedVertice.Y > Right)
		{
			Right = PrjectedVertice.Y;
		}

		if (PrjectedVertice.Y < Left)
		{
			Left = PrjectedVertice.Y;
		}
	}

	return bResult;
}

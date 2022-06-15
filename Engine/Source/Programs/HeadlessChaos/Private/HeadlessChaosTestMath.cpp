// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/Math/Krylov.h"
#include "Chaos/Matrix.h"
#include "Chaos/Utilities.h"
#include "Chaos/Vector.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace ChaosTest
{
	using namespace Chaos;

	TEST(MathTests, TestMatrixInverse)
	{
		FMath::RandInit(10695676);
		const FReal Tolerance = (FReal)0.001;

		for (int RandIndex = 0; RandIndex < 20; ++RandIndex)
		{
			FMatrix33 M = RandomMatrix(-10, 10);
			FMatrix33 MI = M.Inverse();

			FMatrix33 R = Utilities::Multiply(MI, M);

			EXPECT_TRUE(R.Equals(FMatrix33::Identity, Tolerance));
		}
	}

	Chaos::TVector<double, 4> 
	ToVec4(const TArray<double>& x)
	{
		return Chaos::TVector<double, 4>(x[0], x[1], x[2], x[3]);
	}

	TArray<double>
	ToArray4(const TVector<double, 4>& x)
	{
		return TArray<double>({x[0], x[1], x[2], x[3]});
	}

	TEST(MathTests, TestLanczosCGSolver) 
	{
		Chaos::PMatrix<double, 4, 4> A(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		A.M[0][0] = double(2);
		A.M[0][1] = -double(1);
		A.M[1][0] = -double(1);
		A.M[1][1] = double(2);
		A.M[1][2] = -double(1);
		A.M[2][1] = -double(1);
		A.M[2][2] = double(2);
		A.M[2][3] = -double(1);
		A.M[3][2] = -double(1);
		A.M[3][3] = double(2);
		A = A.GetTransposed(); // UE is col maj, not row

		TArray<double> x({ 1, 1, 1, 1 });
		TArray<double> x_cg;
		TArray<double> b = ToArray4(A.TransformFVector4(ToVec4(x)));

		Chaos::LanczosCG<double>(
			[&A](TArray<double>& y, const TArray<double>& x) { y = ToArray4(A.TransformFVector4(ToVec4(x))); },
			[](const TArray<double>& x, const TArray<double>& y) { return Dot4(ToVec4(x), ToVec4(y)); },
			[](TArray<double>& y, double a, const TArray<double>& x) { y = ToArray4(ToVec4(y) + a * ToVec4(x)); },
			[](TArray<double>& y, double a) { y = ToArray4(a * ToVec4(y)); },
			[](TArray<double>& y, const TArray<double>& x) { y = x; },
			x_cg,
			b,
			5);

		double error = 0.;
		for (int i = 0; i < 4; i++)
			error += (x[i] - x_cg[i]) * (x[i] - x_cg[i]);
		error = FGenericPlatformMath::Sqrt(error);
		double tol = 1.e-10;
		EXPECT_NEAR(error, double(0), tol);
	}

#if 0
	void ICRes(const Chaos::PMatrix<double, 4, 4>& L, const Chaos::TVector<double, 4>& d, Chaos::TVector<double, 4>& y, const Chaos::TVector<double, 4>& x)
	{
		Chaos::TVector<double, 4> u;
		for (int k = 0; k < 4; k++)
		{
			if (L.M[k][k])
			{
				double rly = L.M[k][k];
				for (int j = 0; j < k; j++)
				{
					rly -= L.M[k][j] * u[j];
				}
				u[k] = rly / L.M[k][k];
			}
		}
		for (int k = 4 - 1; k >= 0; k--)
		{
			double lu = 0.;
			for (int i = k + 1; i < 4; i++)
			{
				lu += L.M[i][k] * y[i];
			}
			y[k] = u[k] - d[k] * lu;
		}
	}

	TEST(MathTests, LanczosPCGSolver)
	{
		Chaos::PMatrix<double, 4, 4> A(2, 0, 0, 0, 0, 0, 0, -2.6, 0, 0, 4, 3.6, 0, -2.6, 3.6, 5);
		Chaos::PMatrix<double, 4, 4> M;
		Chaos::PMatrix<double, 3, 3> LMat;
		Chaos::TVector<double, 4> DVec;
		TVector<double, 4> x(1, 1, 1, 1);
		TVector<double, 4> x_pcg;
		TVector<double, 4> b;
		//b = A * x;
		b = A.TransformFVector4(x);

		// Calculate M
		auto ICU = [](Chaos::PMatrix<double, 4, 4>& AMat, Chaos::PMatrix<double, 4, 4>& L, Chaos::TVector<double, 4>& d)
		{
			double epsilon = .1;
			L.M[0][0] = AMat.M[0][0];
			
			d[0] = 1. / L.M[0][0];
			for (int k = 0; k < 4; k++) 
			{
				double value = 0.;
				for (int j =0; j < 4; j++) 
				{
					double aij = AMat.M[k][j];
					double ldl = 0.;
					for(int i=0; i < 4; i++)
					{
						ldl -= L.M[k][i] * L.M[j][i] * d[i];
					}
					value = aij + ldl;
					L.M[k][j] = value;
				}
				if (abs(L.M[k][k]) < epsilon)
					L.M[k][k] = epsilon;
				d[k] = 1. / L.M[k][k];
			}
		};

		auto multiplyA = [&A](Chaos::TVector<double, 4>& y, const Chaos::TVector<double, 4>& x) { y = A.TransformFVector4(x); };
		auto multiplyPrecond = [&LMat, &DVec, this](Chaos::TVector<double, 4>& y, const Chaos::TVector<double, 4>& x) { ICRes(LMat, DVec, y, x); };
		auto dotProduct = [](const Chaos::TVector<double, 4>& x, const Chaos::TVector<double, 4>& y) { return Dot4(x, y); };
		auto set = [](Chaos::TVector<double, 4>& y, const Chaos::TVector<double, 4>& x) { y = x; };
		auto setScaled = [](Chaos::TVector<double, 4>& a, const Chaos::TVector<double, 4>& b, const double s) { a = s * b; };        // setScaled(a,b,s): a <- s * b
		auto scaleAndAdd = [](Chaos::TVector<double, 4>& a, const double s, const Chaos::TVector<double, 4>& b) { a = s * a + b; };  // scaleAndAdd(a,s,b): a <- s * a + b
		auto addScaled = [](Chaos::TVector<double, 4>& a, const Chaos::TVector<double, 4>& b, const double s) { a += s * b; };       // addScaled(a,b,s): a <- a + s * b
		auto addScaled2 = [](Chaos::TVector<double, 4>& a, const Chaos::TVector<double, 4>& b1, const double s1, const Chaos::TVector<double, 4>& b2, const double s2) {
			a += s1 * b1 + s2 * b2;
		};  // addScaled2(a,b1,s1,b2,s2); a <- s1 * b1 + s2 * b2
		auto residual = [&](double& r, const Chaos::TVector<double, 4>& x, const Chaos::TVector<double, 4>& b) {
			Chaos::TVector<double, 4> res, Mres;
			multiplyA(res, x);
			addScaled(res, b, double(-1));
			multiplyPrecond(Mres, res);
			r = FGenericPlatformMath::Sqrt(dotProduct(Mres, res));
		};

		Chaos::LanczosPCG<T>(multiplyA, multiplyPrecond, dotProduct, set, setScaled, scaleAndAdd, addScaled, addScaled2, residual, x_pcg, b, 4);
		double r;
		residual(r, x_pcg, b);
		EXPECT_NEAR(r, 0., 1.e-10);
	}
#endif // 0
}

// Copyright Epic Games, Inc. All Rights Reserved.
#include "HeadlessChaosTestConstraints.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "Chaos/HeightField.h"

namespace ChaosTest {

	using namespace Chaos;

	template<typename T>
	void Raycast()
	{
		const int32 Columns = 10;
		const int32 Rows = 10;
		const T CountToWorldScale = 1;

		{

			TArray<T> Heights;
			Heights.AddZeroed(Rows * Columns);

			T Count = 0;
			for(int32 Row = 0; Row < Rows; ++Row)
			{
				for(int32 Col = 0; Col < Columns; ++Col)
				{
					Heights[Row * Columns + Col] = CountToWorldScale * Count++;
				}
			}

			auto ComputeExpectedNormal = [&](const TVec3<T>& Scale)
			{
				//Compute expected normal
				const TVec3<T> A(0,0,0);
				const TVec3<T> B(Scale[0],0,CountToWorldScale * Scale[2]);
				const TVec3<T> C(0,Scale[1],Columns*CountToWorldScale * Scale[2]);
				const TVec3<T> ExpectedNormal = TVec3<T>::CrossProduct((B-A),(C-A)).GetUnsafeNormal();
				return ExpectedNormal;
			};

			auto AlongZTest = [&](const TVec3<T>& Scale)
			{
				TArray<T> HeightsCopy = Heights;
				FHeightField Heightfield(MoveTemp(HeightsCopy),TArray<uint8>(),Rows,Columns,Scale);
				const auto& Bounds = Heightfield.BoundingBox();	//Current API forces us to do this to cache the bounds

				//test straight down raycast
				Count = 0;
				T TOI;
				TVec3<T> Position,Normal;
				int32 FaceIdx;

				const TVec3<T> ExpectedNormal = ComputeExpectedNormal(Scale);

				int32 ExpectedFaceIdx = 0;
				for(int32 Row = 0; Row < Rows; ++Row)
				{
					for(int32 Col = 0; Col < Columns; ++Col)
					{
						const TVec3<T> Start(Col*Scale[0],Row * Scale[1],1000*Scale[2]);
						EXPECT_TRUE(Heightfield.Raycast(Start,TVec3<T>(0,0,-1),2000*Scale[2],0,TOI,Position,Normal,FaceIdx));
						EXPECT_NEAR(TOI,(1000 - Heights[Row*Columns+Col])*Scale[2],1e-2);
						EXPECT_VECTOR_NEAR(Position,TVec3<T>(Col*Scale[0],Row * Scale[1],Heights[Row*Columns+Col] * Scale[2]),1e-2);
						EXPECT_VECTOR_NEAR(Normal,ExpectedNormal,1e-2);

						//offset in from border ever so slightly to get a clear face
						const bool bResult = Heightfield.Raycast(Start + TVec3<T>(0.2 * Scale[0],0.1 * Scale[1],0),TVec3<T>(0,0,-1),2000*Scale[2],0,TOI,Position,Normal,FaceIdx);
						if(Col + 1 == Columns || Row + 1 == Rows)
						{
							EXPECT_FALSE(bResult);	//went past edge so no hit
							//Went past column so do not increment expected face idx
						} else
						{
							EXPECT_TRUE(bResult);
							EXPECT_EQ(FaceIdx,ExpectedFaceIdx);	//each quad has two triangles, so for each column we pass two faces

							ExpectedFaceIdx += 2;	//We hit the first triangle in the quad. Since we are going 1 quad at a time we skip 2
						}
					}
				}
			};

			AlongZTest(TVec3<T>(1));
			AlongZTest(TVec3<T>(1,1,3));
			AlongZTest(TVec3<T>(1,1,.3));
			AlongZTest(TVec3<T>(3,1,.3));
			AlongZTest(TVec3<T>(2,.1,.3));

			auto AlongXTest = [&](const TVec3<T>& Scale)
			{
				TArray<T> HeightsCopy = Heights;
				FHeightField Heightfield(MoveTemp(HeightsCopy),TArray<uint8>(),Rows,Columns,Scale);
				const auto& Bounds = Heightfield.BoundingBox();	//Current API forces us to do this to cache the bounds

				//test along x axis
				Count = 0;
				T TOI;
				TVec3<T> Position,Normal;
				int32 FaceIdx;

				const TVec3<T> ExpectedNormal = ComputeExpectedNormal(Scale);

				//move from left to right and raycast down the x-axis. The Row idx indicates which cell we expect to hit
				for(int32 Row = 0; Row < Rows; ++Row)
				{
					for(int32 Col = 0; Col < Columns; ++Col)
					{
						const TVec3<T> Start(-Scale[0],Row * Scale[1],Heights[Row*Columns + Col] * Scale[2] + 0.01 * Scale[2]);
						const bool bResult = Heightfield.Raycast(Start,TVec3<T>(1,0,0),2000*Scale[0],0,TOI,Position,Normal,FaceIdx);
						if(Col + 1 == Columns)
						{
							EXPECT_FALSE(bResult);
							//No more columns so we shot over the final edge
						} else
						{
							EXPECT_TRUE(bResult);
							EXPECT_NEAR(TOI,(Scale[0] * (1 + Col)),1e-1);
							EXPECT_VECTOR_NEAR(Position,(Start + TVec3<T>{TOI,0,0}),1e-2);
							EXPECT_VECTOR_NEAR(Normal,ExpectedNormal,1e-1);
						}
					}
				}
			};

			AlongXTest(TVec3<T>(1));
			AlongXTest(TVec3<T>(1,1,3));
			AlongXTest(TVec3<T>(1,1,.3));
			AlongXTest(TVec3<T>(3,1,.3));
			AlongXTest(TVec3<T>(2,.1,.3));

			auto AlongYTest = [&](const TVec3<T>& Scale)
			{
				TArray<T> HeightsCopy = Heights;
				FHeightField Heightfield(MoveTemp(HeightsCopy),TArray<uint8>(),Rows,Columns,Scale);
				const auto& Bounds = Heightfield.BoundingBox();	//Current API forces us to do this to cache the bounds

				//test along y axis
				Count = 0;
				T TOI;
				TVec3<T> Position,Normal;
				int32 FaceIdx;

				const TVec3<T> ExpectedNormal = ComputeExpectedNormal(Scale);

				for(int32 Row = 0; Row < Rows; ++Row)
				{
					for(int32 Col = 0; Col < Columns; ++Col)
					{
						const TVec3<T> Start(Col * Scale[0],-Scale[1],Heights[Row*Columns + Col] * Scale[2] + 0.01 * Scale[2]);
						const bool bResult = Heightfield.Raycast(Start,TVec3<T>(0,1,0),2000*Scale[0],0,TOI,Position,Normal,FaceIdx);
						if(Row + 1 == Rows)
						{
							EXPECT_FALSE(bResult);
							//No more columns so we shot over the final edge
						} else
						{
							EXPECT_TRUE(bResult);
							EXPECT_NEAR(TOI,(Scale[1] * (1 + Row)),1e-1);
							EXPECT_VECTOR_NEAR(Position,(Start + TVec3<T>{0,TOI,0}),1e-2);
							EXPECT_VECTOR_NEAR(Normal,ExpectedNormal,1e-1);
						}
					}
				}
			};

			AlongYTest(TVec3<T>(1));
			AlongYTest(TVec3<T>(1,1,3));
			AlongYTest(TVec3<T>(1,1,.3));
			AlongYTest(TVec3<T>(3,1,.3));
			AlongYTest(TVec3<T>(2,.1,.3));
		}

		{

			//For diagonal test simply increase height on the y axis
			TArray<T> Heights2;
			Heights2.AddZeroed(Rows*Columns);
			for(int32 Row = 0; Row < Rows; ++Row)
			{
				for(int32 Col = 0; Col < Columns; ++Col)
				{
					Heights2[Row * Columns + Col] = CountToWorldScale * Row;
				}
			}

			auto ComputeExpectedNormal2 = [&](const TVec3<T>& Scale)
			{
				//Compute expected normal
				const TVec3<T> A(0,0,0);
				const TVec3<T> B(Scale[0],0,0);
				const TVec3<T> C(0,Scale[1],CountToWorldScale * Scale[2]);
				const TVec3<T> ExpectedNormal = TVec3<T>::CrossProduct((B-A),(C-A)).GetUnsafeNormal();
				return ExpectedNormal;
			};

			auto AlongXYTest = [&](const TVec3<T>& Scale)
			{
				TArray<T> HeightsCopy = Heights2;
				FHeightField Heightfield(MoveTemp(HeightsCopy),TArray<uint8>(),Rows,Columns,Scale);
				const auto& Bounds = Heightfield.BoundingBox();	//Current API forces us to do this to cache the bounds

				//test along x-y axis
				T TOI;
				TVec3<T> Position,Normal;
				int32 FaceIdx;

				const TVec3<T> ExpectedNormal = ComputeExpectedNormal2(Scale);

				for(int32 Row = 0; Row < Rows; ++Row)
				{
					for(int32 Col = 0; Col < Columns; ++Col)
					{
						const TVec3<T> Start(Col * Scale[0],0,Heights2[Row*Columns + Col] * Scale[2] + 0.01 * Scale[2]);
						const TVec3<T> Dir = TVec3<T>(1,1,0).GetUnsafeNormal();
						const bool bResult = Heightfield.Raycast(Start,Dir,2000*Scale[0],0,TOI,Position,Normal,FaceIdx);

						//As we increase the row, fewer columns will hit because the ray will exit the heightfield
						const bool bShouldHit = Col + Row + 1 < Columns;
						EXPECT_EQ(bShouldHit,bResult);
						if(bResult)
						{
							EXPECT_VECTOR_NEAR(Normal,ExpectedNormal,1e-1);
						}
					}
				}
			};

			AlongXYTest(TVec3<T>(1));


			auto ToCellsTest = [&](const TVec3<T>& Scale)
			{
				//Pick cells and shoot ray at them
				//This should always succeed because 0,0 is the lowest and n,n is the heighest
				TArray<T> HeightsCopy = Heights2;
				FHeightField Heightfield(MoveTemp(HeightsCopy),TArray<uint8>(),Rows,Columns,Scale);
				const auto& Bounds = Heightfield.BoundingBox();	//Current API forces us to do this to cache the bounds

				T TOI;
				TVec3<T> Position,Normal;
				int32 FaceIdx;

				const TVec3<T> ExpectedNormal = ComputeExpectedNormal2(Scale);

				const TVec3<T> Start(0,0,Heights2.Last() * Scale[2]);
				for(int32 TargetIdx = 0; TargetIdx < Heights2.Num(); ++TargetIdx)
				{
					const int32 Col = TargetIdx % Columns;
					const int32 Row = TargetIdx / Columns;
					const TVec3<T> EndUnscaled(Col,Row,Heights2[TargetIdx]);
					TVec3<T> End = EndUnscaled * Scale;
					if(Col + 1 == Columns)
					{
						//pull back slightly to avoid precision issues at edge
						End[0] -= 0.1 * Scale[0];
					}

					if(Row + 1 == Rows)
					{
						//pulling back row affects Z so just skip
						continue;
					}

					const TVec3<T> Dir = (End - Start).GetUnsafeNormal();

					const bool bResult = Heightfield.Raycast(Start,Dir,2000,0,TOI,Position,Normal,FaceIdx);
					EXPECT_TRUE(bResult);
					EXPECT_VECTOR_NEAR(Normal,ExpectedNormal,1e-1);
					EXPECT_VECTOR_NEAR(Position,End,1e-1);
				}
			};

			ToCellsTest(TVec3<T>(1));
			ToCellsTest(TVec3<T>(1,1,10));
			ToCellsTest(TVec3<T>(1,1,.1));
			ToCellsTest(TVec3<T>(3,1,.1));
			ToCellsTest(TVec3<T>(.3,1,.1));


		}
		
	}

	void EditHeights()
	{
		const int32 Columns = 10;
		const int32 Rows = 10;
		const uint16 InitialHeight = 32768; // Real height = 0, (half of uint16 max of 65535)

		TArray<uint16> Heights;
		Heights.AddZeroed(Rows * Columns);

		// Stolen from Heightfield.cpp
		TUniqueFunction<FReal(const uint16)> ConversionFunc = [](const FReal InVal) -> float
		{
			return (float)((int32)InVal - 32768);
		};



		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Columns; ++Col)
			{
				Heights[Row * Columns + Col] = InitialHeight;
			}
		}


		TVector<FReal, 3> Scale(1, 1, 1);
		FHeightField Heightfield(MoveTemp(Heights),TArray<uint8>(),Rows,Columns,Scale);

		// Test is intended to catch trivial regressions in EditGeomData,
		// specifically handling Landscape module providing buffer with column index inverted from heightfield whe editing.

		int32 InRows = 1;
		int32 InCols = 3;
		TArray<uint16> ModifiedHeights;
		ModifiedHeights.Init(InitialHeight, InRows * InCols);

		int32 Row = 0;
		int32 Col = 0;
		ModifiedHeights[Row * (InCols) + Col] = 35000;
		Col = 1;
		ModifiedHeights[Row * (InCols) + Col] = 40000;
		Col = 2;
		ModifiedHeights[Row * (InCols) + Col] = 45000;

		float ExpectedMaxRealHeight = ConversionFunc(45000);
		float ExpectedMinRealHeight = ConversionFunc(InitialHeight);
		float ExpectedRange = ExpectedMaxRealHeight - ExpectedMinRealHeight;

		int32 RowBegin = 3;
		int32 ColBegin = 4;

		// Expectation is that all values are at default, and ModifiedHeights is applied to heightfield
		// starting at (ColBegin, RowBegin) to (ColBegin + InCols, RowBegin + InRows).
		// Landscape module provides buffer with col idx inverted, so they are expected to be written reverse order over columns within this range.
		// The Begin indices however are not inverted. These match heightfield.
		// If this seems confusing, that's because it is. -MaxW
		Heightfield.EditHeights(ModifiedHeights, RowBegin, ColBegin, InRows, InCols);


		auto& GeomData = Heightfield.GeomData;


		// Validate heights using 2d iteration scheme
		for (int32 RowIdx = 0; RowIdx < Rows; ++RowIdx)
		{
			for (int32 ColIdx = 0; ColIdx < Columns; ++ColIdx)
			{
				if (RowIdx >= RowBegin && RowIdx < RowBegin + InRows && ColIdx >= ColBegin && ColIdx < ColBegin + InCols)
				{
					// This is in modified range
					int32 ModifiedRowIdx = RowIdx - RowBegin;
					int32 ModifiedColIdx = ColIdx - ColBegin;
					int32 ModifiedIdx = ModifiedRowIdx * InCols + ModifiedColIdx;

					int32 HeightIdx = RowIdx * Columns + (Columns - 1 - ColIdx); // Remember that modified heights buffer uses inverted col index
					float HeightReal = GeomData.MinValue + GeomData.Heights[HeightIdx] * GeomData.HeightPerUnit;

					uint16 ModifiedHeight = ModifiedHeights[ModifiedIdx];
					float ModifiedHeightReal = ConversionFunc(ModifiedHeight);
					EXPECT_NEAR(ModifiedHeightReal, HeightReal, 1);
				}
				else
				{
					int32 HeightIdx = RowIdx * Columns + (Columns - 1 - ColIdx); // Remember that modified heights buffer uses inverted col index
					float HeightReal = GeomData.MinValue + GeomData.Heights[HeightIdx] * GeomData.HeightPerUnit;

					float InitialHeightReal = ConversionFunc(InitialHeight);
					EXPECT_NEAR(InitialHeightReal, HeightReal, 0.0001f);
				}
			}
		}

		EXPECT_EQ(GeomData.MinValue, ExpectedMinRealHeight);
		EXPECT_EQ(GeomData.MaxValue, ExpectedMaxRealHeight);
		EXPECT_EQ(GeomData.Range, ExpectedRange); 
		EXPECT_EQ(GeomData.HeightPerUnit, (ExpectedRange) / TNumericLimits<uint16>::Max()); // Range over uint16 max (65535)
	}


	TEST(ChaosTests, Heightfield)
	{
		ChaosTest::Raycast<float>();
		EditHeights();
		SUCCEED();
	}

}
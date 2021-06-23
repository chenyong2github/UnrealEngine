// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public static class MathUtil
    {
        private static MathUtility _util = null;

        public static MathUtility Utility
        {
            get
            {
                if (_util == null)
                {
                    _util = SwAddin.Instance.SwApp.IGetMathUtility();
                }

                return _util;
            }
        }

        public static MathVector CreateVector(double x, double y, double z)
        {
            return (MathVector)Utility.CreateVector(new[] { x, y, z }); ;
        }

        public static MathVector CreateVector(float x, float y, float z)
        {
            return (MathVector)Utility.CreateVector(new[] { x, y, z }); ;
        }

        public static MathPoint CreatePoint(double x, double y, double z)
        {
            return (MathPoint)Utility.CreatePoint(new[] { x, y, z }); ;
        }

        public static MathPoint CreatePoint(float x, float y, float z)
        {
            return (MathPoint)Utility.CreatePoint(new[] { x, y, z }); ;
        }

        // a solidworks matrix indices work as follows:
        // 0  1  2  13
        // 3  4  5  14
        // 6  7  8  15
        // 9  10 11 12
        // where 9, 10 and 11 are the indices of the translation vector
        private static readonly double[] tIdenity = new double[] {
            1.0, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0,
            0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0
        };

        // datasmith matrix:
        // 0 4 8  12 (tx)
        // 1 5 9  13 (ty)
        // 2 6 10 14 (tz)
        // 3(0.0) 7(0.0) 11(0.0) 15(1.0)
        public static float[] ConvertFromSolidworksTransform(MathTransform tm)
        {
            float[] t = new float[16];
            double[] tt = null;
            tt = (double[])tm.ArrayData;
            t[0] = (float)tt[0];
            t[1] = (float)tt[1];
            t[2] = (float)tt[2];
            t[3] = 0f;
            t[4] = (float)tt[3];
            t[5] = (float)tt[4];
            t[6] = (float)tt[5];
            t[7] = 0f;
            t[8] = (float)tt[6];
            t[9] = (float)tt[7];
            t[10] = (float)tt[8];
            t[11] = 0f;
            t[15] = 1f;
            t[12] = (float)tt[9] * SwSingleton.GeometryScale;
            t[13] = (float)tt[10] * SwSingleton.GeometryScale;
            t[14] = (float)tt[11] * SwSingleton.GeometryScale;
            return t;
        }

        public static MathTransform CreateTransform()
        {
            return (MathTransform)Utility.CreateTransform(tIdenity);
        }

        public static MathTransform CreateTransformRotateAxis(Vec3 axis, float rotation)
        {
            return (MathTransform)Utility.CreateTransformRotateAxis(Utility.CreatePoint(new double[] { 0.0, 0.0, 0.0 }), Utility.CreateVector(new double[] { (double)axis.x, (double)axis.y, (double)axis.z }), (double)rotation);
        }
    }
}

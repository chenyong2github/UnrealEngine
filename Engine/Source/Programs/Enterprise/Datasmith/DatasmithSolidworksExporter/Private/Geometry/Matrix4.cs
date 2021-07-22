// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Matrix4
    {
        // Properties
        // -----------------------------------------------------------------------------

        public Vec3 XBasis
        { get { return new Vec3(_m[0], _m[1], _m[2]); } }

        public Vec3 YBasis
        { get { return new Vec3(_m[4], _m[5], _m[6]); } }

        public Vec3 ZBasis
        { get { return new Vec3(_m[8], _m[9], _m[10]); } }


        // Fields
        // -----------------------------------------------------------------------------

        private static Func<float, float> Sin = AngleDegrees => (float)Math.Sin(AngleDegrees * Utility.Deg2Rad);
        private static Func<float, float> Cos = AngleDegrees => (float)Math.Cos(AngleDegrees * Utility.Deg2Rad);

        public const int Size4x4 = 16;
        public const int Size4x3 = 12;

        private float[] _m;

        public Matrix4()
        {
            _m = Identity;
        }

        public Matrix4(float[] m)
        {
            _m = m;
        }

        public override int GetHashCode()
        {
            int hash = 1;
            if (_m != null)
            {
                hash = (hash * 17) + _m.Length;
                foreach (float val in _m)
                {
                    hash *= 17;
                    hash = hash + val.GetHashCode();
                }
            }
            return hash;
        }

        public static implicit operator float[] (Matrix4 m)
        {
            return m._m;
        }

        public static Matrix4 Identity
        {
            get
            {
                return new Matrix4(new float[Size4x4]
                {
                    1f, 0f, 0f, 0f,
                    0f, 1f, 0f, 0f,
                    0f, 0f, 1f, 0f,
                    0f, 0f, 0f, 1f
                });
            }
        }

        public static Matrix4 FromSolidWorks(double[] m)
        {
            return new Matrix4(new float[Size4x4]
            {
                (float)m[0], (float)m[3], (float)m[6], (float)m[9] * SwSingleton.GeometryScale,
                (float)m[1], (float)m[4], (float)m[7], (float)m[10] * SwSingleton.GeometryScale,
                (float)m[2], (float)m[5], (float)m[8], (float)m[11] * SwSingleton.GeometryScale,
                0f, 0f, 0f, (float)m[12]
            });
        }

        public static Matrix4 From3x3(float[] m)
        {
            return new Matrix4(new float[Size4x4]
            {
                m[0], m[1], m[2], 0.0f,
                m[3], m[4], m[5], 0.0f,
                m[6], m[7], m[8], 0.0f,
                0f, 0f, 0f, 1.0f
            });
        }

        public static Matrix4 From4x3(float[] m)
        {
            return new Matrix4(new float[Size4x4]
            {
                m[0], m[1], m[2], m[3],
                m[4], m[5], m[6], m[7],
                m[8], m[9], m[10], m[11],
                0f, 0f, 0f, 1.0f
            });
        }

        public static Matrix4 FromRotationX(float AngleDegrees)
        {
            return new Matrix4(new float[Size4x4]
            {
                1f, 0f, 0f, 0f,
                0f, Cos(AngleDegrees), -Sin(AngleDegrees), 0f,
                0f, Sin(AngleDegrees), Cos(AngleDegrees), 0f,
                0f, 0f, 0f, 1f
            });
        }

        public static Matrix4 FromRotationY(float AngleDegrees)
        {
            return new Matrix4(new float[Size4x4]
            {
                Cos(AngleDegrees), 0f, Sin(AngleDegrees), 0f,
                0f, 1f, 0f, 0f,
                -Sin(AngleDegrees), 0f, Cos(AngleDegrees), 0f,
                0f, 0f, 0f, 1f
            });
        }

        public static Matrix4 FromRotationZ(float AngleDegrees)
        {
            return new Matrix4(new float[Size4x4]
            {
                Cos(AngleDegrees), -Sin(AngleDegrees), 0f, 0f,
                Sin(AngleDegrees), Cos(AngleDegrees), 0f, 0f,
                0f, 0f, 1f, 0f,
                0f, 0f, 0f, 1f
            });
        }

        public static Matrix4 Translation(Vec3 v)
        {
            return FromTranslation(v.x, v.y, v.z);
        }

        public static Matrix4 FromTranslation(float x, float y, float z)
        {
            Matrix4 m = Identity;
            m.SetTranslation(x, y, z);
            return m;
        }

        public static Matrix4 FromScale(float x, float y, float z)
        {
            Matrix4 m = Identity;
            m._m[0] = x;
            m._m[5] = y;
            m._m[10] = z;
            return m;
        }

        public static float[] LookAt(Vec3 directionZ, Vec3 fromPoint = null, Vec3 vecUp = null)
        {
            float[] ret = null;
            if (directionZ != null)
            {
                if (vecUp == null)
                {
                    vecUp = new Vec3(0f, 1f, 0f);
                }

                var nDirectionZ = directionZ.Normalized();
                var directionX = Vec3.Cross(vecUp, nDirectionZ);
                var directionY = Vec3.Cross(nDirectionZ, directionX);
                ret = new[] { directionX.x, directionY.x, nDirectionZ.x, 0f ,
                              directionX.y, directionY.y, nDirectionZ.y, 0f ,
                              directionX.z, directionY.z, nDirectionZ.z, 0f ,
                              0f, 0f, 0f, 1f };

                if (fromPoint != null)
                {
                    ret[3] = fromPoint.x;
                    ret[7] = fromPoint.y;
                    ret[11] = fromPoint.z;
                }
            }

            return ret;
        }

        public static float[] Matrix4x4Multiply(float[] matrixA, float[] matrixB)
        {
            float[] ret = null;
            if (matrixA.Length == Size4x4 && matrixB.Length == Size4x4)
            {
                ret = new float[Size4x4];
                for (var i = 0; i < 4; i++)
                {
                    for (var j = 0; j < 4; j++)
                    {
                        var val = 0f;
                        for (var pos = 0; pos < 4; pos++)
                        {
                            val += matrixA[i * 4 + pos] * matrixB[pos * 4 + j];
                        }

                        ret[i * 4 + j] = val;
                    }
                }
            }

            return ret;
        }


        // Methods
        // -----------------------------------------------------------------------------

        public override string ToString()
        {
            string s =
                $"\n( { _m[0] }, { _m[1] }, { _m[2] } )( { _m[3] } )" +
                $"\n( { _m[4] }, { _m[5] }, { _m[6] } )( { _m[7] } )" +
                $"\n( { _m[8] }, { _m[9] }, { _m[10] } )( {_m[11] } )";
            return s;
        }

        public float[] To4x3()
        {
            float[] m = new float[Size4x3];

            for (int i = 0; i < Size4x3; i++)
                m[i] = _m[i];

            return m;
        }

        public void SetTranslation(float x, float y, float z)
        {
            _m[3] = x;
            _m[7] = y;
            _m[11] = z;
        }

        public void SetTranslation(Vec3 vec)
        {
            _m[3] = vec.x;
            _m[7] = vec.y;
            _m[11] = vec.z;
        }

        public static Matrix4 FromRotationAxisAngle(Vec3 Axis, float AngleDegrees)
        {
            Matrix4 m = new Matrix4();
            m.SetRotationAxisAngle(Axis, AngleDegrees);
            return m;
        }

        public void SetRotationAxisAngle(Vec3 Axis, float AngleDegrees)
        {
            Vec3 v = Axis.Normalized();

            float cosA = Cos(AngleDegrees);
            float sinA = Sin(AngleDegrees);
            float oneMinusCosA = 1f - cosA;

            _m[0] = ((v.x * v.x) * oneMinusCosA) + cosA;
            _m[1] = ((v.x * v.y) * oneMinusCosA) + (v.z * sinA);
            _m[2] = ((v.x * v.z) * oneMinusCosA) - (v.y * sinA);
            _m[3] = 0f;

            _m[4] = ((v.y * v.x) * oneMinusCosA) - (v.z * sinA);
            _m[5] = ((v.y * v.y) * oneMinusCosA) + cosA;
            _m[6] = ((v.y * v.z) * oneMinusCosA) + (v.x * sinA);
            _m[7] = 0f;

            _m[8] = ((v.z * v.x) * oneMinusCosA) + (v.y * sinA);
            _m[9] = ((v.z * v.y) * oneMinusCosA) - (v.x * sinA);
            _m[10] = ((v.z * v.z) * oneMinusCosA) + cosA;
            _m[11] = 0f;

            _m[12] = 0f;
            _m[13] = 0f;
            _m[14] = 0f;
            _m[15] = 1f;
        }

        public Vec3 TransformPoint(Vec3 p)
        {
            float x = p.x * _m[0] + p.y * _m[1] + p.z * _m[2] + _m[3];
            float y = p.x * _m[4] + p.y * _m[5] + p.z * _m[6] + _m[7];
            float z = p.x * _m[8] + p.y * _m[9] + p.z * _m[10] + _m[11];
            float w = p.x * _m[12] + p.y * _m[13] + p.z * _m[14] + _m[15];

            if (w != 1)
                return new Vec3(x / w, y / w, z / w);
            else
                return new Vec3(x, y, z);
        }

        public Vec3 TransformVector(Vec3 p)
        {
            float x = p.x * _m[0] + p.y * _m[1] + p.z * _m[2];
            float y = p.x * _m[4] + p.y * _m[5] + p.z * _m[6];
            float z = p.x * _m[8] + p.y * _m[9] + p.z * _m[10];

            return new Vec3(x, y, z);
        }

        public Matrix4 Transposed()
        {
            Matrix4 t = new Matrix4();

            t._m[0] = _m[0]; t._m[1] = _m[4]; t._m[2] = _m[8]; t._m[3] = _m[12];
            t._m[4] = _m[1]; t._m[5] = _m[5]; t._m[6] = _m[9]; t._m[7] = _m[13];
            t._m[8] = _m[2]; t._m[9] = _m[6]; t._m[10] = _m[10]; t._m[11] = _m[14];
            t._m[12] = _m[3]; t._m[13] = _m[7]; t._m[14] = _m[11]; t._m[15] = _m[15];

            return t;
        }

        public Matrix4 Inverse()
        {
            float s0 = _m[0] * _m[5] - _m[1] * _m[4];
            float s1 = _m[0] * _m[9] - _m[1] * _m[8];
            float s2 = _m[0] * _m[13] - _m[1] * _m[12];
            float s3 = _m[4] * _m[9] - _m[5] * _m[8];
            float s4 = _m[4] * _m[13] - _m[5] * _m[12];
            float s5 = _m[8] * _m[13] - _m[9] * _m[12];

            float c0 = _m[2] * _m[7] - _m[3] * _m[6];
            float c1 = _m[2] * _m[11] - _m[3] * _m[10];
            float c2 = _m[2] * _m[15] - _m[3] * _m[14];
            float c3 = _m[6] * _m[11] - _m[7] * _m[10];
            float c4 = _m[6] * _m[15] - _m[7] * _m[14];
            float c5 = _m[10] * _m[15] - _m[11] * _m[14];

            float dI = 1.0f / (s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0);

            Matrix4 i = new Matrix4();

            i._m[0] = (_m[5] * c5 - _m[9] * c4 + _m[13] * c3) * dI;
            i._m[4] = (-_m[4] * c5 + _m[8] * c4 - _m[12] * c3) * dI;
            i._m[8] = (_m[7] * s5 - _m[11] * s4 + _m[15] * s3) * dI;
            i._m[12] = (-_m[6] * s5 + _m[10] * s4 - _m[14] * s3) * dI;

            i._m[1] = (-_m[1] * c5 + _m[9] * c2 - _m[13] * c1) * dI;
            i._m[5] = (_m[0] * c5 - _m[8] * c2 + _m[12] * c1) * dI;
            i._m[9] = (-_m[3] * s5 + _m[11] * s2 - _m[15] * s1) * dI;
            i._m[13] = (_m[2] * s5 - _m[10] * s2 + _m[14] * s1) * dI;

            i._m[2] = (_m[1] * c4 - _m[5] * c2 + _m[13] * c0) * dI;
            i._m[6] = (-_m[0] * c4 + _m[4] * c2 - _m[12] * c0) * dI;
            i._m[10] = (_m[3] * s4 - _m[7] * s2 + _m[15] * s0) * dI;
            i._m[14] = (-_m[2] * s4 + _m[6] * s2 - _m[14] * s0) * dI;

            i._m[3] = (-_m[1] * c3 + _m[5] * c1 - _m[9] * c0) * dI;
            i._m[7] = (_m[0] * c3 - _m[4] * c1 + _m[8] * c0) * dI;
            i._m[11] = (-_m[3] * s3 + _m[7] * s1 - _m[11] * s0) * dI;
            i._m[15] = (_m[2] * s3 - _m[6] * s1 + _m[10] * s0) * dI;

            return i;
        }

        public static Matrix4 RotateVectorByAxis(Vec3 centerPoint, Matrix4 rotationMatrix, Vec3 axis, double angle)
        {
            Matrix4 ret = rotationMatrix;
            if (!Utility.IsSame(angle, 0.0))
            {
                Matrix4 rotateMatrix = Matrix4.FromRotationAxisAngle(axis, (float)angle);
                rotateMatrix.SetTranslation(axis.x, axis.y, axis.z);
                ret = (ret != null ? rotateMatrix * ret : rotateMatrix);
            }
            return ret;
        }

        public static Matrix4 operator *(Matrix4 a, Matrix4 b)
        {
            return new Matrix4(Matrix4x4Multiply(a, b));
        }

        public override bool Equals(object obj)
        {
            return base.Equals(obj);
        }

        public static bool operator == (Matrix4 first, Matrix4 second)
        {
            if ((object)first == null)
                return (object)second == null;
            return first.Equals(second);
        }

        public static bool operator != (Matrix4 first, Matrix4 second)
        {
            return !(first == second);
        }
    }
}

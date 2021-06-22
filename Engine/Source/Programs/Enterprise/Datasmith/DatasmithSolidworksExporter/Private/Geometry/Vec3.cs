// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Vec3
    {
        public float x = 0f;
        public float y = 0f;
        public float z = 0f;

        public static readonly Vec3 Zero = new Vec3(0f, 0f, 0f);
        public static readonly Vec3 One = new Vec3(1f, 1f, 1f);
        public static readonly Vec3 XAxis = new Vec3(1f, 0f, 0f);
        public static readonly Vec3 YAxis = new Vec3(0f, 1f, 0f);
        public static readonly Vec3 ZAxis = new Vec3(0f, 0f, 1f);

        static Vec3()
        {
        }

        public Vec3()
        {
        }

        public Vec3(double x, double y, double z)
        {
            this.x = (float)x;
            this.y = (float)y;
            this.z = (float)z;
        }

        public Vec3(float x, float y, float z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public Vec3(Vec3 other)
        {
            x = other.x;
            y = other.y;
            z = other.z;
        }

        public Vec3(double[] other)
        {
            x = (float)other[0];
            y = (float)other[1];
            z = (float)other[2];
        }

        public Vec3(MathVector swVec)
        {
            double[] data = swVec.ArrayData();
            x = (float)data[0];
            y = (float)data[1];
            z = (float)data[2];
        }

        public override bool Equals(object obj)
        {
            return base.Equals(obj);
        }

        public override int GetHashCode()
        {
            int hash = 1;
            hash = hash * 17 + x.GetHashCode();
            hash = hash * 17 + y.GetHashCode();
            hash = hash * 17 + z.GetHashCode();
            return hash;
        }

        public bool IsZero()
        {
            return (x == 0f && y == 0f && z == 0f);
        }

        public float SquareMagnitude()
        {
            return (x * x) + (y * y) + (z * z);
        }

        public float Magnitude()
        {
            return (float)Math.Sqrt(SquareMagnitude());
        }

        public Vec3 Normalized()
        {
            Vec3 n = new Vec3(this);
            float mag = Magnitude();
            if (mag != 0.0f)
            {
                mag = 1.0f / mag;
                n.x *= mag;
                n.y *= mag;
                n.z *= mag;
            }
            return n;
        }

        public Vec3 Cleared()
        {
            return new Vec3(
                (x == float.NaN) ? 0.0f : x,
                (y == float.NaN) ? 0.0f : y,
                (z == float.NaN) ? 0.0f : z);
        }

        public static bool operator !=(Vec3 a, Vec3 b)
        {
            return !(a == b);
        }

        public static bool operator ==(Vec3 v1, Vec3 v2)
        {
            bool same = Utility.IsSame(v1.x, v2.x);
            if (same)
            {
                same = Utility.IsSame(v1.y, v2.y);
                if (same)
                {
                    same = Utility.IsSame(v1.z, v2.z);
                }
            }
            return same;
        }

        public static bool operator <(Vec3 v1, Vec3 v2)
        {
            if (v1 == v2)
                return false;

            bool min = v1.x < v2.x;
            if (!min)
            {
                min = v1.y < v2.y;
                if (!min)
                {
                    min = v1.z < v2.z;
                }
            }
            return min;
        }

        public static bool operator >(Vec3 v1, Vec3 v2)
        {
            if (v1 == v2)
                return false;

            bool maj = v1.x > v2.x;
            if (!maj)
            {
                maj = v1.y > v2.y;
                if (!maj)
                {
                    maj = v1.z > v2.z;
                }
            }
            return maj;
        }

        public static float Dot(Vec3 v1, Vec3 v2)
        {
            return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
        }

        public static Vec3 Cross(Vec3 v1, Vec3 v2)
        {
            return new Vec3(
                v1.z * v2.y - v1.y * v2.z,
                v1.x * v2.z - v1.z * v2.x,
                v1.y * v2.x - v1.x * v2.y);
        }

        public static Vec3 operator * (Vec3 v, float m)
        {
            return new Vec3(v.x * m, v.y * m, v.z * m);
        }

        public static Vec3 operator *(Vec3 v, Vec3 m)
        {
            return new Vec3(v.x * m.x, v.y * m.y, v.z * m.z);
        }

        public static Vec3 operator /(Vec3 v, float m)
        {
            return new Vec3(v.x / m, v.y / m, v.z / m);
        }

        public static Vec3 operator -(Vec3 v, float m)
        {
            return new Vec3(v.x - m, v.y - m, v.z - m);
        }

        public static Vec3 operator -(Vec3 v, Vec3 m)
        {
            return new Vec3(v.x - m.x, v.y - m.y, v.z - m.z);
        }

        public static Vec3 operator +(Vec3 v, float m)
        {
            return new Vec3(v.x + m, v.y + m, v.z + m);
        }

        public static Vec3 operator +(Vec3 v, Vec3 m)
        {
            return new Vec3(v.x + m.x, v.y + m.y, v.z + m.z);
        }

        public override string ToString()
        {
            return "" + x + "," + y + "," + z;
        }

        public MathPoint ToMathPoint()
        {
            return MathUtil.CreatePoint(x, y, z);
        }

        public MathVector ToMathVector()
        {
            return MathUtil.CreateVector(x, y, z);
        }

    }
}

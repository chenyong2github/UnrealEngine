// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Vec2
    {
        public float x = 0f;
        public float y = 0f;

        public Vec2()
        {
        }

        public Vec2(double x, double y)
        {
            this.x = (float)x;
            this.y = (float)y;
        }

        public Vec2(float x, float y)
        {
            this.x = x;
            this.y = y;
        }

        public static Vec2 Rotate(Vec2 original, float rotate)
        {
            return new Vec2((float)(original.x * Math.Cos(rotate) + original.y * Math.Sin(rotate)), (float)(-original.x * Math.Sin(rotate) + original.y * Math.Cos(rotate)));
        }

        public static Vec2 Translate(Vec2 original, Vec2 translate)
        {
            return new Vec2(original.x + translate.y, original.y + translate.y);
        }

        public static Vec2 Scale(Vec2 original, Vec2 scale)
        {
            return new Vec2(original.x * scale.y, original.y * scale.y);
        }

        public static Vec2 operator -(Vec2 v, Vec2 v2)
        {
            return new Vec2(v.x - v2.x, v.y - v2.y);
        }

        public static Vec2 operator +(Vec2 v, Vec2 v2)
        {
            return new Vec2(v.x + v2.x, v.y + v2.y);
        }

        public static Vec2 operator /(Vec2 v, float value)
        {
            if (Utility.IsSame(value, 0f))
            {
                throw new InvalidOperationException();
            }
            return new Vec2(v.x / value, v.y / value);
        }

        public static Vec2 operator *(Vec2 v, float value)
        {
            return new Vec2(v.x * value, v.y * value);
        }

        public static Vec2 RotateOnPlane(Vec2 CosSin, Vec2 vec)
        {
            return new Vec2((vec.x * CosSin.x - vec.y * CosSin.y), (vec.x * CosSin.y + vec.y * CosSin.x));
        }

        public override string ToString()
        {
            return "" + x + "," + y;
        }
    }
}

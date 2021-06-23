// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class BoundingBox
    {
        public BoundingBox()
        {
            _bounds = null;
        }

        public BoundingBox(double[] bounds)
        {
            Add(bounds);
        }

        double[] _bounds;
        public double[] Bounds
        {
            get
            {
                if (_bounds != null)
                    return _bounds;
                else
                    return new double[6];
            }
            set
            {
                _bounds = value;
            }
        }

        public Vec3 Center { get; private set; }
        public float Size { get; private set; }
        public float Volume { get; private set; }
        public Vec3 Min { get { return new Vec3(_bounds[0], _bounds[1], _bounds[2]); } }
        public Vec3 Max { get { return new Vec3(_bounds[3], _bounds[4], _bounds[5]); } }

        public void Add(BoundingBox other)
        {
            Add(other.Bounds);
        }

        public void Add(double[] bounds)
        {
            if (_bounds == null)
            {
                _bounds = new double[6];
                Array.Copy(bounds, _bounds, 6);
            }
            else
            {
                _bounds[0] = Math.Min(_bounds[0], bounds[0]);
                _bounds[1] = Math.Min(_bounds[1], bounds[1]);
                _bounds[2] = Math.Min(_bounds[2], bounds[2]);

                _bounds[3] = Math.Max(_bounds[3], bounds[3]);
                _bounds[4] = Math.Max(_bounds[4], bounds[4]);
                _bounds[5] = Math.Max(_bounds[5], bounds[5]);
            }

            double dX = _bounds[3] - _bounds[0];
            double dY = _bounds[4] - _bounds[1];
            double dZ = _bounds[5] - _bounds[2];

            Center = new Vec3(
                _bounds[0] + dX / 2,
                _bounds[1] + dY / 2,
                _bounds[2] + dZ / 2);

            Size = (float)Math.Max(dX, Math.Max(dY, dZ));

            Volume = (float)(dX * dY * dZ);
        }

        public bool Contains(Vec3 p)
        {
            return
                p.x >= _bounds[0] && p.x <= _bounds[3] &&
                p.y >= _bounds[1] && p.y <= _bounds[4] &&
                p.z >= _bounds[2] && p.z <= _bounds[5];
        }

        public bool Contains(Vec3 p, double tolerance)
        {
            return
                p.x >= (_bounds[0] - tolerance) && p.x <= (_bounds[3] + tolerance) &&
                p.y >= (_bounds[1] - tolerance) && p.y <= (_bounds[4] + tolerance) &&
                p.z >= (_bounds[2] - tolerance) && p.z <= (_bounds[5] + tolerance);
        }

        public BoundingBox Transform(Matrix4 m)
        {
            Vec3 min = m.TransformPoint(Min);
            Vec3 max = m.TransformPoint(Max);

            double[] bounds = new double[6];

            bounds[0] = Math.Min(min.x, max.x);
            bounds[1] = Math.Min(min.y, max.y);
            bounds[2] = Math.Min(min.z, max.z);

            bounds[3] = Math.Max(min.x, max.x);
            bounds[4] = Math.Max(min.y, max.y);
            bounds[5] = Math.Max(min.z, max.z);

            return new BoundingBox(bounds);
        }

        public static bool operator ==(BoundingBox a, BoundingBox b)
        {
            if ((object)a == null)
                return (object)b == null;

            return a.Equals(b);
        }

        public static bool operator !=(BoundingBox a, BoundingBox b)
        {
            return !(a == b);
        }

        public override bool Equals(object o)
        {
            BoundingBox other = o as BoundingBox;
            if (o == null)
                return false;

            if (_bounds == null && other._bounds == null)
                return true;

            if (_bounds == null && other._bounds != null)
                return false;

            if (_bounds != null && other._bounds == null)
                return false;

            for (int i = 0; i < 6; i++)
                if (_bounds[i] != other.Bounds[i])
                    return false;

            return true;
        }

        public override int GetHashCode()
        {
            int hash = 47;

            foreach (double v in _bounds)
                hash = (hash * 53) ^ v.GetHashCode();

            return hash;
        }
    }
}

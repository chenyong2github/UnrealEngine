// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.SwObjects;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Materials
{
    [ComVisible(false)]
    public class MaterialMapper
    {
        private List<SwMaterial> Materials = new List<SwMaterial>();
        public Dictionary<string, SwMaterial> MaterialUsers_bodies = new Dictionary<string, SwMaterial>();
        public Dictionary<string, SwMaterial> MaterialUsers_features = new Dictionary<string, SwMaterial>();
        public Dictionary<string, SwMaterial> MaterialUsers_components = new Dictionary<string, SwMaterial>();
        public Dictionary<string, SwMaterial> MaterialUsers_parts = new Dictionary<string, SwMaterial>();
        public Dictionary<uint, SwMaterial> MaterialUsers_faces = new Dictionary<uint, SwMaterial>();

        public enum EntityType
        {
            MU_BODY,
            MU_FEATURE,
            MU_FACE,
            MU_COMPONENT,
            MU_PART
        };

        public void SetMaterialUser(EntityType type, uint id, SwMaterial mat)
        {
            Dictionary<uint, SwMaterial> users = null;
            switch (type)
            {
                case EntityType.MU_FACE: users = MaterialUsers_faces; break;
            }
            if (users != null)
            {
                if (users.ContainsKey(id))
                    users[id] = mat;
                else
                    users.Add(id, mat);
            }
        }

        public void SetMaterialUser(EntityType type, string id, SwMaterial mat)
        {
            Dictionary<string, SwMaterial> users = null;
            switch (type)
            {
                case EntityType.MU_COMPONENT: users = MaterialUsers_components; break;
                case EntityType.MU_PART: users = MaterialUsers_parts; break;
                case EntityType.MU_BODY: users = MaterialUsers_bodies; break;
                case EntityType.MU_FEATURE: users = MaterialUsers_features; break;
            }

            if (users != null)
            {
                if (users.ContainsKey(id))
				{
                    users[id] = mat;
				}
                else
				{
                    users.Add(id, mat);
				}
            }
        }

        public SwMaterial GetUserMaterial(EntityType type, uint id)
        {
            Dictionary<uint, SwMaterial> users = null;
            SwMaterial res = null;
            switch (type)
            {
                case EntityType.MU_FACE: users = MaterialUsers_faces; break;
            }
            if (users != null)
            {
                if (users.ContainsKey(id))
                    res = users[id];
            }
            return res;
        }

        public SwMaterial GetUserMaterial(EntityType type, string id)
        {
            Dictionary<string, SwMaterial> users = null;
            SwMaterial res = null;
            switch (type)
            {
                case EntityType.MU_COMPONENT: users = MaterialUsers_components; break;
                case EntityType.MU_PART: users = MaterialUsers_parts; break;
                case EntityType.MU_BODY: users = MaterialUsers_bodies; break;
                case EntityType.MU_FEATURE: users = MaterialUsers_features; break;
            }
            if (users != null)
            {
                if (users.ContainsKey(id))
                    res = users[id];
            }
            return res;
        }

        public void ClearMaterialUsers(bool everything = false)
        {
            MaterialUsers_bodies.Clear();
            MaterialUsers_features.Clear();
            MaterialUsers_faces.Clear();
            MaterialUsers_components.Clear();
            MaterialUsers_parts.Clear();
            if (everything)
            {
                Materials.Clear();
            }
        }

        public SwMaterial AddMaterial(RenderMaterial mat, IModelDocExtension ext)
        {
            SwMaterial swmat = null;
            if (mat != null)
            {
                swmat = new SwMaterial(mat, ext);
                Materials.Add(swmat);
            }
            return swmat;
        }

		public SwMaterial FindOrAddMaterial(RenderMaterial mat, IModelDocExtension ext)
		{
			SwMaterial swmat = Materials.FirstOrDefault(x => x.Source == mat && x.ID == SwMaterial.GetMaterialID(mat));
			if (swmat != null)
				return swmat;

			// The exactly matching material is not found, let's create a new one (even if it will be immediately removed)
			swmat = new SwMaterial(mat, ext);

			// The identical material still could exist, so we'll check the existing materials list
			SwMaterial found = Materials.FirstOrDefault(x => SwMaterial.AreTheSame(x, swmat, true));
			if (found != null)
			{
				// We'll return the existing material, and swmat will be GC'ed
				return found;
			}

			// We'll continue with the newly created material
			Materials.Add(swmat);
			return swmat;
		}
	}
}

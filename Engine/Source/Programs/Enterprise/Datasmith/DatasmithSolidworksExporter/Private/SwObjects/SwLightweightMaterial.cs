// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

namespace SolidworksDatasmith.SwObjects
{
	public class SwLightweightMaterial
	{
		public int ID = -1;
		private string _name = "";
		private System.Drawing.Color _color = System.Drawing.Color.Black;
		private float _ambient = 0f;
		private float _diffuse = 0f;
		private float _specular = 0f;
		private float _shininess = 0f;
		private float _transparency = 0f;
		private float _emission = 0f;
		// extra from appearance
		private System.Drawing.Color _specularColor = System.Drawing.Color.Black;
		private float _specularSpread = 0f;
		private float _reflection = 0f;
		private bool _blurryReflection = false;

		public string Name { get { return _name; } }
		public System.Drawing.Color Color { get { return _color; } }
		public float Ambient { get { return _ambient; } }
		public float Diffuse { get { return _diffuse; } }
		public float Specular { get { return _specular; } }
		public float Shininess { get { return _shininess; } }
		public float Transparency { get { return _transparency; } }
		public float Emission { get { return _emission; } }
		// extra from appearance
		public System.Drawing.Color SpecularColor { get { return _specularColor; } }
		public float SpecularSpread { get { return _specularSpread; } }
		public float Reflection { get { return _reflection; } }
		public bool BlurryReflection { get { return _blurryReflection; } }

		// [ R, G, B, Ambient, Diffuse, Specular, Shininess, Transparency, Emission ]
		public SwLightweightMaterial(double[] properties)
		{
			_name = Guid.NewGuid().ToString();
			_color = System.Drawing.Color.FromArgb((int)(properties[0] * 255.0), (int)(properties[1] * 255.0), (int)(properties[2] * 255.0));
			_ambient = (float)properties[3];
			_diffuse = (float)properties[4];
			_specular = (float)properties[5];
			_shininess = (float)properties[6];
			_transparency = (float)properties[7];
			_emission = (float)properties[8];
		}

		public void SetAppearance(IAppearanceSetting appearance)
		{
			_specularColor = System.Drawing.Color.FromArgb(appearance.SpecularColor);
			_specularSpread = (float)appearance.SpecularSpread;
			_reflection = (float)appearance.Reflection;
			_blurryReflection = appearance.BlurryReflection;
		}

		public static bool AreTheSame(SwLightweightMaterial mat1, SwLightweightMaterial mat2)
		{
			if (mat1.Color != mat2.Color) return false;
			if (!Utility.IsSame(mat1.Ambient, mat2.Ambient)) return false;
			if (!Utility.IsSame(mat1.Diffuse, mat2.Diffuse)) return false;
			if (!Utility.IsSame(mat1.Specular, mat2.Specular)) return false;
			if (!Utility.IsSame(mat1.Shininess, mat2.Shininess)) return false;
			if (!Utility.IsSame(mat1.Transparency, mat2.Transparency)) return false;
			if (!Utility.IsSame(mat1.Emission, mat2.Emission)) return false;
			if (mat1._specularColor != mat2._specularColor) return false;
			if (!Utility.IsSame(mat1._specularSpread, mat2._specularSpread)) return false;
			if (!Utility.IsSame(mat1._reflection, mat2._reflection)) return false;
			if (mat1._blurryReflection != mat2._blurryReflection) return false;
			return true;
		}

		public SwMaterial ToSwMaterial()
		{
			SwMaterial material = new SwMaterial();
			material.Type = (int)swRenderMaterialIlluminationTypes_e.swRenderMaterialIlluminationTypes_plastic;
			material.Name = "material_" + ID.ToString();
			material.ID = ID;
			material.Emission = Emission;
			material.PrimaryColor = Color;
			material.Diffuse = Diffuse;
			material.Specular = Specular;
			material.Roughness = (1f - Shininess) * (1f - Shininess);
			material.Transparency = Transparency;
			material.SpecularColor = SpecularColor.ToArgb();
			material.SpecularSpread = SpecularSpread;
			material.Reflectivity = Reflection;
			material.BlurryReflections = BlurryReflection;
			return material;
		}
	}
}

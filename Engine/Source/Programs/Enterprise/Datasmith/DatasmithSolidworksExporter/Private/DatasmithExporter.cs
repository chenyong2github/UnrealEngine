// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidworksDatasmith.Geometry;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith
{
	[ComVisible(false)]
	public class DatasmithExporter
	{
		private const string original = "^/()#$&.?!ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿБбВвГгДдЁёЖжЗзИиЙйКкЛлМмНнОоПпРрСсТтУуФфХхЦцЧчШшЩщЪъЫыЬьЭэЮюЯя'\"";
		private const string modified = "_____S____AAAAAAECEEEEIIIIDNOOOOOx0UUUUYPsaaaaaaeceeeeiiiiOnoooood0uuuuypyBbVvGgDdEeJjZzIiYyKkLlMmNnOoPpRrSsTtUuFfJjTtCcSsSs__ii__EeYyYy__";

		public DatasmithExporter()
		{
		}
		// #ue_datasmith_todo: reuse sdk capabilities
		public static string SanitizeName(string ioStringToSanitize)
		{
			string result = "";
			for (int i = 0; i < ioStringToSanitize.Length; i++)
			{
				if (ioStringToSanitize[i] <= 32)
				{
					result += '_';
				}
				else
				{
					bool replaced = false;
					for (int j = 0; j < original.Length; j++)
					{
						if (ioStringToSanitize[i] == original[j])
						{
							result += modified[j];
							replaced = true;
							break;
						}
					}
					if (!replaced)
						result += ioStringToSanitize[i];
				}
			}
			return result;
		}

		public static void SanitizeNameInplace(ref string ioStringToSanitize)
		{
			StringBuilder res = new StringBuilder();
			for (int i = 0; i < ioStringToSanitize.Length; i++)
			{
				for (int j = 0; j < original.Length; j++)
				{
					if (ioStringToSanitize[i] == original[j])
					{
						res.Append(modified[j]);
					}
					else
					{
						res.Append(ioStringToSanitize[i]);
					}
				}
			}

			//now replace all non printable characters and replace space (32) by '_'
			res = new StringBuilder();
			for (int i = 0; i < ioStringToSanitize.Length; i++)
			{
				if ((byte)ioStringToSanitize[i] <= 32)
				{
					res.Append('_');
				}
				else
				{
					res.Append(ioStringToSanitize[i]);
				}
			}
			ioStringToSanitize = res.ToString();
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal class UhtPackageCodeGeneratorHFile : UhtPackageCodeGenerator
	{
		/// <summary>
		/// Construct an instance of this generator object
		/// </summary>
		/// <param name="CodeGenerator">The base code generator</param>
		/// <param name="Package">Package being generated</param>
		public UhtPackageCodeGeneratorHFile(UhtCodeGenerator CodeGenerator, UhtPackage Package)
			: base(CodeGenerator, Package)
		{
		}

		/// <summary>
		/// For a given UE header file, generated the generated H file
		/// </summary>
		/// <param name="HeaderOutput">Output object</param>
		/// <param name="PackageSortedHeaders">Sorted list of headers by name of all headers in the package</param>
		public void Generate(IUhtExportOutput HeaderOutput, List<UhtHeaderFile> PackageSortedHeaders)
		{
			using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Big))
			{
				StringBuilder Builder = Borrower.StringBuilder;

				Builder.Append(HeaderCopyright);
				Builder.Append("#pragma once\r\n");
				Builder.Append("\r\n");
				Builder.Append("\r\n");

				List<UhtHeaderFile> HeaderFiles = new List<UhtHeaderFile>(this.Package.Children.Count * 2);
				HeaderFiles.AddRange(PackageSortedHeaders);

				foreach (UhtHeaderFile HeaderFile in this.Package.Children)
				{
					if (HeaderFile.HeaderFileType == UhtHeaderFileType.Classes)
					{
						HeaderFiles.Add(HeaderFile);
					}
				}

				List<UhtHeaderFile> SortedHeaderFiles = new List<UhtHeaderFile>(HeaderFiles.Distinct());
				SortedHeaderFiles.Sort((x, y) => StringComparerUE.OrdinalIgnoreCase.Compare(x.FilePath, y.FilePath));

				foreach (UhtHeaderFile HeaderFile in SortedHeaderFiles)
				{
					if (HeaderFile.HeaderFileType == UhtHeaderFileType.Classes)
					{
						Builder.Append("#include \"").Append(this.HeaderInfos[HeaderFile.HeaderFileTypeIndex].IncludePath).Append("\"\r\n");
					}
				}

				Builder.Append("\r\n");
				HeaderOutput.CommitOutput(Builder);
			}
		}
	}
}

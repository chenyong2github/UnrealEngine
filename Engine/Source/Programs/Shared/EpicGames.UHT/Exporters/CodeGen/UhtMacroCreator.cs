// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Types;
using System;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal struct UhtMacroCreator : IDisposable
	{
		private readonly StringBuilder Builder;
		private readonly int StartingLength;

		public UhtMacroCreator(StringBuilder Builder, UhtHeaderCodeGenerator Generator, int LineNumber, string MacroSuffix)
		{
			Builder.Append("#define ").AppendMacroName(Generator, LineNumber, MacroSuffix).Append(" \\\r\n");
			this.Builder = Builder;
			this.StartingLength = Builder.Length;
		}

		public UhtMacroCreator(StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtClass Class, string MacroSuffix)
		{
			Builder.Append("#define ").AppendMacroName(Generator, Class, MacroSuffix).Append(" \\\r\n");
			this.Builder = Builder;
			this.StartingLength = Builder.Length;
		}

		public UhtMacroCreator(StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtScriptStruct ScriptStruct, string MacroSuffix)
		{
			Builder.Append("#define ").AppendMacroName(Generator, ScriptStruct, MacroSuffix).Append(" \\\r\n");
			this.Builder = Builder;
			this.StartingLength = Builder.Length;
		}

		public UhtMacroCreator(StringBuilder Builder, UhtHeaderCodeGenerator Generator, UhtFunction Function, string MacroSuffix)
		{
			Builder.Append("#define ").AppendMacroName(Generator, Function, MacroSuffix).Append(" \\\r\n");
			this.Builder = Builder;
			this.StartingLength = Builder.Length;
		}

		public void Dispose()
		{
			int FinalLength = Builder.Length;
			Builder.Length = Builder.Length - 4;
			if (FinalLength == StartingLength)
			{
				Builder.Append("\r\n");
			}
			else
			{
				Builder.Append("\r\n\r\n\r\n");
			}
		}
	}
}

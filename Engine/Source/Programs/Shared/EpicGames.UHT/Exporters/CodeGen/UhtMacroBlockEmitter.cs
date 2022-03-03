// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal struct UhtMacroBlockEmitter : IDisposable
	{
		private readonly StringBuilder Builder;
		private readonly string MacroName;
		private bool bEmitted;

		public UhtMacroBlockEmitter(StringBuilder Builder, string MacroName, bool bInitialState = false)
		{
			this.Builder = Builder;
			this.MacroName = MacroName;
			this.bEmitted = false;
			Set(bInitialState);
		}

		public void Set(bool bEmit)
		{
			if (this.bEmitted == bEmit)
			{
				return;
			}
			this.bEmitted = bEmit;
			if (this.bEmitted)
			{
				Builder.Append("#if ").Append(this.MacroName).Append("\r\n");
			}
			else
			{
				Builder.Append("#endif // ").Append(this.MacroName).Append("\r\n");
			}
		}

		public void Dispose()
		{
			Set(false);
		}
	}
}

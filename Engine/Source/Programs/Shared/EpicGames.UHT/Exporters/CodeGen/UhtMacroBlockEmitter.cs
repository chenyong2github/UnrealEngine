// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal struct UhtMacroBlockEmitter : IDisposable
	{
		private readonly StringBuilder _builder;
		private readonly string _macroName;
		private bool _bEmitted;

		public UhtMacroBlockEmitter(StringBuilder builder, string macroName, bool initialState = false)
		{
			this._builder = builder;
			this._macroName = macroName;
			this._bEmitted = false;
			Set(initialState);
		}

		public void Set(bool emit)
		{
			if (this._bEmitted == emit)
			{
				return;
			}
			this._bEmitted = emit;
			if (this._bEmitted)
			{
				_builder.Append("#if ").Append(this._macroName).Append("\r\n");
			}
			else
			{
				_builder.Append("#endif // ").Append(this._macroName).Append("\r\n");
			}
		}

		public void Dispose()
		{
			Set(false);
		}
	}
}

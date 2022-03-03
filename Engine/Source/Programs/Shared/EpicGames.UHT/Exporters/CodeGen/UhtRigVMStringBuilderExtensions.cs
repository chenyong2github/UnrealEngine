// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal static class UhtRigVMStringBuilderExtensions
	{
		public static StringBuilder AppendParameterNames(this StringBuilder Builder, IEnumerable<UhtRigVMParameter> Parameters, 
			bool bLeadingSeparator = false, string Separator = ", ", bool bCastName = false, bool bIncludeEditorOnly = false)
		{
			bool bEmitSeparator = bLeadingSeparator;
			foreach (UhtRigVMParameter Parameter in Parameters)
			{
				if (bIncludeEditorOnly && !Parameter.bEditorOnly)
				{
					continue;
				}
				if (bEmitSeparator)
				{
					Builder.Append(Separator);
				}
				bEmitSeparator = true;
				Builder.Append(Parameter.NameOriginal(bCastName));
			}
			return Builder;
		}

		public static StringBuilder AppendTypeConstRef(this StringBuilder Builder, UhtRigVMParameter Parameter, bool bCastType = false)
		{
			Builder.Append("const ");
			string String = Parameter.TypeOriginal(bCastType);
			ReadOnlySpan<char> Span = String.AsSpan();
			if (Span.EndsWith("&"))
			{
				Span = Span.Slice(0, Span.Length - 1);
			}
			Builder.Append(Span);
			if (Span.Length > 0 && (Span[0] == 'T' || Span[0] == 'F'))
			{
				Builder.Append('&');
			}
			return Builder;
		}

		public static StringBuilder AppendTypeRef(this StringBuilder Builder, UhtRigVMParameter Parameter, bool bCastType = false)
		{
			string String = Parameter.TypeOriginal(bCastType);
			ReadOnlySpan<char> Span = String.AsSpan();
			if (Span.EndsWith("&"))
			{
				Builder.Append(Span);
			}
			else
			{
				Builder.Append(Span).Append('&');
			}
			return Builder;
		}

		public static StringBuilder AppendTypeNoRef(this StringBuilder Builder, UhtRigVMParameter Parameter, bool bCastType = false)
		{
			string String = Parameter.TypeOriginal(bCastType);
			ReadOnlySpan<char> Span = String.AsSpan();
			if (Span.EndsWith("&"))
			{
				Span = Span.Slice(0, Span.Length - 1);
			}
			return Builder.Append(Span);
		}

		public static StringBuilder AppendTypeVariableRef(this StringBuilder Builder, UhtRigVMParameter Parameter, bool bCastType = false)
		{
			if (Parameter.IsConst())
			{
				Builder.AppendTypeConstRef(Parameter, bCastType);
			}
			else
			{
				Builder.AppendTypeRef(Parameter, bCastType);
			}
			return Builder;
		}

		public static StringBuilder AppendParameterDecls(this StringBuilder Builder, IEnumerable<UhtRigVMParameter> Parameters, 
			bool bLeadingSeparator = false, string Separator = ", ", bool bCastType = false, bool bCastName = false, bool bIncludeEditorOnly = false)
		{
			bool bEmitSeparator = bLeadingSeparator;
			foreach (UhtRigVMParameter Parameter in Parameters)
			{
				if (bIncludeEditorOnly && !Parameter.bEditorOnly)
				{
					continue;
				}
				if (bEmitSeparator)
				{
					Builder.Append(Separator);
				}
				bEmitSeparator = true;
				Builder
					.AppendTypeVariableRef(Parameter, bCastType)
					.Append(' ')
					.Append(bCastName ? Parameter.CastName : Parameter.Name);
			}
			return Builder;
		}
	}
}

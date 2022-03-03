// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "TextProperty", IsProperty = true)]
	public class UhtTextProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "TextProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "FText"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.EngineClass; }

		public UhtTextProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef | UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			this.PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerKey);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FTextPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FTextPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Text");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("FText::GetEmpty()");
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			if (DefaultValueReader.TryOptional("FText"))
			{
				if (DefaultValueReader.TryOptional('('))
				{
					DefaultValueReader.Require(')');
					return true;
				}
				else if (DefaultValueReader.TryOptional("::"))
				{
					// Handle legacy cases of FText::FromString being used as default values
					// These should be replaced with INVTEXT as FText::FromString can produce inconsistent keys
					if (DefaultValueReader.TryOptional("FromString"))
					{
						DefaultValueReader.Require('(');
						StringView Value = DefaultValueReader.GetWrappedConstString();
						DefaultValueReader.Require(')');
						InnerDefaultValue.Append("\"").Append(Value).Append("\"");
						this.LogWarning("FText::FromString should be replaced with INVTEXT for default parameter values");
					}
					else
					{
						DefaultValueReader.Require("GetEmpty").Require('(').Require(')');
					}
					return true;
				}
				else
				{
					return false;
				}
			}

			UhtToken Token = DefaultValueReader.GetToken();

			if (Token.IsIdentifier("LOCTEXT"))
			{
				//ETSTODO - Add a test case for this
				this.LogError($"LOCTEXT default parameter values are not supported; use NSLOCTEXT instead: {this.SourceName}");
				return false;
			}
			else
			{
				return SanitizeDefaultValue(this, DefaultValueReader, ref Token, InnerDefaultValue, false);
			}
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtTextProperty;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.CppTypeText;
		}

		#region Parsing keywords and default parsers		
		[UhtPropertyType(Keyword = "FText", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		private static UhtProperty? TextProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			return new UhtTextProperty(PropertySettings);
		}

		[UhtPropertyType(Keyword = "Text", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate | UhtPropertyTypeOptions.CaseInsensitive)]
		public static UhtProperty? MissingPrefixTextProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			throw new UhtException(TokenReader, "'Text' is missing a prefix, expecting 'FText'");
		}

		[UhtLocTextDefaultValue(Name = "INVTEXT")]
		private static bool InvTextDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require('(');
			StringView String = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(')');
			InnerDefaultValue.Append("INVTEXT(").Append(String).Append(')');
			return true;
		}

		[UhtLocTextDefaultValue(Name = "LOCTEXT")]
		private static bool LocTextDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			Property.LogError($"LOCTEXT default parameter values are not supported; use NSLOCTEXT instead: {Property.SourceName}");
			return false;
		}

		[UhtLocTextDefaultValue(Name = "NSLOCTEXT")]
		private static bool NsLocTextDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require('(');
			StringView NamespaceString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(',');
			StringView KeyString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(',');
			StringView SourceString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(')');

			// Strip out the package name
			ReadOnlySpan<char> StrippedNS = NamespaceString.Span;
			if (StrippedNS.Length > 1)
			{
				StrippedNS = StrippedNS.Slice(1, StrippedNS.Length - 2).Trim();
				if (StrippedNS.Length > 0)
				{
					if (StrippedNS[StrippedNS.Length - 1] == ']')
					{
						int Index = StrippedNS.LastIndexOf('[');
						if (Index != -1)
						{
							StrippedNS = StrippedNS.Slice(0, Index).TrimEnd();
						}
					}
				}
			}

			InnerDefaultValue.Append("NSLOCTEXT(\"").Append(StrippedNS).Append("\", ").Append(KeyString).Append(", ").Append(SourceString).Append(')');
			return true;
		}

		[UhtLocTextDefaultValue(Name = "LOCTABLE")]
		private static bool LocTableDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require('(');
			StringView NamespaceString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(',');
			StringView KeyString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(')');

			//ETSTODO - Validate the namespace string?
			InnerDefaultValue.Append("LOCTABLE(").Append(NamespaceString).Append(", ").Append(KeyString).Append(')');
			return true;
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_FORMAT_NAMED")]
		private static bool LocGenFormatNamedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			InnerDefaultValue.Append("LOCGEN_FORMAT_NAMED(");
			DefaultValueReader.Require('(');
			if (!SanitizeDefaultValue(Property, DefaultValueReader, InnerDefaultValue, false))
			{
				return false;
			}

			// RequireList assumes that we have already parsed the ','.  So if it is there, consume it.
			DefaultValueReader.TryOptional(',');

			bool bSuccess = true;
			DefaultValueReader.RequireList(')', ',', false, () =>
			{
				StringView Value = DefaultValueReader.GetWrappedConstString();
				DefaultValueReader.Require(',');
				InnerDefaultValue.Append(", \"").Append(Value).Append("\", ");
				if (!SanitizeDefaultValue(Property, DefaultValueReader, InnerDefaultValue, true))
				{
					bSuccess = false;
				}
			});
			InnerDefaultValue.Append(')');
			return bSuccess;
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_FORMAT_ORDERED")]
		private static bool LocGenFormatOrderedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			InnerDefaultValue.Append("LOCGEN_FORMAT_ORDERED(");
			DefaultValueReader.Require('(');
			if (!SanitizeDefaultValue(Property, DefaultValueReader, InnerDefaultValue, false))
			{
				return false;
			}

			// RequireList assumes that we have already parsed the ','.  So if it is there, consume it.
			DefaultValueReader.TryOptional(',');

			bool bSuccess = true;
			DefaultValueReader.RequireList(')', ',', false, () =>
			{
				InnerDefaultValue.Append(", ");
				if (!SanitizeDefaultValue(Property, DefaultValueReader, InnerDefaultValue, true))
				{
					bSuccess = false;
				}
			});
			InnerDefaultValue.Append(')');
			return bSuccess;
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_NUMBER")]
		private static bool LocGenNumberDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.None);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_NUMBER_GROUPED")]
		private static bool LocGenNumberGroupedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Grouped);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_NUMBER_UNGROUPED")]
		private static bool LocGenNumberUngroupedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Ungrouped);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_NUMBER_CUSTOM")]
		private static bool LocGenNumberCustomDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Custom);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_PERCENT")]
		private static bool LocGenPercentDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.None);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_PERCENT_GROUPED")]
		private static bool LocGenPercentGroupedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Grouped);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_PERCENT_UNGROUPED")]
		private static bool LocGenPercentUngroupedDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Ungrouped);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_PERCENT_CUSTOM")]
		private static bool LocGenPercentCustomDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenNumberOrPercentDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, NumberStyle.Custom);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_CURRENCY")]
		private static bool LocGenCurrencyDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require('(');

			double BaseValue;
			UhtToken Token = DefaultValueReader.GetToken();
			if (!Token.GetConstDouble(out BaseValue))
			{
				return false;
			}

			DefaultValueReader.Require(',');
			StringView CurrencyCode = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(',');
			StringView CultureNameString = DefaultValueReader.GetConstQuotedString();
			DefaultValueReader.Require(')');

			// With UHT, we end up outputting just the integer part of the specified value.  It is using default locale information in UHT
			InnerDefaultValue.Append("LOCGEN_CURRENCY(").Append((int)BaseValue).Append(", ").Append(CurrencyCode).Append(", ").Append(CultureNameString).Append(")");
			return true;
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATE_LOCAL")]
		private static bool LocGenDateLocalDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, false, false, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATE_UTC")]
		private static bool LocGenDateUtcDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, false, true, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_TIME_LOCAL")]
		private static bool LocGenTimeLocalDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, false, true, false, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_TIME_UTC")]
		private static bool LocGenTimeUtcDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, false, true, true, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATETIME_LOCAL")]
		private static bool LocGenDateTimeLocalDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, true, false, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATETIME_UTC")]
		private static bool LocGenDateTimeUtcDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, true, true, false);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATETIME_CUSTOM_LOCAL")]
		private static bool LocGenDateTimeCustomLocalDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, true, false, true);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_DATETIME_CUSTOM_UTC")]
		private static bool LocGenDateTimeCustomUtcDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenDateTimeDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue, true, true, true, true);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_TOUPPER")]
		private static bool LocGenToUpperDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenTransformDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue);
		}

		[UhtLocTextDefaultValue(Name = "LOCGEN_TOLOWER")]
		private static bool LocGenToLowerDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			return LocGenTransformDefaultValue(Property, DefaultValueReader, ref MacroToken, InnerDefaultValue);
		}
		#endregion

		#region Default value helper methods
		private static bool SanitizeDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue, bool bAllowNumerics)
		{
			UhtToken Token = DefaultValueReader.GetToken();
			return SanitizeDefaultValue(Property, DefaultValueReader, ref Token, InnerDefaultValue, bAllowNumerics);
		}

		private static bool SanitizeDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken Token, StringBuilder InnerDefaultValue, bool bAllowNumerics)
		{
			switch (Token.TokenType)
			{
				case UhtTokenType.Identifier:
					UhtLocTextDefaultValue LocTextDefaultValue;
					if (UhtLocTextDefaultValueTable.Instance.TryGet(Token.Value, out LocTextDefaultValue))
					{
						if (LocTextDefaultValue.Delegate(Property, DefaultValueReader, ref Token, InnerDefaultValue))
						{
							return true;
						}
					}
					return false;

				case UhtTokenType.StringConst:
					InnerDefaultValue.Append(Token.Value);
					return true;

				case UhtTokenType.DecimalConst:
					if (bAllowNumerics)
					{
						FormatDecimal(ref Token, InnerDefaultValue);
						return true;
					}
					return false;

				case UhtTokenType.FloatConst:
					if (bAllowNumerics)
					{
						FormatFloat(ref Token, InnerDefaultValue);
						return true;
					}
					return false;

				default:
					return false;
			}
		}

		private static void FormatDecimal(ref UhtToken Token, StringBuilder InnerDefaultValue)
		{
			ReadOnlySpan<char> Span = Token.Value.Span;
			int SuffixStart = Span.Length;
			bool bIsUnsigned = false;
			while (SuffixStart > 0)
			{
				char C = Span[SuffixStart - 1];
				if (UhtFCString.IsUnsignedMarker(C))
				{
					bIsUnsigned = true;
					SuffixStart--;
				}
				else if (UhtFCString.IsLongMarker(C))
				{
					SuffixStart--;
				}
				else
				{
					break;
				}
			}
			if (Token.GetConstLong(out long Value))
			{
				if (bIsUnsigned)
				{
					InnerDefaultValue.Append((ulong)Value).Append('u');
				}
				else
				{
					InnerDefaultValue.Append(Value);
				}
			}
		}

		private static void FormatFloat(ref UhtToken Token, StringBuilder InnerDefaultValue)
		{
			char C = Token.Value.Span[Token.Value.Length - 1];
			bool bIsFloat = UhtFCString.IsFloatMarker(C);
			if (Token.GetConstDouble(out double Value))
			{
				InnerDefaultValue.AppendFormat("{0:F6}", Value);
				if (bIsFloat)
				{
					InnerDefaultValue.Append(C);
				}
			}
		}

		enum NumberStyle
		{
			None,
			Grouped,
			Ungrouped,
			Custom,
		}

		private static bool LocGenNumberOfPercentBoolean(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		private static bool LocGenNumberOrPercentDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue, NumberStyle NumberStyle)
		{
			InnerDefaultValue.Append(MacroToken.Value.ToString());
			DefaultValueReader.Require('(');
			InnerDefaultValue.Append('(');

			UhtToken Token = DefaultValueReader.GetToken();
			switch (Token.TokenType)
			{
				case UhtTokenType.DecimalConst:
					FormatDecimal(ref Token, InnerDefaultValue);
					break;

				case UhtTokenType.FloatConst:
					FormatFloat(ref Token, InnerDefaultValue);
					break;

				default:
					return false;
			}
			DefaultValueReader.Require(',');
			InnerDefaultValue.Append(", ");

			if (NumberStyle == NumberStyle.Custom)
			{
				while (true)
				{
					UhtToken CustomToken = DefaultValueReader.GetToken();
					if (!CustomToken.IsIdentifier())
					{
						return false;
					}
					InnerDefaultValue.Append(CustomToken.Value);

					DefaultValueReader.Require('(');
					InnerDefaultValue.Append('(');

					switch (CustomToken.Value.ToString())
					{
						case "SetAlwaysSign":
						case "SetUseGrouping":
							{
								UhtToken BooleanToken = DefaultValueReader.GetToken();
								if (BooleanToken.IsIdentifier("true"))
								{
									InnerDefaultValue.Append("true");
								}
								else if (BooleanToken.IsIdentifier("false"))
								{
									InnerDefaultValue.Append("false");
								}
								else
								{
									return false;
								}
							}
							break;

						case "SetRoundingMode":
							{
								DefaultValueReader.Require("ERoundingMode");
								DefaultValueReader.Require("::");
								StringView Identifier = DefaultValueReader.GetIdentifier().Value;
								InnerDefaultValue.Append("ERoundingMode::").Append(Identifier);
							}
							break;

						case "SetMinimumIntegralDigits":
						case "SetMaximumIntegralDigits":
						case "SetMinimumFractionalDigits":
						case "MaximumFractionalDigits":
							{
								UhtToken NumericToken = DefaultValueReader.GetToken();
								if (NumericToken.GetConstInt(out int Value))
								{
									InnerDefaultValue.Append(Value);
								}
								else
								{
									return false;
								}
							}
							break;

						default:
							return false;
					}

					DefaultValueReader.Require(')');
					InnerDefaultValue.Append(')');

					if (!DefaultValueReader.TryOptional('.'))
					{
						break;
					}
					InnerDefaultValue.Append('.');
				}

				DefaultValueReader.Require(',');
				InnerDefaultValue.Append(", ");
			}

			StringView Culture = DefaultValueReader.GetConstQuotedString();
			InnerDefaultValue.Append("\"\""); // UHT doesn't write out the culture

			DefaultValueReader.Require(')');
			InnerDefaultValue.Append(')');
			return true;
		}

		private static void FormatDateTimeStyle(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require(',');
			DefaultValueReader.Require("EDateTimeStyle");
			DefaultValueReader.Require("::");
			StringView Identifier = DefaultValueReader.GetIdentifier().Value;
			InnerDefaultValue.Append(", EDateTimeStyle::").Append(Identifier);
		}

		private static bool LocGenDateTimeDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue, bool bDate, bool bTime, bool bUtc, bool bCustom)
		{
			DefaultValueReader.Require('(');
			InnerDefaultValue.Append(MacroToken.Value).Append('(');

			long UnixTimestampValue = 0;
			UhtToken TimestampToken = DefaultValueReader.GetToken();
			switch (TimestampToken.TokenType)
			{
				case UhtTokenType.FloatConst:
					if (TimestampToken.GetConstDouble(out double Value))
					{
						UnixTimestampValue = (long)Value;
						break;
					}
					else
					{
						return false;
					}

				case UhtTokenType.DecimalConst:
					if (!TimestampToken.GetConstLong(out UnixTimestampValue))
					{
						return false;
					}
					break;

				default:
					return false;

			}
			InnerDefaultValue.Append(UnixTimestampValue);

			if (bCustom)
			{
				DefaultValueReader.Require(',');
				StringView CustomPattern = DefaultValueReader.GetConstQuotedString();
				InnerDefaultValue.Append(", ").Append(CustomPattern);
			}
			else
			{
				if (bDate)
				{
					FormatDateTimeStyle(DefaultValueReader, InnerDefaultValue);
				}
				if (bTime)
				{
					FormatDateTimeStyle(DefaultValueReader, InnerDefaultValue);
				}
			}

			if (bUtc)
			{
				DefaultValueReader.Require(',');
				StringView TimeZone = DefaultValueReader.GetConstQuotedString();
				InnerDefaultValue.Append(", ").Append(TimeZone);
			}

			DefaultValueReader.Require(',');
			StringView CultureName = DefaultValueReader.GetConstQuotedString();
			InnerDefaultValue.Append(", ").Append("\"\""); // We don't write out the culture

			DefaultValueReader.Require(')');
			InnerDefaultValue.Append(")");
			return true;
		}

		private static bool LocGenTransformDefaultValue(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue)
		{
			InnerDefaultValue.Append(MacroToken.Value).Append('(');
			DefaultValueReader.Require('(');
			if (!SanitizeDefaultValue(Property, DefaultValueReader, InnerDefaultValue, false))
			{
				return false;
			}
			DefaultValueReader.Require(')');
			InnerDefaultValue.Append(')');
			return true;
		}

		#endregion
	}
}

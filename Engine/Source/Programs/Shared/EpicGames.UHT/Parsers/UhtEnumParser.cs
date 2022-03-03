// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{
	[UnrealHeaderTool]
	public class UhtEnumParser : UhtEnum
	{
		private static UhtKeywordTable KeywordTable = UhtKeywordTables.Instance.Get(UhtTableNames.Enum);
		private static UhtSpecifierTable SpecifierTable = UhtSpecifierTables.Instance.Get(UhtTableNames.Enum);

		public UhtEnumParser(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}

		#region Keywords
		[UhtKeyword(Extends = UhtTableNames.Global)]
		private static UhtParseResult UENUMKeyword(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token)
		{
			return ParseUEnum(TopScope, Token);
		}
		#endregion

		private static UhtParseResult ParseUEnum(UhtParsingScope ParentScope, UhtToken KeywordToken)
		{
			UhtEnumParser Enum = new UhtEnumParser(ParentScope.ScopeType, KeywordToken.InputLine);
			using (var TopScope = new UhtParsingScope(ParentScope, Enum, KeywordTable, UhtAccessSpecifier.Public))
			{
				const string ScopeName = "UENUM";

				using (var TokenContext = new UhtMessageContext(TopScope.TokenReader, ScopeName))
				{

					// Parse the specifiers
					UhtSpecifierContext SpecifierContext = new UhtSpecifierContext(TopScope, TopScope.TokenReader, Enum.MetaData);
					UhtSpecifierParser Specifiers = TopScope.HeaderParser.GetSpecifierParser(SpecifierContext, ScopeName, SpecifierTable);
					Specifiers.ParseSpecifiers();

					// Read the name and the CPP type
					switch (TopScope.TokenReader.TryOptional(new string[] { "namespace", "enum" }))
					{
						case 0: // namespace
							Enum.CppForm = UhtEnumCppForm.Namespaced;
							TopScope.TokenReader.SkipDeprecatedMacroIfNecessary();
							break;
						case 1: // enum
							Enum.CppForm = TopScope.TokenReader.TryOptional(new string[] { "class", "struct" }) >= 0 ? UhtEnumCppForm.EnumClass : UhtEnumCppForm.Regular;
							TopScope.TokenReader.SkipAlignasAndDeprecatedMacroIfNecessary();
							break;
						default:
							throw new UhtTokenException(TopScope.TokenReader, TopScope.TokenReader.PeekToken(), null);
					}

					UhtToken EnumToken = TopScope.TokenReader.GetIdentifier("enumeration name");

					Enum.SourceName = EnumToken.Value.ToString();

					Specifiers.ParseFieldMetaData(ScopeName);
					Specifiers.ParseDeferred();

					if (Enum.Outer != null)
					{
						Enum.Outer.AddChild(Enum);
					}

					if ((TopScope.HeaderParser.GetCurrentCompositeCompilerDirective() & UhtCompilerDirective.WithEditorOnlyData) != 0)
					{
						Enum.bIsEditorOnly = true;
					}

					// Read base for enum class
					if (Enum.CppForm == UhtEnumCppForm.EnumClass)
					{
						if (TopScope.TokenReader.TryOptional(':'))
						{
							UhtToken EnumType = TopScope.TokenReader.GetIdentifier("enumeration base");

							UhtEnumUnderlyingType UnderlyingType;
							if (!System.Enum.TryParse<UhtEnumUnderlyingType>(EnumType.Value.ToString(), out UnderlyingType) || UnderlyingType == UhtEnumUnderlyingType.Unspecified)
							{
								TopScope.TokenReader.LogError(EnumType.InputLine, $"Unsupported enum class base type '{EnumType.Value}'");
							}
							Enum.UnderlyingType = UnderlyingType;
						}
						else
						{
							Enum.UnderlyingType = UhtEnumUnderlyingType.Unspecified;
						}
					}
					else
					{
						if ((Enum.EnumFlags & EEnumFlags.Flags) != 0)
						{
							TopScope.TokenReader.LogError("The 'Flags' specifier can only be used on enum classes");
						}
					}

					if (Enum.UnderlyingType != UhtEnumUnderlyingType.uint8 && Enum.MetaData.ContainsKey("BlueprintType"))
					{
						TopScope.TokenReader.LogError("Invalid BlueprintType enum base - currently only uint8 supported");
					}

					//EnumDef.GetDefinitionRange().Start = &Input[InputPos];

					// Get the opening brace
					TopScope.TokenReader.Require('{');

					switch (Enum.CppForm)
					{
						case UhtEnumCppForm.Namespaced:
							// Now handle the inner true enum portion
							TopScope.TokenReader.Require("enum");
							TopScope.TokenReader.SkipAlignasAndDeprecatedMacroIfNecessary();

							UhtToken InnerEnumToken = TopScope.TokenReader.GetIdentifier("enumeration type name");

							TopScope.TokenReader.Require('{');
							Enum.CppType = $"{Enum.SourceName}::{InnerEnumToken.Value}";
							break;

						case UhtEnumCppForm.EnumClass:
						case UhtEnumCppForm.Regular:
							Enum.CppType = Enum.SourceName;
							break;
					}

					TokenContext.Reset($"UENUM {Enum.SourceName}");

					TopScope.AddModuleRelativePathToMetaData();
					TopScope.AddFormattedCommentsAsTooltipMetaData();

					// Parse all enum tags
					bool bHasUnparsedValue = false;
					long CurrentEnumValue = 0;

					TopScope.TokenReader.RequireList('}', ',', true, () =>
					{
						UhtToken TagToken = TopScope.TokenReader.GetIdentifier();

						StringView FullEnumName;
						switch (Enum.CppForm)
						{
							case UhtEnumCppForm.Namespaced:
							case UhtEnumCppForm.EnumClass:
								FullEnumName = new StringView($"{Enum.SourceName}::{TagToken.Value}");
								break;

							case UhtEnumCppForm.Regular:
								FullEnumName = TagToken.Value;
								break;

							default:
								throw new UhtIceException("Unexpected EEnumCppForm value");
						}

						// Save the new tag with a default value.  This will be replaced later
						int EnumIndex = Enum.AddEnumValue(TagToken.Value.ToString(), 0);
						TopScope.AddFormattedCommentsAsTooltipMetaData(EnumIndex);

						// Try to read an optional explicit enum value specification
						if (TopScope.TokenReader.TryOptional('='))
						{
							long ScatchValue;
							bool bParsedValue = TopScope.TokenReader.TryOptionalConstLong(out ScatchValue);
							if (bParsedValue)
							{
								CurrentEnumValue = ScatchValue;
							}
							else
							{
								// We didn't parse a literal, so set an invalid value
								CurrentEnumValue = -1;
								bHasUnparsedValue = true;
							}

							// Skip tokens until we encounter a comma, a closing brace or a UMETA declaration
							// There are tokens after the initializer so it's not a standalone literal,
							// so set it to an invalid value.
							int SkippedCount = TopScope.TokenReader.ConsumeUntil(new string[] { ",", "}", "UMETA" });
							if (SkippedCount == 0 && !bParsedValue)
							{
								throw new UhtTokenException(TopScope.TokenReader, TopScope.TokenReader.PeekToken(), "enumerator initializer");
							}
							if (SkippedCount > 0)
							{
								CurrentEnumValue = -1;
								bHasUnparsedValue = true;
							}
						}

						// Save the value and auto increment
						UhtEnumValue Value = Enum.EnumValues[EnumIndex];
						Value.Value = CurrentEnumValue;
						Enum.EnumValues[EnumIndex] = Value;
						if (CurrentEnumValue != -1)
						{
							++CurrentEnumValue;
						}

						Enum.MetaData.Add(UhtNames.Name, EnumIndex, Enum.EnumValues[EnumIndex].Name.ToString());

						// check for metadata on this enum value
						SpecifierContext.MetaNameIndex = EnumIndex;
						Specifiers.ParseFieldMetaData("Enum value");
					});

					// Trailing brace and semicolon for the enum
					TopScope.TokenReader.Require(';');
					if (Enum.CppForm == UhtEnumCppForm.Namespaced)
					{
						TopScope.TokenReader.Require('}');
					}

					if (!bHasUnparsedValue && !Enum.IsValidEnumValue(0) && Enum.MetaData.ContainsKey(UhtNames.BlueprintType))
					{
						Enum.LogWarning($"'{Enum.SourceName}' does not have a 0 entry! (This is a problem when the enum is initialized by default)");
					}
				}

				return UhtParseResult.Handled;
			}
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCompilerCommon.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HlslccDefinitions.h"
#include "HAL/FileManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, ShaderCompilerCommon);


int16 GetNumUniformBuffersUsed(const FShaderCompilerResourceTable& InSRT)
{
	auto CountLambda = [&](const TArray<uint32>& In)
					{
						int16 LastIndex = -1;
						for (int32 i = 0; i < In.Num(); ++i)
						{
							auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(In[i]);
							if (BufferIndex != static_cast<uint16>(FRHIResourceTableEntry::GetEndOfStreamToken()) )
							{
								LastIndex = FMath::Max(LastIndex, (int16)BufferIndex);
							}
						}

						return LastIndex + 1;
					};
	int16 Num = CountLambda(InSRT.SamplerMap);
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.ShaderResourceViewMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.TextureMap));
	Num = FMath::Max(Num, (int16)CountLambda(InSRT.UnorderedAccessViewMap));
	return Num;
}


void BuildResourceTableTokenStream(const TArray<uint32>& InResourceMap, int32 MaxBoundResourceTable, TArray<uint32>& OutTokenStream, bool bGenerateEmptyTokenStreamIfNoResources)
{
	if (bGenerateEmptyTokenStreamIfNoResources)
	{
		if (InResourceMap.Num() == 0)
		{
			return;
		}
	}

	// First we sort the resource map.
	TArray<uint32> SortedResourceMap = InResourceMap;
	SortedResourceMap.Sort();

	// The token stream begins with a table that contains offsets per bound uniform buffer.
	// This offset provides the start of the token stream.
	OutTokenStream.AddZeroed(MaxBoundResourceTable+1);
	auto LastBufferIndex = FRHIResourceTableEntry::GetEndOfStreamToken();
	for (int32 i = 0; i < SortedResourceMap.Num(); ++i)
	{
		auto BufferIndex = FRHIResourceTableEntry::GetUniformBufferIndex(SortedResourceMap[i]);
		if (BufferIndex != LastBufferIndex)
		{
			// Store the offset for resources from this buffer.
			OutTokenStream[BufferIndex] = OutTokenStream.Num();
			LastBufferIndex = BufferIndex;
		}
		OutTokenStream.Add(SortedResourceMap[i]);
	}

	// Add a token to mark the end of the stream. Not needed if there are no bound resources.
	if (OutTokenStream.Num())
	{
		OutTokenStream.Add(FRHIResourceTableEntry::GetEndOfStreamToken());
	}
}


bool BuildResourceTableMapping(
	const TMap<FString,FResourceTableEntry>& ResourceTableMap,
	const TMap<FString,uint32>& ResourceTableLayoutHashes,
	TBitArray<>& UsedUniformBufferSlots,
	FShaderParameterMap& ParameterMap,
	FShaderCompilerResourceTable& OutSRT)
{
	check(OutSRT.ResourceTableBits == 0);
	check(OutSRT.ResourceTableLayoutHashes.Num() == 0);

	// Build resource table mapping
	int32 MaxBoundResourceTable = -1;
	TArray<uint32> ResourceTableSRVs;
	TArray<uint32> ResourceTableSamplerStates;
	TArray<uint32> ResourceTableUAVs;

	// Go through ALL the members of ALL the UB resources
	for( auto MapIt = ResourceTableMap.CreateConstIterator(); MapIt; ++MapIt )
	{
		const FString& Name	= MapIt->Key;
		const FResourceTableEntry& Entry = MapIt->Value;

		uint16 BufferIndex, BaseIndex, Size;

		// If the shaders uses this member (eg View_PerlinNoise3DTexture)...
		if (ParameterMap.FindParameterAllocation( *Name, BufferIndex, BaseIndex, Size ) )
		{
			ParameterMap.RemoveParameterAllocation(*Name);

			uint16 UniformBufferIndex = INDEX_NONE;
			uint16 UBBaseIndex, UBSize;

			// Add the UB itself as a parameter if not there
			if (!ParameterMap.FindParameterAllocation(*Entry.UniformBufferName, UniformBufferIndex, UBBaseIndex, UBSize))
			{
				UniformBufferIndex = UsedUniformBufferSlots.FindAndSetFirstZeroBit();
				ParameterMap.AddParameterAllocation(*Entry.UniformBufferName,UniformBufferIndex,0,0,EShaderParameterType::UniformBuffer);
			}

			// Mark used UB index
			if (UniformBufferIndex >= sizeof(OutSRT.ResourceTableBits) * 8)
			{
				return false;
			}
			OutSRT.ResourceTableBits |= (1 << UniformBufferIndex);

			// How many resource tables max we'll use, and fill it with zeroes
			MaxBoundResourceTable = FMath::Max<int32>(MaxBoundResourceTable, (int32)UniformBufferIndex);

			auto ResourceMap = FRHIResourceTableEntry::Create(UniformBufferIndex, Entry.ResourceIndex, BaseIndex);
			switch( Entry.Type )
			{
			case UBMT_TEXTURE:
			case UBMT_RDG_TEXTURE:
				OutSRT.TextureMap.Add(ResourceMap);
				break;
			case UBMT_SAMPLER:
				OutSRT.SamplerMap.Add(ResourceMap);
				break;
			case UBMT_SRV:
			case UBMT_RDG_TEXTURE_SRV:
			case UBMT_RDG_BUFFER_SRV:
				OutSRT.ShaderResourceViewMap.Add(ResourceMap);
				break;
			case UBMT_UAV:
			case UBMT_RDG_TEXTURE_UAV:
			case UBMT_RDG_BUFFER_UAV:
				OutSRT.UnorderedAccessViewMap.Add(ResourceMap);
				break;
			default:
				return false;
			}
		}

		// We have to do this separately from the resource table member check above. We want to include the hash even
		// if the uniform buffer does not have any actual members used, because it will still be in the parameter map
		// and certain platforms (like DX12) will pessimise and require them.
		{
			uint16 UniformBufferIndex = INDEX_NONE;
			uint16 UBBaseIndex, UBSize;

			if (ParameterMap.FindParameterAllocation(*Entry.UniformBufferName, UniformBufferIndex, UBBaseIndex, UBSize))
			{
				while (OutSRT.ResourceTableLayoutHashes.Num() <= UniformBufferIndex)
				{
					OutSRT.ResourceTableLayoutHashes.Add(0);
				}

				OutSRT.ResourceTableLayoutHashes[UniformBufferIndex] = ResourceTableLayoutHashes.FindChecked(Entry.UniformBufferName);
			}
		}
	}

	OutSRT.MaxBoundResourceTable = MaxBoundResourceTable;
	return true;
}

const TCHAR* FindNextWhitespace(const TCHAR* StringPtr)
{
	while (*StringPtr && !FChar::IsWhitespace(*StringPtr))
	{
		StringPtr++;
	}

	if (*StringPtr && FChar::IsWhitespace(*StringPtr))
	{
		return StringPtr;
	}
	else
	{
		return nullptr;
	}
}

const TCHAR* FindNextNonWhitespace(const TCHAR* StringPtr)
{
	bool bFoundWhitespace = false;

	while (*StringPtr && (FChar::IsWhitespace(*StringPtr) || !bFoundWhitespace))
	{
		bFoundWhitespace = true;
		StringPtr++;
	}

	if (bFoundWhitespace && *StringPtr && !FChar::IsWhitespace(*StringPtr))
	{
		return StringPtr;
	}
	else
	{
		return nullptr;
	}
}

const TCHAR* FindMatchingClosingBrace(const TCHAR* OpeningBracePtr)
{
	const TCHAR* SearchPtr = OpeningBracePtr;
	int32 Depth = 0;

	while (*SearchPtr)
	{
		if (*SearchPtr == '{')
		{
			Depth++;
		}
		else if (*SearchPtr == '}')
		{
			if (Depth == 0)
			{
				return SearchPtr;
			}

			Depth--;
		}
		SearchPtr++;
	}

	return nullptr;
}

// See MSDN HLSL 'Symbol Name Restrictions' doc
inline bool IsValidHLSLIdentifierCharacter(TCHAR Char)
{
	return (Char >= 'a' && Char <= 'z') ||
		(Char >= 'A' && Char <= 'Z') ||
		(Char >= '0' && Char <= '9') ||
		Char == '_';
}

void ParseHLSLTypeName(const TCHAR* SearchString, const TCHAR*& TypeNameStartPtr, const TCHAR*& TypeNameEndPtr)
{
	TypeNameStartPtr = FindNextNonWhitespace(SearchString);
	check(TypeNameStartPtr);

	TypeNameEndPtr = TypeNameStartPtr;
	int32 Depth = 0;

	const TCHAR* NextWhitespace = FindNextWhitespace(TypeNameStartPtr);
	const TCHAR* PotentialExtraTypeInfoPtr = NextWhitespace ? FindNextNonWhitespace(NextWhitespace) : nullptr;

	// Find terminating whitespace, but skip over trailing ' < float4 >'
	while (*TypeNameEndPtr)
	{
		if (*TypeNameEndPtr == '<')
		{
			Depth++;
		}
		else if (*TypeNameEndPtr == '>')
		{
			Depth--;
		}
		else if (Depth == 0 
			&& FChar::IsWhitespace(*TypeNameEndPtr)
			// If we found a '<', we must not accept any whitespace before it
			&& (!PotentialExtraTypeInfoPtr || *PotentialExtraTypeInfoPtr != '<' || TypeNameEndPtr > PotentialExtraTypeInfoPtr))
		{
			break;
		}

		TypeNameEndPtr++;
	}

	check(TypeNameEndPtr);
}

const TCHAR* ParseHLSLSymbolName(const TCHAR* SearchString, FString& SymboName)
{
	const TCHAR* SymbolNameStartPtr = FindNextNonWhitespace(SearchString);
	check(SymbolNameStartPtr);

	const TCHAR* SymbolNameEndPtr = SymbolNameStartPtr;
	while (*SymbolNameEndPtr && IsValidHLSLIdentifierCharacter(*SymbolNameEndPtr))
	{
		SymbolNameEndPtr++;
	}

	SymboName = FString(SymbolNameEndPtr - SymbolNameStartPtr, SymbolNameStartPtr);

	return SymbolNameEndPtr;
}

class FUniformBufferMemberInfo
{
public:
	// eg View.WorldToClip
	FString NameAsStructMember;
	// eg View_WorldToClip
	FString GlobalName;
};

const TCHAR* ParseStructRecursive(
	const TCHAR* StructStartPtr,
	FString& UniformBufferName,
	int32 StructDepth,
	const FString& StructNamePrefix, 
	const FString& GlobalNamePrefix, 
	TMap<FString, TArray<FUniformBufferMemberInfo>>& UniformBufferNameToMembers)
{
	const TCHAR* OpeningBracePtr = FCString::Strstr(StructStartPtr, TEXT("{"));
	check(OpeningBracePtr);

	const TCHAR* ClosingBracePtr = FindMatchingClosingBrace(OpeningBracePtr + 1);
	check(ClosingBracePtr);

	FString StructName;
	const TCHAR* StructNameEndPtr = ParseHLSLSymbolName(ClosingBracePtr + 1, StructName);
	check(StructName.Len() > 0);

	FString NestedStructNamePrefix = StructNamePrefix + StructName + TEXT(".");
	FString NestedGlobalNamePrefix = GlobalNamePrefix + StructName + TEXT("_");

	if (StructDepth == 0)
	{
		UniformBufferName = StructName;
	}

	const TCHAR* LastMemberSemicolon = ClosingBracePtr;

	// Search backward to find the last member semicolon so we know when to stop parsing members
	while (LastMemberSemicolon > OpeningBracePtr && *LastMemberSemicolon != ';')
	{
		LastMemberSemicolon--;
	}

	const TCHAR* MemberSearchPtr = OpeningBracePtr + 1;

	do
	{
		const TCHAR* MemberTypeStartPtr = nullptr;
		const TCHAR* MemberTypeEndPtr = nullptr;
		ParseHLSLTypeName(MemberSearchPtr, MemberTypeStartPtr, MemberTypeEndPtr);
		FString MemberTypeName(MemberTypeEndPtr - MemberTypeStartPtr, MemberTypeStartPtr);

		if (FCString::Strcmp(*MemberTypeName, TEXT("struct")) == 0)
		{
			MemberSearchPtr = ParseStructRecursive(MemberTypeStartPtr, UniformBufferName, StructDepth + 1, NestedStructNamePrefix, NestedGlobalNamePrefix, UniformBufferNameToMembers);
		}
		else
		{
			FString MemberName;
			const TCHAR* SymbolEndPtr = ParseHLSLSymbolName(MemberTypeEndPtr, MemberName);
			check(MemberName.Len() > 0);
			
			MemberSearchPtr = SymbolEndPtr;

			// Skip over trailing tokens '[1];'
			while (*MemberSearchPtr && *MemberSearchPtr != ';')
			{
				MemberSearchPtr++;
			}

			// Add this member to the map
			TArray<FUniformBufferMemberInfo>& UniformBufferMembers = UniformBufferNameToMembers.FindOrAdd(UniformBufferName);

			FUniformBufferMemberInfo NewMemberInfo;
			NewMemberInfo.NameAsStructMember = NestedStructNamePrefix + MemberName;
			NewMemberInfo.GlobalName = NestedGlobalNamePrefix + MemberName;
			UniformBufferMembers.Add(MoveTemp(NewMemberInfo));
		}
	} 
	while (MemberSearchPtr < LastMemberSemicolon);

	const TCHAR* StructEndPtr = StructNameEndPtr;

	// Skip over trailing tokens '[1];'
	while (*StructEndPtr && *StructEndPtr != ';')
	{
		StructEndPtr++;
	}

	return StructEndPtr;
}

bool MatchStructMemberName(const FString& SymbolName, const TCHAR* SearchPtr, const FString& PreprocessedShaderSource)
{
	// Only match whole symbol
	if (IsValidHLSLIdentifierCharacter(*(SearchPtr - 1)) || *(SearchPtr - 1) == '.')
	{
		return false;
	}

	for (int32 i = 0; i < SymbolName.Len(); i++)
	{
		if (*SearchPtr != SymbolName[i])
		{
			return false;
		}
		
		SearchPtr++;

		if (i < SymbolName.Len() - 1)
		{
			// Skip whitespace within the struct member reference before the end
			// eg 'View. ViewToClip'
			while (FChar::IsWhitespace(*SearchPtr))
			{
				SearchPtr++;
			}
		}
	}

	// Only match whole symbol
	if (IsValidHLSLIdentifierCharacter(*SearchPtr))
	{
		return false;
	}

	return true;
}

// Searches string SearchPtr for 'SearchString.' or 'SearchString .' and returns a pointer to the first character of the match.
TCHAR* FindNextUniformBufferReference(TCHAR* SearchPtr, const TCHAR* SearchString, uint32 SearchStringLength)
{
	TCHAR* FoundPtr = FCString::Strstr(SearchPtr, SearchString);
	
	while(FoundPtr)
	{
		if (FoundPtr == nullptr)
		{
			return nullptr;
		}
		else if (FoundPtr[SearchStringLength] == '.' || (FoundPtr[SearchStringLength] == ' ' && FoundPtr[SearchStringLength+1] == '.'))
		{
			return FoundPtr;
		}
		
		FoundPtr = FCString::Strstr(FoundPtr + SearchStringLength, SearchString);
	}
	
	return nullptr;
}

bool FShaderParameterParser::ParseAndMoveShaderParametersToRootConstantBuffer(
	const FShaderCompilerInput& CompilerInput,
	FShaderCompilerOutput& CompilerOutput,
	FString& PreprocessedShaderSource,
	const TCHAR* ConstantBufferType)
{
	// The shader doesn't have any parameter binding through shader structure, therefore don't do anything.
	if (CompilerInput.RootParameterBindings.Num() == 0)
	{
		return true;
	}

	const bool bMoveToRootConstantBuffer = ConstantBufferType != nullptr;
	OriginalParsedShader = PreprocessedShaderSource;
	ParsedParameters.Reserve(CompilerInput.RootParameterBindings.Num());

	// Prepare the set of parameter to look for during parsing.
	for (const FShaderCompilerInput::FRootParameterBinding& Member : CompilerInput.RootParameterBindings)
	{
		ParsedParameters.Add(Member.Name, FParsedShaderParameter());
	}

	bool bSuccess = true;

	// Browse the code for global shader parameter, Save their type and erase them white spaces.
	{
		enum class EState
		{
			// When to look for something to scan.
			Scanning,

			// When going to next ; in the global scope and reset.
			GoToNextSemicolonAndReset,

			// Parsing what might be a type of the parameter.
			ParsingPotentialType,
			FinishedPotentialType,

			// Parsing what might be a name of the parameter.
			ParsingPotentialName,
			FinishedPotentialName,

			// Parsing what looks like array of the parameter.
			ParsingPotentialArraySize,
			FinishedArraySize,

			// Found a parameter, just finish to it's semi colon.
			FoundParameter,
		};

		const int32 ShaderSourceLen = PreprocessedShaderSource.Len();

		int32 CurrentPragamLineoffset = -1;
		int32 CurrentLineoffset = 0;

		int32 TypeStartPos = -1;
		int32 TypeEndPos = -1;
		int32 NameStartPos = -1;
		int32 NameEndPos = -1;
		int32 ScopeIndent = 0;

		EState State = EState::Scanning;
		bool bGoToNextLine = false;

		auto ResetState = [&]()
		{
			TypeStartPos = -1;
			TypeEndPos = -1;
			NameStartPos = -1;
			NameEndPos = -1;
			State = EState::Scanning;
		};

		auto EmitError = [&](const FString& ErrorMessage)
		{
			FShaderCompilerError Error;
			Error.StrippedErrorMessage = ErrorMessage;
			ExtractFileAndLine(CurrentPragamLineoffset, CurrentLineoffset, Error.ErrorVirtualFilePath, Error.ErrorLineString);
			CompilerOutput.Errors.Add(Error);
			bSuccess = false;
		};

		auto EmitUnpextectedHLSLSyntaxError = [&]()
		{
			EmitError(TEXT("Unexpected syntax when parsing shader parameters from shader code."));
			State = EState::GoToNextSemicolonAndReset;
		};

		for (int32 Cursor = 0; Cursor < ShaderSourceLen; Cursor++)
		{
			const TCHAR Char = PreprocessedShaderSource[Cursor];

			auto FoundShaderParameter = [&]()
			{
				check(Char == ';');
				check(TypeStartPos != -1);
				check(TypeEndPos != -1);
				check(NameStartPos != -1);
				check(NameEndPos != -1);

				FString Type = PreprocessedShaderSource.Mid(TypeStartPos, TypeEndPos - TypeStartPos + 1);
				FString Name = PreprocessedShaderSource.Mid(NameStartPos, NameEndPos - NameStartPos + 1);

				if (ParsedParameters.Contains(Name))
				{
					if (ParsedParameters.FindChecked(Name).IsFound())
					{
						// If it has already been found, it means it is duplicated. Do nothing and let the shader compiler throw the error.
					}
					else
					{
						FParsedShaderParameter ParsedParameter;
						ParsedParameter.Type = Type;
						ParsedParameter.PragamLineoffset = CurrentPragamLineoffset;
						ParsedParameter.LineOffset = CurrentLineoffset;
						ParsedParameters[Name] = ParsedParameter;

						// Erases this shader parameter conserving the same line numbers.
						if (bMoveToRootConstantBuffer)
						{
							for (int32 j = TypeStartPos; j <= Cursor; j++)
							{
								if (PreprocessedShaderSource[j] != '\r' && PreprocessedShaderSource[j] != '\n')
									PreprocessedShaderSource[j] = ' ';
							}
						}
					}
				}

				ResetState();
			};

			const bool bIsWhiteSpace = Char == ' ' || Char == '\t' || Char == '\r' || Char == '\n';
			const bool bIsLetter = (Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z');
			const bool bIsNumber = Char >= '0' && Char <= '9';

			const TCHAR* UpComing = (*PreprocessedShaderSource) + Cursor;
			const int32 RemainingSize = ShaderSourceLen - Cursor;

			CurrentLineoffset += Char == '\n';

			// Go to the next line if this is a preprocessor macro.
			if (bGoToNextLine)
			{
				if (Char == '\n')
				{
					bGoToNextLine = false;
				}
				continue;
			}
			else if (Char == '#')
			{
				if (RemainingSize > 6 && FCString::Strncmp(UpComing, TEXT("#line "), 6) == 0)
				{
					CurrentPragamLineoffset = Cursor;
					CurrentLineoffset = -1; // that will be incremented to 0 when reaching the \n at the end of the #line
				}

				bGoToNextLine = true;
				continue;
			}

			// If within a scope, just carry on until outside the scope.
			if (ScopeIndent > 0 || Char == '{')
			{
				if (Char == '{')
				{
					ScopeIndent++;
				}
				else if (Char == '}')
				{
					ScopeIndent--;
					if (ScopeIndent == 0)
					{
						ResetState();
					}
				}
				continue;
			}

			if (State == EState::Scanning)
			{
				if (bIsLetter)
				{
					static const TCHAR* KeywordTable[] = {
						TEXT("enum"),
						TEXT("class"),
						TEXT("const"),
						TEXT("struct"),
						TEXT("static"),
					};
					static int32 KeywordTableSize[] = {4, 5, 5, 6, 6};

					int32 RecognisedKeywordId = -1;
					for (int32 KeywordId = 0; KeywordId < UE_ARRAY_COUNT(KeywordTable); KeywordId++)
					{
						const TCHAR* Keyword = KeywordTable[KeywordId];
						const int32 KeywordSize = KeywordTableSize[KeywordId];

						if (RemainingSize > KeywordSize)
						{
							TCHAR KeywordEndTestChar = UpComing[KeywordSize];

							if ((KeywordEndTestChar == ' ' || KeywordEndTestChar == '\r' || KeywordEndTestChar == '\n' || KeywordEndTestChar == '\t') &&
								FCString::Strncmp(UpComing, Keyword, KeywordSize) == 0)
							{
								RecognisedKeywordId = KeywordId;
								break;
							}
						}
					}

					if (RecognisedKeywordId == -1)
					{
						// Might have found beginning of the type of a parameter.
						State = EState::ParsingPotentialType;
						TypeStartPos = Cursor;
					}
					else if (RecognisedKeywordId == 2)
					{
						// Ignore the const keywords, but still parse given it might still be a shader parameter.
						Cursor += KeywordTableSize[RecognisedKeywordId];
					}
					else
					{
						// Purposefully ignore enum, class, struct, static
						State = EState::GoToNextSemicolonAndReset;
					}
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else if (Char == ';')
				{
					// Looks like redundant semicolon, just ignore and keep scanning.
				}
				else
				{
					// No idea what this is, just go to next semi colon.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::GoToNextSemicolonAndReset)
			{
				// If need to go to next global semicolon and reach it. Resume browsing.
				if (Char == ';')
				{
					ResetState();
				}
			}
			else if (State == EState::ParsingPotentialType)
			{
				// Found character legal for a type...
				if (bIsLetter ||
					bIsNumber ||
					Char == '<' || Char == '>' || Char == '_')
				{
					// Keep browsing what might be type of the parameter.
				}
				else if (bIsWhiteSpace)
				{
					// Might have found a type.
					State = EState::FinishedPotentialType;
					TypeEndPos = Cursor - 1;
				}
				else
				{
					// Found unexpected character in the type.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::FinishedPotentialType)
			{
				if (bIsLetter)
				{
					// Might have found beginning of the name of a parameter.
					State = EState::ParsingPotentialName;
					NameStartPos = Cursor;
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else
				{
					// No idea what this is, just go to next semi colon.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::ParsingPotentialName)
			{
				// Found character legal for a name...
				if (bIsLetter ||
					bIsNumber ||
					Char == '_')
				{
					// Keep browsing what might be name of the parameter.
				}
				else if (Char == ':' || Char == '=')
				{
					// Found a parameter with syntax:
					// uint MyParameter : <whatever>;
					// uint MyParameter = <DefaultValue>;
					NameEndPos = Cursor - 1;
					State = EState::FoundParameter;
				}
				else if (Char == ';')
				{
					// Found a parameter with syntax:
					// uint MyParameter;
					NameEndPos = Cursor - 1;
					FoundShaderParameter();
				}
				else if (Char == '[')
				{
					// Syntax:
					//  uint MyArray[
					NameEndPos = Cursor - 1;
					State = EState::ParsingPotentialArraySize;
				}
				else if (bIsWhiteSpace)
				{
					// Might have found a name.
					// uint MyParameter <Still need to know what is after>;
					NameEndPos = Cursor - 1;
					State = EState::FinishedPotentialName;
				}
				else
				{
					// Found unexpected character in the name.
					// syntax:
					// uint MyFunction(<Don't care what is after>
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (
				State == EState::FinishedPotentialName ||
				State == EState::FinishedArraySize)
			{
				if (Char == ';')
				{
					// Found a parameter with syntax:
					// uint MyParameter <a bit of OK stuf>;
					FoundShaderParameter();
				}
				else if (Char == ':')
				{
					// Found a parameter with syntax:
					// uint MyParameter <a bit of OK stuf> : <Ignore all this crap>;
					State = EState::FoundParameter;
				}
				else if (Char == '=')
				{
					// Found syntax that doesn't make any sens:
					// uint MyParameter <a bit of OK stuf> = <Ignore all this crap>;
					State = EState::FoundParameter;
					// TDOO: should error out that this is useless.
				}
				else if (Char == '[')
				{
					if (State == EState::FinishedPotentialName)
					{
						// Syntax:
						//  uint MyArray [
						State = EState::ParsingPotentialArraySize;
					}
					else
					{
						EmitError(TEXT("Shader parameters can only support one dimensional array"));
					}
				}
				else if (bIsWhiteSpace)
				{
					// Keep parsing void.
				}
				else
				{
					// Found unexpected stuff.
					State = EState::GoToNextSemicolonAndReset;
				}
			}
			else if (State == EState::ParsingPotentialArraySize)
			{
				if (Char == ']')
				{
					State = EState::FinishedArraySize;
				}
				else if (Char == ';')
				{
					EmitUnpextectedHLSLSyntaxError();
				}
				else
				{
					// Keep going through the array size that might be a complex expression.
				}
			}
			else if (State == EState::FoundParameter)
			{
				if (Char == ';')
				{
					FoundShaderParameter();
				}
				else
				{
					// Cary on skipping all crap we don't care about shader parameter until we find it's semi colon.
				}
			}
			else
			{
				unimplemented();
			}
		} // for (int32 Cursor = 0; Cursor < PreprocessedShaderSource.Len(); Cursor++)
	}

	// Generate the root cbuffer content.
	if (bMoveToRootConstantBuffer)
	{
		FString RootCBufferContent;
		for (const auto& Member : CompilerInput.RootParameterBindings)
		{
			const FParsedShaderParameter& ParsedParameter = ParsedParameters[Member.Name];
			if (!ParsedParameter.IsFound())
			{
				continue;
			}

			FString HLSLOffset;
			{
				int32 ByteOffset = int32(Member.ByteOffset);
				HLSLOffset = FString::FromInt(ByteOffset / 16);
			
				switch (ByteOffset % 16)
				{
				case 0:
					break;
				case 4:
					HLSLOffset.Append(TEXT(".y"));
					break;
				case 8:
					HLSLOffset.Append(TEXT(".z"));
					break;
				case 12:
					HLSLOffset.Append(TEXT(".w"));
					break;
				}
			}

			RootCBufferContent.Append(FString::Printf(
				TEXT("%s %s : packoffset(c%s);\r\n"),
				*ParsedParameter.Type,
				*Member.Name,
				*HLSLOffset));
		}

		FString NewShaderCode = FString::Printf(
			TEXT("%s %s\r\n")
			TEXT("{\r\n")
			TEXT("%s")
			TEXT("}\r\n\r\n%s"),
			ConstantBufferType,
			FShaderParametersMetadata::kRootUniformBufferBindingName,
			*RootCBufferContent,
			*PreprocessedShaderSource);

		PreprocessedShaderSource = MoveTemp(NewShaderCode);
	}

	return bSuccess;
}

void FShaderParameterParser::ValidateShaderParameterTypes(
	const FShaderCompilerInput& CompilerInput,
	FShaderCompilerOutput& CompilerOutput) const
{
	// The shader doesn't have any parameter binding through shader structure, therefore don't do anything.
	if (CompilerInput.RootParameterBindings.Num() == 0)
	{
		return;
	}

	if (!CompilerOutput.bSucceeded)
	{
		return;
	}

	const TMap<FString, FParameterAllocation>& ParametersFoundByCompiler = CompilerOutput.ParameterMap.GetParameterMap();

	bool bSuccess = true;
	for (const FShaderCompilerInput::FRootParameterBinding& Member : CompilerInput.RootParameterBindings)
	{
		const FParsedShaderParameter& ParsedParameter = ParsedParameters[Member.Name];

		// Did not find shader parameter in code.
		if (!ParsedParameter.IsFound())
		{
			// Verify the shader compiler also did not find this parameter to make sure there is no bug in the parser.
			checkf(
				!ParametersFoundByCompiler.Contains(Member.Name),
				TEXT("Looks like there is a bug in FShaderParameterParser ParameterName=%s DumpDebugInfoPath=%s"),
				*Member.Name,
				*CompilerInput.DumpDebugInfoPath);
			continue;
		}

		const bool bShouldBeInt = Member.ExpectedShaderType.StartsWith(TEXT("int"));
		const bool bShouldBeUint = Member.ExpectedShaderType.StartsWith(TEXT("uint"));

		bool bIsTypeCorrect = ParsedParameter.Type == Member.ExpectedShaderType;

		// Allow silent casting between signed and unsigned on shader bindings.
		if (!bIsTypeCorrect && (bShouldBeInt || bShouldBeUint))
		{
			FString NewExpectedShaderType;
			if (bShouldBeInt)
			{
				// tries up with an uint.
				NewExpectedShaderType = TEXT("u") + Member.ExpectedShaderType;
			}
			else
			{
				// tries up with an int.
				NewExpectedShaderType = Member.ExpectedShaderType;
				NewExpectedShaderType.RemoveAt(0);
			}

			bIsTypeCorrect = ParsedParameter.Type == NewExpectedShaderType;
		}

		if (!bIsTypeCorrect)
		{
			FShaderCompilerError Error;
			Error.StrippedErrorMessage = FString::Printf(
				TEXT("Type %s of shader parameter %s in shader mismatch the shader parameter structure: it expects a %s"),
				*ParsedParameter.Type,
				*Member.Name,
				*Member.ExpectedShaderType);
			ExtractFileAndLine(ParsedParameter.PragamLineoffset, ParsedParameter.LineOffset, Error.ErrorVirtualFilePath, Error.ErrorLineString);

			CompilerOutput.Errors.Add(Error);
			bSuccess = false;
		}
	} // for (const auto& Member : CompilerInput.RootParameterBindings)

	CompilerOutput.bSucceeded = bSuccess;
}

void FShaderParameterParser::ExtractFileAndLine(int32 PragamLineoffset, int32 LineOffset, FString& OutFile, FString& OutLine) const
{
	if (PragamLineoffset == -1)
	{
		return;
	}

	check(FCString::Strncmp((*OriginalParsedShader) + PragamLineoffset, TEXT("#line "), 6) == 0);

	const int32 ShaderSourceLen = OriginalParsedShader.Len();

	int32 StartFilePos = -1;
	int32 EndFilePos = -1;
	int32 StartLinePos = PragamLineoffset + 6;
	int32 EndLinePos = -1;

	for (int32 Cursor = StartLinePos; Cursor < ShaderSourceLen; Cursor++)
	{
		const TCHAR Char = OriginalParsedShader[Cursor];

		if (Char == '\n')
		{
			break;
		}

		if (EndLinePos == -1)
		{
			if (Char > '9' || Char < '0')
			{
				EndLinePos = Cursor - 1;
			}
		}
		else if (StartFilePos == -1)
		{
			if (Char == '"')
			{
				StartFilePos = Cursor + 1;
			}
		}
		else if (EndFilePos == -1)
		{
			if (Char == '"')
			{
				EndFilePos = Cursor - 1;
				break;
			}
		}
	}

	check(StartFilePos != -1);
	check(EndFilePos != -1);
	check(EndLinePos != -1);

	OutFile = OriginalParsedShader.Mid(StartFilePos, EndFilePos - StartFilePos + 1);
	FString LineBasis = OriginalParsedShader.Mid(StartLinePos, EndLinePos - StartLinePos + 1);

	int32 FinalLine = FCString::Atoi(*LineBasis) + LineOffset;
	OutLine = FString::FromInt(FinalLine);
}


// The cross compiler doesn't yet support struct initializers needed to construct static structs for uniform buffers
// Replace all uniform buffer struct member references (View.WorldToClip) with a flattened name that removes the struct dependency (View_WorldToClip)
void RemoveUniformBuffersFromSource(const FShaderCompilerEnvironment& Environment, FString& PreprocessedShaderSource)
{
	TMap<FString, TArray<FUniformBufferMemberInfo>> UniformBufferNameToMembers;
	UniformBufferNameToMembers.Reserve(Environment.ResourceTableLayoutHashes.Num());

	// Build a mapping from uniform buffer name to its members
	{
		const TCHAR* UniformBufferStructIdentifier = TEXT("static const struct");
		const int32 StructPrefixLen = FCString::Strlen(TEXT("static const "));
		const int32 StructIdentifierLen = FCString::Strlen(UniformBufferStructIdentifier);
		TCHAR* SearchPtr = FCString::Strstr(&PreprocessedShaderSource[0], UniformBufferStructIdentifier);

		while (SearchPtr)
		{
			FString UniformBufferName;
			const TCHAR* ConstStructEndPtr = ParseStructRecursive(SearchPtr + StructPrefixLen, UniformBufferName, 0, TEXT(""), TEXT(""), UniformBufferNameToMembers);
			TCHAR* StructEndPtr = &PreprocessedShaderSource[ConstStructEndPtr - &PreprocessedShaderSource[0]];

			// Comment out the uniform buffer struct and initializer
			*SearchPtr = '/';
			*(SearchPtr + 1) = '*';
			*(StructEndPtr - 1) = '*';
			*StructEndPtr = '/';

			SearchPtr = FCString::Strstr(StructEndPtr, UniformBufferStructIdentifier);
		}
	}

	// Replace all uniform buffer struct member references (View.WorldToClip) with a flattened name that removes the struct dependency (View_WorldToClip)
	for (TMap<FString, TArray<FUniformBufferMemberInfo>>::TConstIterator It(UniformBufferNameToMembers); It; ++It)
	{
		const FString& UniformBufferName = It.Key();
		FString UniformBufferAccessString = UniformBufferName + TEXT(".");
		// MCPP inserts spaces after defines
		FString UniformBufferAccessStringWithSpace = UniformBufferName + TEXT(" .");

		// Search for the uniform buffer name first, as an optimization (instead of searching the entire source for every member)
		TCHAR* SearchPtr = FindNextUniformBufferReference(&PreprocessedShaderSource[0], *UniformBufferName, UniformBufferName.Len());

		while (SearchPtr)
		{
			const TArray<FUniformBufferMemberInfo>& UniformBufferMembers = It.Value();

			// Find the matching member we are replacing
			for (int32 MemberIndex = 0; MemberIndex < UniformBufferMembers.Num(); MemberIndex++)
			{
				const FString& MemberNameAsStructMember = UniformBufferMembers[MemberIndex].NameAsStructMember;

				if (MatchStructMemberName(MemberNameAsStructMember, SearchPtr, PreprocessedShaderSource))
				{
					const FString& MemberNameGlobal = UniformBufferMembers[MemberIndex].GlobalName;
					int32 NumWhitespacesToAdd = 0;

					for (int32 i = 0; i < MemberNameAsStructMember.Len(); i++)
					{
						if (i < MemberNameAsStructMember.Len() - 1)
						{
							if (FChar::IsWhitespace(SearchPtr[i]))
							{
								NumWhitespacesToAdd++;
							}
						}

						SearchPtr[i] = MemberNameGlobal[i];
					}

					// MCPP inserts spaces after defines
					// #define ReflectionStruct OpaqueBasePass.Shared.Reflection
					// 'ReflectionStruct.SkyLightCubemapBrightness' becomes 'OpaqueBasePass.Shared.Reflection .SkyLightCubemapBrightness' after MCPP
					// In order to convert this struct member reference into a globally unique variable we move the spaces to the end
					// 'OpaqueBasePass.Shared.Reflection .SkyLightCubemapBrightness' -> 'OpaqueBasePass_Shared_Reflection_SkyLightCubemapBrightness '
					for (int32 i = 0; i < NumWhitespacesToAdd; i++)
					{
						// If we passed MatchStructMemberName, it should not be possible to overwrite the null terminator
						check(SearchPtr[MemberNameAsStructMember.Len() + i] != 0);
						SearchPtr[MemberNameAsStructMember.Len() + i] = ' ';
					}
							
					break;
				}
			}

			SearchPtr = FindNextUniformBufferReference(SearchPtr + UniformBufferAccessString.Len(), *UniformBufferName, UniformBufferName.Len());
		}
	}
}

FString CreateShaderCompilerWorkerDirectCommandLine(const FShaderCompilerInput& Input, uint32 CCFlags)
{
	FString Text(TEXT("-directcompile -format="));
	Text += Input.ShaderFormat.GetPlainNameString();
	Text += TEXT(" -entry=");
	Text += Input.EntryPointName;
	switch (Input.Target.Frequency)
	{
	case SF_Vertex:		Text += TEXT(" -vs"); break;
	case SF_Hull:		Text += TEXT(" -hs"); break;
	case SF_Domain:		Text += TEXT(" -ds"); break;
	case SF_Geometry:	Text += TEXT(" -gs"); break;
	case SF_Pixel:		Text += TEXT(" -ps"); break;
	case SF_Compute:	Text += TEXT(" -cs"); break;
#if RHI_RAYTRACING
	case SF_RayGen:			Text += TEXT(" -rgs"); break;
	case SF_RayMiss:		Text += TEXT(" -rms"); break;
	case SF_RayHitGroup:	Text += TEXT(" -rhs"); break;
	case SF_RayCallable:	Text += TEXT(" -rcs"); break;
#endif // RHI_RAYTRACING
	default: break;
	}
	if (Input.bCompilingForShaderPipeline)
	{
		Text += TEXT(" -pipeline");
	}
	if (Input.bIncludeUsedOutputs)
	{
		Text += TEXT(" -usedoutputs=");
		for (int32 Index = 0; Index < Input.UsedOutputs.Num(); ++Index)
		{
			if (Index != 0)
			{
				Text += TEXT("+");
			}
			Text += Input.UsedOutputs[Index];
		}
	}

	Text += TEXT(" ");
	Text += Input.DumpDebugInfoPath / Input.GetSourceFilename();

	uint64 CFlags = 0;
	for (int32 Index = 0; Index < Input.Environment.CompilerFlags.Num(); ++Index)
	{
		CFlags = CFlags | ((uint64)1 << (uint64)Input.Environment.CompilerFlags[Index]);
	}
	if (CFlags)
	{
		Text += TEXT(" -cflags=");
		Text += FString::Printf(TEXT("%llu"), CFlags);
	}
	if (CCFlags)
	{
		Text += TEXT(" -hlslccflags=");
		Text += FString::Printf(TEXT("%llu"), CCFlags);
	}
	// When we're running in directcompile mode, we don't to spam the crash reporter
	Text += TEXT(" -nocrashreports");
	return Text;
}

static int Mali_ExtractNumberInstructions(const FString &MaliOutput)
{
	int ReturnedNum = 0;

	// Parse the instruction count
	const int32 InstructionStringLength = FPlatformString::Strlen(TEXT("Instructions Emitted:"));
	const int32 InstructionsIndex = MaliOutput.Find(TEXT("Instructions Emitted:"));

	if (InstructionsIndex != INDEX_NONE && InstructionsIndex + InstructionStringLength < MaliOutput.Len())
	{
		const int32 EndIndex = MaliOutput.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, InstructionsIndex + InstructionStringLength);

		if (EndIndex != INDEX_NONE)
		{
			int32 StartIndex = InstructionsIndex + InstructionStringLength;

			bool bFoundNrStart = false;
			int32 NumberIndex = 0;

			while (StartIndex < EndIndex)
			{
				if (FChar::IsDigit(MaliOutput[StartIndex]) && !bFoundNrStart)
				{
					// found number's beginning
					bFoundNrStart = true;
					NumberIndex = StartIndex;
				}
				else if (FChar::IsWhitespace(MaliOutput[StartIndex]) && bFoundNrStart)
				{
					// found number's end
					bFoundNrStart = false;
					const FString NumberString = MaliOutput.Mid(NumberIndex, StartIndex - NumberIndex);
					const float fNrInstructions = FCString::Atof(*NumberString);
					ReturnedNum += ceil(fNrInstructions);
				}

				++StartIndex;
			}
		}
	}

	return ReturnedNum;
}

static FString Mali_ExtractErrors(const FString &MaliOutput)
{
	FString ReturnedErrors;

	const int32 GlobalErrorIndex = MaliOutput.Find(TEXT("Compilation failed."));

	// find each 'line' that begins with token "ERROR:" and copy it to the returned string
	if (GlobalErrorIndex != INDEX_NONE)
	{
		int32 CompilationErrorIndex = MaliOutput.Find(TEXT("ERROR:"));
		while (CompilationErrorIndex != INDEX_NONE)
		{
			int32 EndLineIndex = MaliOutput.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CompilationErrorIndex + 1);
			EndLineIndex = EndLineIndex == INDEX_NONE ? MaliOutput.Len() - 1 : EndLineIndex;

			ReturnedErrors += MaliOutput.Mid(CompilationErrorIndex, EndLineIndex - CompilationErrorIndex + 1);

			CompilationErrorIndex = MaliOutput.Find(TEXT("ERROR:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EndLineIndex);
		}
	}

	return ReturnedErrors;
}

void CompileOfflineMali(const FShaderCompilerInput& Input, FShaderCompilerOutput& ShaderOutput, const ANSICHAR* ShaderSource, const int32 SourceSize, bool bVulkanSpirV)
{
	const bool bCompilerExecutableExists = FPaths::FileExists(Input.ExtraSettings.OfflineCompilerPath);

	if (bCompilerExecutableExists)
	{
		const auto Frequency = (EShaderFrequency)Input.Target.Frequency;
		const FString WorkingDir(FPlatformProcess::ShaderDir());

		FString CompilerPath = Input.ExtraSettings.OfflineCompilerPath;

		FString CompilerCommand = "";

		// add process and thread ids to the file name to avoid collision between workers
		auto ProcID = FPlatformProcess::GetCurrentProcessId();
		auto ThreadID = FPlatformTLS::GetCurrentThreadId();
		FString GLSLSourceFile = WorkingDir / TEXT("GLSLSource#") + FString::FromInt(ProcID) + TEXT("#") + FString::FromInt(ThreadID);

		// setup compilation arguments
		TCHAR *FileExt = nullptr;
		switch (Frequency)
		{
			case SF_Vertex:
				GLSLSourceFile += TEXT(".vert");
				CompilerCommand += TEXT(" -v");
			break;
			case SF_Pixel:
				GLSLSourceFile += TEXT(".frag");
				CompilerCommand += TEXT(" -f");
			break;
			case SF_Geometry:
				GLSLSourceFile += TEXT(".geom");
				CompilerCommand += TEXT(" -g");
			break;
			case SF_Hull:
				GLSLSourceFile += TEXT(".tesc");
				CompilerCommand += TEXT(" -t");
			break;
			case SF_Domain:
				GLSLSourceFile += TEXT(".tese");
				CompilerCommand += TEXT(" -e");
			break;
			case SF_Compute:
				GLSLSourceFile += TEXT(".comp");
				CompilerCommand += TEXT(" -C");
			break;

			default:
				GLSLSourceFile += TEXT(".shd");
			break;
		}

		if (bVulkanSpirV)
		{
			CompilerCommand += TEXT(" -p");
		}
		else
		{
			CompilerCommand += TEXT(" -s");
		}

		FArchive* Ar = IFileManager::Get().CreateFileWriter(*GLSLSourceFile, FILEWRITE_EvenIfReadOnly);

		if (Ar == nullptr)
		{
			return;
		}

		// write out the shader source to a file and use it below as input for the compiler
		Ar->Serialize((void*)ShaderSource, SourceSize);
		delete Ar;

		FString StdOut;
		FString StdErr;
		int32 ReturnCode = 0;

		// Since v6.2.0, Mali compiler needs to be started in the executable folder or it won't find "external/glslangValidator" for Vulkan
		FString CompilerWorkingDirectory = FPaths::GetPath(CompilerPath);

		if (!CompilerWorkingDirectory.IsEmpty() && FPaths::DirectoryExists(CompilerWorkingDirectory))
		{
			// compiler command line contains flags and the GLSL source file name
			CompilerCommand += " " + FPaths::ConvertRelativePathToFull(GLSLSourceFile);

			// Run Mali shader compiler and wait for completion
			FPlatformProcess::ExecProcess(*CompilerPath, *CompilerCommand, &ReturnCode, &StdOut, &StdErr, *CompilerWorkingDirectory);
		}
		else
		{
			StdErr = "Couldn't find Mali offline compiler at " + CompilerPath;
		}

		// parse Mali's output and extract instruction count or eventual errors
		ShaderOutput.bSucceeded = (ReturnCode >= 0);
		if (ShaderOutput.bSucceeded)
		{
			// check for errors
			if (StdErr.Len())
			{
				ShaderOutput.bSucceeded = false;

				FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
				NewError->StrippedErrorMessage = TEXT("[Mali Offline Complier]\n") + StdErr;
			}
			else
			{
				FString Errors = Mali_ExtractErrors(StdOut);

				if (Errors.Len())
				{
					FShaderCompilerError* NewError = new(ShaderOutput.Errors) FShaderCompilerError();
					NewError->StrippedErrorMessage = TEXT("[Mali Offline Complier]\n") + Errors;
					ShaderOutput.bSucceeded = false;
				}
			}

			// extract instruction count
			if (ShaderOutput.bSucceeded)
			{
				ShaderOutput.NumInstructions = Mali_ExtractNumberInstructions(StdOut);
			}
		}

		// we're done so delete the shader file
		IFileManager::Get().Delete(*GLSLSourceFile, true, true);
	}
}

namespace CrossCompiler
{
	FString CreateResourceTableFromEnvironment(const FShaderCompilerEnvironment& Environment)
	{
		FString Line = TEXT("\n#if 0 /*BEGIN_RESOURCE_TABLES*/\n");
		for (auto Pair : Environment.ResourceTableLayoutHashes)
		{
			Line += FString::Printf(TEXT("%s, %d\n"), *Pair.Key, Pair.Value);
		}
		Line += TEXT("NULL, 0\n");
		for (auto Pair : Environment.ResourceTableMap)
		{
			const FResourceTableEntry& Entry = Pair.Value;
			Line += FString::Printf(TEXT("%s, %s, %d, %d\n"), *Pair.Key, *Entry.UniformBufferName, Entry.Type, Entry.ResourceIndex);
		}
		Line += TEXT("NULL, NULL, 0, 0\n");

		Line += TEXT("#endif /*END_RESOURCE_TABLES*/\n");
		return Line;
	}

	void CreateEnvironmentFromResourceTable(const FString& String, FShaderCompilerEnvironment& OutEnvironment)
	{
		FString Prolog = TEXT("#if 0 /*BEGIN_RESOURCE_TABLES*/");
		int32 FoundBegin = String.Find(Prolog, ESearchCase::CaseSensitive);
		if (FoundBegin == INDEX_NONE)
		{
			return;
		}
		int32 FoundEnd = String.Find(TEXT("#endif /*END_RESOURCE_TABLES*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FoundBegin);
		if (FoundEnd == INDEX_NONE)
		{
			return;
		}

		// +1 for EOL
		const TCHAR* Ptr = &String[FoundBegin + 1 + Prolog.Len()];
		while (*Ptr == '\r' || *Ptr == '\n')
		{
			++Ptr;
		}
		const TCHAR* PtrEnd = &String[FoundEnd];
		while (Ptr < PtrEnd)
		{
			FString UB;
			if (!CrossCompiler::ParseIdentifier(Ptr, UB))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 Hash;
			if (!CrossCompiler::ParseSignedNumber(Ptr, Hash))
			{
				return;
			}
			// Optional \r
			CrossCompiler::Match(Ptr, '\r');
			if (!CrossCompiler::Match(Ptr, '\n'))
			{
				return;
			}

			if (UB == TEXT("NULL") && Hash == 0)
			{
				break;
			}
			OutEnvironment.ResourceTableLayoutHashes.FindOrAdd(UB) = (uint32)Hash;
		}

		while (Ptr < PtrEnd)
		{
			FString Name;
			if (!CrossCompiler::ParseIdentifier(Ptr, Name))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			FString UB;
			if (!CrossCompiler::ParseIdentifier(Ptr, UB))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 Type;
			if (!CrossCompiler::ParseSignedNumber(Ptr, Type))
			{
				return;
			}
			if (!CrossCompiler::Match(Ptr, TEXT(", ")))
			{
				return;
			}
			int32 ResourceIndex;
			if (!CrossCompiler::ParseSignedNumber(Ptr, ResourceIndex))
			{
				return;
			}
			// Optional
			CrossCompiler::Match(Ptr, '\r');
			if (!CrossCompiler::Match(Ptr, '\n'))
			{
				return;
			}

			if (Name == TEXT("NULL") && UB == TEXT("NULL") && Type == 0 && ResourceIndex == 0)
			{
				break;
			}
			FResourceTableEntry& Entry = OutEnvironment.ResourceTableMap.FindOrAdd(Name);
			Entry.UniformBufferName = UB;
			Entry.Type = Type;
			Entry.ResourceIndex = ResourceIndex;
		}
	}

	/**
	 * Parse an error emitted by the HLSL cross-compiler.
	 * @param OutErrors - Array into which compiler errors may be added.
	 * @param InLine - A line from the compile log.
	 */
	void ParseHlslccError(TArray<FShaderCompilerError>& OutErrors, const FString& InLine, bool bUseAbsolutePaths)
	{
		const TCHAR* p = *InLine;
		FShaderCompilerError* Error = new(OutErrors) FShaderCompilerError();

		// Copy the filename.
		while (*p && *p != TEXT('('))
		{
			Error->ErrorVirtualFilePath += (*p++);
		}

		if (!bUseAbsolutePaths)
		{
			Error->ErrorVirtualFilePath = ParseVirtualShaderFilename(Error->ErrorVirtualFilePath);
		}
		p++;

		// Parse the line number.
		int32 LineNumber = 0;
		while (*p && *p >= TEXT('0') && *p <= TEXT('9'))
		{
			LineNumber = 10 * LineNumber + (*p++ - TEXT('0'));
		}
		Error->ErrorLineString = *FString::Printf(TEXT("%d"), LineNumber);

		// Skip to the warning message.
		while (*p && (*p == TEXT(')') || *p == TEXT(':') || *p == TEXT(' ') || *p == TEXT('\t')))
		{
			p++;
		}
		Error->StrippedErrorMessage = p;
	}


	/** Map shader frequency -> string for messages. */
	static const TCHAR* FrequencyStringTable[] =
	{
		TEXT("Vertex"),
		TEXT("Hull"),
		TEXT("Domain"),
		TEXT("Pixel"),
		TEXT("Geometry"),
		TEXT("Compute"),
		TEXT("RayGen"),
		TEXT("RayMiss"),
		TEXT("RayHitGroup"),
		TEXT("RayCallable"),
	};

	/** Compile time check to verify that the GL mapping tables are up-to-date. */
	static_assert(SF_NumFrequencies == UE_ARRAY_COUNT(FrequencyStringTable), "NumFrequencies changed. Please update tables.");

	const TCHAR* GetFrequencyName(EShaderFrequency Frequency)
	{
		check((int32)Frequency >= 0 && Frequency < SF_NumFrequencies);
		return FrequencyStringTable[Frequency];
	}

	FHlslccHeader::FHlslccHeader() :
		Name(TEXT(""))
	{
		NumThreads[0] = NumThreads[1] = NumThreads[2] = 0;
	}

	bool FHlslccHeader::Read(const ANSICHAR*& ShaderSource, int32 SourceLen)
	{
#define DEF_PREFIX_STR(Str) \
		static const ANSICHAR* Str##Prefix = "// @" #Str ": "; \
		static const int32 Str##PrefixLen = FCStringAnsi::Strlen(Str##Prefix)
		DEF_PREFIX_STR(Inputs);
		DEF_PREFIX_STR(Outputs);
		DEF_PREFIX_STR(UniformBlocks);
		DEF_PREFIX_STR(Uniforms);
		DEF_PREFIX_STR(PackedGlobals);
		DEF_PREFIX_STR(PackedUB);
		DEF_PREFIX_STR(PackedUBCopies);
		DEF_PREFIX_STR(PackedUBGlobalCopies);
		DEF_PREFIX_STR(Samplers);
		DEF_PREFIX_STR(UAVs);
		DEF_PREFIX_STR(SamplerStates);
		DEF_PREFIX_STR(NumThreads);
#undef DEF_PREFIX_STR

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " !", 2) != 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		// Read shader name if any
		if (FCStringAnsi::Strncmp(ShaderSource, "// !", 4) == 0)
		{
			ShaderSource += 4;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				Name += (TCHAR)*ShaderSource;
				++ShaderSource;
			}

			if (*ShaderSource == '\n')
			{
				++ShaderSource;
			}
		}

		// Skip any comments that come before the signature.
		while (FCStringAnsi::Strncmp(ShaderSource, "//", 2) == 0 &&
			FCStringAnsi::Strncmp(ShaderSource + 2, " @", 2) != 0)
		{
			ShaderSource += 2;
			while (*ShaderSource && *ShaderSource++ != '\n')
			{
				// Do nothing
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, InputsPrefix, InputsPrefixLen) == 0)
		{
			ShaderSource += InputsPrefixLen;

			if (!ReadInOut(ShaderSource, Inputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, OutputsPrefix, OutputsPrefixLen) == 0)
		{
			ShaderSource += OutputsPrefixLen;

			if (!ReadInOut(ShaderSource, Outputs))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformBlocksPrefix, UniformBlocksPrefixLen) == 0)
		{
			ShaderSource += UniformBlocksPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute UniformBlock;
				if (!ParseIdentifier(ShaderSource, UniformBlock.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}
				
				if (!ParseIntegerNumber(ShaderSource, UniformBlock.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UniformBlocks.Add(UniformBlock);

				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				if (Match(ShaderSource, ','))
				{
					continue;
				}
			
				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UniformsPrefix, UniformsPrefixLen) == 0)
		{
			// @todo-mobile: Will we ever need to support this code path?
			check(0);
			return false;
/*
			ShaderSource += UniformsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				uint16 ArrayIndex = 0;
				uint16 Offset = 0;
				uint16 NumComponents = 0;

				FString ParameterName = ParseIdentifier(ShaderSource);
				verify(ParameterName.Len() > 0);
				verify(Match(ShaderSource, '('));
				ArrayIndex = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				Offset = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ':'));
				NumComponents = ParseNumber(ShaderSource);
				verify(Match(ShaderSource, ')'));

				ParameterMap.AddParameterAllocation(
					*ParameterName,
					ArrayIndex,
					Offset * BytesPerComponent,
					NumComponents * BytesPerComponent
					);

				if (ArrayIndex < OGL_NUM_PACKED_UNIFORM_ARRAYS)
				{
					PackedUniformSize[ArrayIndex] = FMath::Max<uint16>(
						PackedUniformSize[ArrayIndex],
						BytesPerComponent * (Offset + NumComponents)
						);
				}

				// Skip the comma.
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				verify(Match(ShaderSource, ','));
			}

			Match(ShaderSource, '\n');
*/
		}

		// @PackedGlobals: Global0(h:0,1),Global1(h:4,1),Global2(h:8,1)
		if (FCStringAnsi::Strncmp(ShaderSource, PackedGlobalsPrefix, PackedGlobalsPrefixLen) == 0)
		{
			ShaderSource += PackedGlobalsPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedGlobal PackedGlobal;
				if (!ParseIdentifier(ShaderSource, PackedGlobal.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				PackedGlobal.PackedType = *ShaderSource++;

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, PackedGlobal.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedGlobals.Add(PackedGlobal);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		// Packed Uniform Buffers (Multiple lines)
		// @PackedUB: CBuffer(0): CBMember0(0,1),CBMember1(1,1)
		while (FCStringAnsi::Strncmp(ShaderSource, PackedUBPrefix, PackedUBPrefixLen) == 0)
		{
			ShaderSource += PackedUBPrefixLen;

			FPackedUB PackedUB;

			if (!ParseIdentifier(ShaderSource, PackedUB.Attribute.Name))
			{
				return false;
			}

			if (!Match(ShaderSource, '('))
			{
				return false;
			}
			
			if (!ParseIntegerNumber(ShaderSource, PackedUB.Attribute.Index))
			{
				return false;
			}

			if (!Match(ShaderSource, ')'))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FPackedUB::FMember Member;
				ParseIdentifier(ShaderSource, Member.Name);
				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Offset))
				{
					return false;
				}
				
				if (!Match(ShaderSource, ','))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Member.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				PackedUB.Members.Add(Member);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}

			PackedUBs.Add(PackedUB);
		}

		// @PackedUBCopies: 0:0-0:h:0:1,0:1-0:h:4:1,1:0-1:h:0:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBCopiesPrefix, PackedUBCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, false, PackedUBCopies))
			{
				return false;
			}
		}

		// @PackedUBGlobalCopies: 0:0-h:12:1,0:1-h:16:1,1:0-h:20:1
		if (FCStringAnsi::Strncmp(ShaderSource, PackedUBGlobalCopiesPrefix, PackedUBGlobalCopiesPrefixLen) == 0)
		{
			ShaderSource += PackedUBGlobalCopiesPrefixLen;
			if (!ReadCopies(ShaderSource, true, PackedUBGlobalCopies))
			{
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplersPrefix, SamplersPrefixLen) == 0)
		{
			ShaderSource += SamplersPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FSampler Sampler;

				if (!ParseIdentifier(ShaderSource, Sampler.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, Sampler.Count))
				{
					return false;
				}

				if (Match(ShaderSource, '['))
				{
					// Sampler States
					do
					{
						FString SamplerState;
						
						if (!ParseIdentifier(ShaderSource, SamplerState))
						{
							return false;
						}

						Sampler.SamplerStates.Add(SamplerState);
					}
					while (Match(ShaderSource, ','));

					if (!Match(ShaderSource, ']'))
					{
						return false;
					}
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				Samplers.Add(Sampler);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, UAVsPrefix, UAVsPrefixLen) == 0)
		{
			ShaderSource += UAVsPrefixLen;

			while (*ShaderSource && *ShaderSource != '\n')
			{
				FUAV UAV;

				if (!ParseIdentifier(ShaderSource, UAV.Name))
				{
					return false;
				}

				if (!Match(ShaderSource, '('))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Offset))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIntegerNumber(ShaderSource, UAV.Count))
				{
					return false;
				}

				if (!Match(ShaderSource, ')'))
				{
					return false;
				}

				UAVs.Add(UAV);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, SamplerStatesPrefix, SamplerStatesPrefixLen) == 0)
		{
			ShaderSource += SamplerStatesPrefixLen;
			while (*ShaderSource && *ShaderSource != '\n')
			{
				FAttribute SamplerState;
				if (!ParseIntegerNumber(ShaderSource, SamplerState.Index))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}

				if (!ParseIdentifier(ShaderSource, SamplerState.Name))
				{
					return false;
				}

				SamplerStates.Add(SamplerState);

				// Break if EOL
				if (Match(ShaderSource, '\n'))
				{
					break;
				}

				// Has to be a comma!
				if (Match(ShaderSource, ','))
				{
					continue;
				}

				//#todo-rco: Need a log here
				//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
				return false;
			}
		}

		if (FCStringAnsi::Strncmp(ShaderSource, NumThreadsPrefix, NumThreadsPrefixLen) == 0)
		{
			ShaderSource += NumThreadsPrefixLen;
			if (!ParseIntegerNumber(ShaderSource, NumThreads[0]))
			{
				return false;
			}
			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[1]))
			{
				return false;
			}

			if (!Match(ShaderSource, ','))
			{
				return false;
			}

			if (!Match(ShaderSource, ' '))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, NumThreads[2]))
			{
				return false;
			}

			if (!Match(ShaderSource, '\n'))
			{
				return false;
			}
		}
	
		return ParseCustomHeaderEntries(ShaderSource);
	}

	bool FHlslccHeader::ReadCopies(const ANSICHAR*& ShaderSource, bool bGlobals, TArray<FPackedUBCopy>& OutCopies)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FPackedUBCopy PackedUBCopy;
			PackedUBCopy.DestUB = 0;

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceUB))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.SourceOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, '-'))
			{
				return false;
			}

			if (!bGlobals)
			{
				if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestUB))
				{
					return false;
				}

				if (!Match(ShaderSource, ':'))
				{
					return false;
				}
			}

			PackedUBCopy.DestPackedType = *ShaderSource++;

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.DestOffset))
			{
				return false;
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIntegerNumber(ShaderSource, PackedUBCopy.Count))
			{
				return false;
			}

			OutCopies.Add(PackedUBCopy);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				break;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		return true;
	}

	bool FHlslccHeader::ReadInOut(const ANSICHAR*& ShaderSource, TArray<FInOut>& OutAttributes)
	{
		while (*ShaderSource && *ShaderSource != '\n')
		{
			FInOut Attribute;

			if (!ParseIdentifier(ShaderSource, Attribute.Type))
			{
				return false;
			}

			if (Match(ShaderSource, '['))
			{
				if (!ParseIntegerNumber(ShaderSource, Attribute.ArrayCount))
				{
					return false;
				}

				if (!Match(ShaderSource, ']'))
				{
					return false;
				}
			}
			else
			{
				Attribute.ArrayCount = 0;
			}

			if (Match(ShaderSource, ';'))
			{
				if (!ParseSignedNumber(ShaderSource, Attribute.Index))
				{
					return false;
				}
			}

			if (!Match(ShaderSource, ':'))
			{
				return false;
			}

			if (!ParseIdentifier(ShaderSource, Attribute.Name))
			{
				return false;
			}

			// Optional array suffix
			if (Match(ShaderSource, '['))
			{
				Attribute.Name += '[';
				while (*ShaderSource)
				{
					Attribute.Name += *ShaderSource;
					if (Match(ShaderSource, ']'))
					{
						break;
					}
					++ShaderSource;
				}
			}

			OutAttributes.Add(Attribute);

			// Break if EOL
			if (Match(ShaderSource, '\n'))
			{
				break;
			}

			// Has to be a comma!
			if (Match(ShaderSource, ','))
			{
				continue;
			}

			//#todo-rco: Need a log here
			//UE_LOG(ShaderCompilerCommon, Warning, TEXT("Invalid char '%c'"), *ShaderSource);
			return false;
		}

		return true;
	}
}

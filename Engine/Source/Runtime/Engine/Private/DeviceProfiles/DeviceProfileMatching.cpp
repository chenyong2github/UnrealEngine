// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeviceProfiles/DeviceProfileMatching.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "UObject/Package.h"
#include "SceneManagement.h"
#include "SystemSettings.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "IDeviceProfileSelectorModule.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Internationalization/Regex.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

// Platform independent source types
static FName SRC_Chipset(TEXT("SRC_Chipset"));
static FName SRC_MakeAndModel(TEXT("SRC_MakeAndModel"));
static FName SRC_OSVersion(TEXT("SRC_OSVersion"));
static FName SRC_CommandLine(TEXT("SRC_CommandLine"));
static FName SRC_PrimaryGPUDesc(TEXT("SRC_PrimaryGPUDesc"));
static FName SRC_False(TEXT("false"));
static FName SRC_True(TEXT("true"));
static FName SRC_PreviousRegexMatch(TEXT("SRC_PreviousRegexMatch"));
static FName SRC_PreviousRegexMatch1(TEXT("SRC_PreviousRegexMatch1"));

// comparison operators:
static FName CMP_Equal(TEXT("=="));
static FName CMP_Less(TEXT("<"));
static FName CMP_LessEqual(TEXT("<="));
static FName CMP_Greater(TEXT(">"));
static FName CMP_GreaterEqual(TEXT(">="));
static FName CMP_NotEqual(TEXT("!="));
static FName CMP_Regex(TEXT("CMP_Regex"));
static FName CMP_EqualIgnore(TEXT("CMP_EqualIgnore"));
static FName CMP_LessIgnore(TEXT("CMP_LessIgnore"));
static FName CMP_LessEqualIgnore(TEXT("CMP_LessEqualIgnore"));
static FName CMP_GreaterIgnore(TEXT("CMP_GreaterIgnore"));
static FName CMP_GreaterEqualIgnore(TEXT("CMP_GreaterEqualIgnore"));
static FName CMP_NotEqualIgnore(TEXT("CMP_NotEqualIgnore"));
static FName CMP_Hash(TEXT("CMP_Hash"));
static FName CMP_Or(TEXT("OR"));
static FName CMP_And(TEXT("AND"));

class FRuleMatchRunner
{
	const TMap<FName, FString>* UserDefinedSrcs;
	FOutputDevice* ErrorDevice;
	FString PreviousRegexMatches[2];
public:
	struct FDeviceProfileMatchCriterion
	{
		FName SourceType;
		FString SourceArg;
		FName CompareType;
		FString MatchString;
	};

	FRuleMatchRunner(const TMap<FName, FString>* UserDefinedSrcsIN, FOutputDevice* ErrorDeviceIn) : UserDefinedSrcs(UserDefinedSrcsIN), ErrorDevice(ErrorDeviceIn){}
	bool ProcessRules(IDeviceProfileSelectorModule* DPSelectorModule, TArray<FDeviceProfileMatchCriterion>& MatchingCriteria, const FString& RuleName)
	{
		bool bResult = false;
		bool bFoundMatch = true;
		for (const FDeviceProfileMatchCriterion& DeviceProfileMatchCriterion : MatchingCriteria)
		{
			bool bCurrentMatch = true;
			FName SourceType = DeviceProfileMatchCriterion.SourceType;
			const FString& SourceArg = DeviceProfileMatchCriterion.SourceArg;
			const FString& MatchString = DeviceProfileMatchCriterion.MatchString;
			FName CompareType = DeviceProfileMatchCriterion.CompareType;
			FString CommandLine = FCommandLine::Get();

			FString SourceString("");
			FString PlatformSourceValue("");

			// Selector module gets first dibs on retrieving source data
			if (DPSelectorModule && DPSelectorModule->GetSelectorPropertyValue(SourceType, PlatformSourceValue)) { SourceString = PlatformSourceValue; }
			// universal properties
			else if (SourceType == SRC_Chipset) { SourceString = FPlatformMisc::GetCPUChipset(); }
			else if (SourceType == SRC_MakeAndModel) { SourceString = FPlatformMisc::GetDeviceMakeAndModel(); }
			else if (SourceType == SRC_OSVersion) { SourceString = FPlatformMisc::GetOSVersion(); }
			else if (SourceType == SRC_PrimaryGPUDesc) {SourceString = FPlatformMisc::GetGPUDriverInfo(FPlatformMisc::GetPrimaryGPUBrand()).DeviceDescription;}
			else if (SourceType == SRC_PreviousRegexMatch) { SourceString = PreviousRegexMatches[0]; }
			else if (SourceType == SRC_PreviousRegexMatch1) { SourceString = PreviousRegexMatches[1]; }
			else if (SourceType == SRC_CommandLine) { SourceString = CommandLine; }
			else if (SourceType == SRC_False) { SourceString = TEXT("false"); }
			else if (SourceType == SRC_True) { SourceString = TEXT("true"); }
			// SetSrc defined properties.
			else if (const FString* Value = UserDefinedSrcs ? UserDefinedSrcs->Find(SourceType) : nullptr) { SourceString = *Value; }
			else
			{
				// SourceType wasn't found. 
				ErrorDevice->Logf(TEXT("source type '%s' was not defined for matchingrule %s. (%s, %s, %s)"), *SourceType.ToString(), *RuleName, *SourceType.ToString(), *CompareType.ToString(), *MatchString);
			}

			const bool bNumericOperands = SourceString.IsNumeric() && MatchString.IsNumeric();
			if (CompareType == CMP_Equal)
			{
				if (SourceType == SRC_CommandLine)
				{
					if (!FParse::Param(*SourceString, *MatchString))
					{
						bCurrentMatch = false;
					}
				}
				else
				{
					if (SourceString != MatchString)
					{
						bCurrentMatch = false;
					}
				}
			}
			else if (CompareType == CMP_Less)
			{
				if ((bNumericOperands && FCString::Atof(*SourceString) >= FCString::Atof(*MatchString)) || (!bNumericOperands && SourceString >= MatchString))
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_LessEqual)
			{
				if ((bNumericOperands && FCString::Atof(*SourceString) > FCString::Atof(*MatchString)) || (!bNumericOperands && SourceString > MatchString))
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_Greater)
			{
				if ((bNumericOperands && FCString::Atof(*SourceString) <= FCString::Atof(*MatchString)) || (!bNumericOperands && SourceString <= MatchString))
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_GreaterEqual)
			{
				if ((bNumericOperands && FCString::Atof(*SourceString) < FCString::Atof(*MatchString)) || (!bNumericOperands && SourceString < MatchString))
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_NotEqual)
			{
				if (SourceType == SRC_CommandLine)
				{
					if (FParse::Param(*CommandLine, *MatchString))
					{
						bCurrentMatch = false;
					}
				}
				else
				{
					if (*SourceString == MatchString)
					{
						bCurrentMatch = false;
					}
				}
			}
			else if (CompareType == CMP_Or || CompareType == CMP_And)
			{
				bool bArg1, bArg2;

				if (bNumericOperands)
				{
					bArg1 = FCString::Atoi(*SourceString) != 0;
					bArg2 = FCString::Atoi(*MatchString) != 0;
				}
				else
				{
					static const FString TrueString("true");
					bArg1 = SourceString == TrueString;
					bArg2 = MatchString == TrueString;
				}

				if (CompareType == CMP_Or)
				{
					bCurrentMatch = (bArg1 || bArg2);
				}
				else
				{
					bCurrentMatch = (bArg1 && bArg2);
				}
			}
			else if (CompareType == CMP_EqualIgnore)
			{
				if (SourceString.ToLower() != MatchString.ToLower())
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_LessIgnore)
			{
				if (SourceString.ToLower() >= MatchString.ToLower())
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_LessEqualIgnore)
			{
				if (SourceString.ToLower() > MatchString.ToLower())
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_GreaterIgnore)
			{
				if (SourceString.ToLower() <= MatchString.ToLower())
				{
					bCurrentMatch = false;
				}
				else if (CompareType == CMP_GreaterEqualIgnore)
				{
					if (SourceString.ToLower() < MatchString.ToLower())
					{
						bCurrentMatch = false;
					}
				}
			}
			else if (CompareType == CMP_NotEqualIgnore)
			{
				if (SourceString.ToLower() == MatchString.ToLower())
				{
					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_Regex)
			{
				const FRegexPattern RegexPattern(MatchString);
				FRegexMatcher RegexMatcher(RegexPattern, SourceString);
				if (RegexMatcher.FindNext())
				{
					PreviousRegexMatches[0] = RegexMatcher.GetCaptureGroup(1);
					PreviousRegexMatches[1] = RegexMatcher.GetCaptureGroup(2);
				}
				else
				{
					PreviousRegexMatches[0].Empty();
					PreviousRegexMatches[1].Empty();

					bCurrentMatch = false;
				}
			}
			else if (CompareType == CMP_Hash)
			{
				// Salt string is concatenated onto the end of the input text.
				// For example the input string "PhoneModel" with salt "Salt" and pepper "Pepper" can be computed with
				// % printf "PhoneModelSaltPepper" | openssl dgst -sha1 -hex
				// resulting in d9e5cbd6b0e4dba00edd9de92cf64ee4c3f3a2db and would be stored in the matching rules as 
				// "Salt|d9e5cbd6b0e4dba00edd9de92cf64ee4c3f3a2db". Salt is optional.
				FString MatchHashString;
				FString SaltString;
				if (!MatchString.Split(TEXT("|"), &SaltString, &MatchHashString))
				{
					MatchHashString = MatchString;
				}
				FString HashInputString = SourceString + SaltString
#ifdef HASH_PEPPER_SECRET_GUID
					+ HASH_PEPPER_SECRET_GUID.ToString()
#endif
					;

				FSHAHash SourceHash;
				FSHA1::HashBuffer(TCHAR_TO_ANSI(*HashInputString), HashInputString.Len(), SourceHash.Hash);
				if (SourceHash.ToString() != MatchHashString.ToUpper())
				{
					bCurrentMatch = false;
				}
			}
			else
			{
				bCurrentMatch = false;
			}

			bFoundMatch = bCurrentMatch;
		}
		return bFoundMatch;
	}
};

static bool EvaluateMatch(IDeviceProfileSelectorModule* DPSelectorModule, const FDPMatchingRulestructBase& rule, TMap<FName, FString>& UserDefinedSrc, FString& SelectedFragmentsOUT, FOutputDevice* Errors)
{
	static const FString NoName("");
	const FString& RuleName = rule.RuleName.IsEmpty() ? NoName : rule.RuleName;

	if(!rule.AppendFragments.IsEmpty())
	{
		if(!SelectedFragmentsOUT.IsEmpty())
		{
			SelectedFragmentsOUT += TEXT(",");
		}
		if (rule.AppendFragments.Contains(TEXT("[clear]")))
		{
			SelectedFragmentsOUT.Empty();
			SelectedFragmentsOUT += rule.AppendFragments.Replace(TEXT("[clear]"), TEXT(""));
		}
		else
		{
			SelectedFragmentsOUT += rule.AppendFragments;
		}
	}

	if (!rule.SetSrc.IsEmpty())
	{
		TArray<FString> NewSRCs;
		rule.SetSrc.ParseIntoArray(NewSRCs, TEXT(","), true);
		for (FString& SRCentry : NewSRCs)
		{
			FString CSRCType, CSRCValue;
			if (SRCentry.Split(TEXT("="), &CSRCType, &CSRCValue))
			{
				UserDefinedSrc.Add(FName(CSRCType), CSRCValue);
				UE_LOG(LogInit, Verbose, TEXT("MatchesRules: Adding source %s : %s"), *CSRCType, *CSRCValue);
			}
		}
	}

	TArray<FDPMatchingIfCondition> Expression = rule.IfConditions;
	if (Expression.Num() == 0)
	{
		UE_LOG(LogInit, Verbose, TEXT("MatchesRules: %s, no match criteria."), *RuleName);
		return true;
	}

	// if no operator specified, insert an implicit AND operator.
	for (int i = 0; i < Expression.Num(); i++)
	{
		// if this and next are )( operations insert AND
		if (i + 1 < Expression.Num() && (!Expression[i].Arg1.IsEmpty() || Expression[i].Operator == TEXT(")")))
		{
			if (!Expression[i + 1].Arg1.IsEmpty() || Expression[i + 1].Operator == TEXT("("))
			{
				FDPMatchingIfCondition ImplicitAnd;
				ImplicitAnd.Operator = TEXT("AND");
				Expression.Insert(ImplicitAnd, i + 1);
				i++;
			}
		}
	}

	FString Line;
	for (int i = 0; i < Expression.Num(); i++)
	{
		if (!(Expression[i].Arg1.IsEmpty()))
		{
			Line += FString::Printf(TEXT("(%s %s %s)"), *Expression[i].Arg1, *Expression[i].Operator.ToString(), *Expression[i].Arg2);
		}
		else
		{
			Line += FString::Printf(TEXT(" %s "), *Expression[i].Operator.ToString());
		}
	}
	UE_LOG(LogInit, Verbose, TEXT("MatchesRules: rule %s : %s"), *RuleName, *Line);

	struct FExpressionItem
	{
		FExpressionItem(FString InValue, bool bInOperator) : Value(InValue), bOperator(bInOperator) {}
		FString Value;
		bool bOperator;
	};

	TArray<FExpressionItem> RPNOutput;
	TArray<FString> Operators;

	auto PushOperand = [&RPNOutput](FString InValue) { RPNOutput.Push(FExpressionItem(InValue, false)); };
	auto PushOperator = [&RPNOutput](FString InValue)
	{
		RPNOutput.Push(FExpressionItem(InValue, true));
	};

	for (const FDPMatchingIfCondition& element : Expression)
	{
		if (!element.Arg1.IsEmpty())
		{
			PushOperand(element.Arg1);
			//if (!element.Arg2.IsEmpty())
			{
				PushOperand(element.Arg2);
			}
			if (!element.Operator.IsNone())
			{
				PushOperator(element.Operator.ToString());
			}
		}
		else if (!element.Operator.IsNone())
		{
			if (element.Operator == FName(TEXT(")")))
			{
				while (Operators.Num())
				{
					FString PoppedOperator = Operators.Pop();
					if (PoppedOperator != FString(TEXT("(")))
					{
						PushOperator(PoppedOperator);
					}
					else
					{
						break;
					}
				}
			}
			else
			{
				Operators.Push(element.Operator.ToString());
			}
		}
	}
	while (Operators.Num())
	{
		FString PoppedOperator = Operators.Pop();
		if (PoppedOperator == FString(TEXT("(")) || PoppedOperator == FString(TEXT(")")))
		{
			Errors->Logf(TEXT("MatchesRules: rule %s failed due to mismatching parenthesis! %s"), *RuleName, *PoppedOperator);
			return false;
		}
		PushOperator(PoppedOperator);
	}
	UE_LOG(LogInit, Verbose, TEXT("MatchesRules: rule %s : "), *RuleName);
	for (int i = 0; i < RPNOutput.Num(); i++)
	{
		UE_LOG(LogInit, Verbose, TEXT("MatchesRules: (%d - %s)"), i, *RPNOutput[i].Value);
	}

	FRuleMatchRunner MatchMe(&UserDefinedSrc, Errors);
	TArray<FExpressionItem> ExpressionStack;
	for (int i = 0; i < RPNOutput.Num(); i++)
	{
		if (!RPNOutput[i].bOperator)
		{
			ExpressionStack.Push(RPNOutput[i]);
		}
		else
		{
			FExpressionItem B = ExpressionStack.Pop();
			FExpressionItem A = ExpressionStack.Pop();
			TArray<FRuleMatchRunner::FDeviceProfileMatchCriterion> MatchingCriteria;

			FRuleMatchRunner::FDeviceProfileMatchCriterion crit;
			crit.CompareType = FName(RPNOutput[i].Value);
			crit.SourceType = FName(A.Value);
			crit.MatchString = B.Value;

			MatchingCriteria.Add(crit);
			bool bResult = MatchMe.ProcessRules(DPSelectorModule, MatchingCriteria, RuleName);
			FExpressionItem C(bResult ? TEXT("true") : TEXT("false"), false);
			ExpressionStack.Push(C);

			UE_LOG(LogInit, Verbose, TEXT("MatchesRules: rule %s evaluating (%s %s %s) = %s"), *RuleName, *A.Value, *RPNOutput[i].Value, *B.Value, *C.Value);
		}
	}
	FExpressionItem Result = ExpressionStack.Pop();
	UE_LOG(LogInit, Log, TEXT("MatchesRules: rule %s = %s"), *RuleName, *Result.Value);

	const bool bMatched = Result.Value == TEXT("true");
	const FDPMatchingRulestructBase* Next = bMatched ? rule.GetOnTrue() : rule.GetOnFalse();

	bool bSuccess = true;
	if (Next)
	{
		bSuccess = EvaluateMatch(DPSelectorModule, *Next, UserDefinedSrc, SelectedFragmentsOUT, Errors);
	}

	return bSuccess;
}

// Convert a string of fragment names to a FSelectedFragmentProperties array.
// FragmentName1,FragmentName2,[optionaltag]FragmentName3, etc.
static TArray<FSelectedFragmentProperties> FragmentStringToFragmentProperties(const FString& FragmentString)
{
	TArray<FSelectedFragmentProperties> FragmentPropertiesList;
	TArray<FString> AppendedFragments;
	FragmentString.ParseIntoArray(AppendedFragments, TEXT(","), true);
	for (const FString& Fragment : AppendedFragments)
	{
		FSelectedFragmentProperties FragmentProperties;
		int32 TagDeclStart = Fragment.Find(TEXT("["));
		if (TagDeclStart >= 0)
		{
			int32 TagDeclEnd = Fragment.Find(TEXT("]"));
			if (TagDeclEnd > TagDeclStart)
			{
				FName Tag(Fragment.Mid(TagDeclStart + 1, (TagDeclEnd - TagDeclStart)-1));
				FragmentProperties.Tag = Tag;
				FragmentProperties.bEnabled = false;
				FragmentProperties.Fragment = Fragment.RightChop(TagDeclEnd + 1);
				FragmentPropertiesList.Emplace(MoveTemp(FragmentProperties));
			}
		}
		else
		{
			FragmentProperties.Fragment = Fragment;
			FragmentProperties.bEnabled = true;
			FragmentPropertiesList.Emplace(MoveTemp(FragmentProperties));
		}
	}
	return FragmentPropertiesList;
}

class FDeviceProfileMatchingErrorContext: public FOutputDevice
{
public:
	FString Stage;
	int32 NumErrors;

	FDeviceProfileMatchingErrorContext()
		: FOutputDevice()
		, NumErrors(0)
	{}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		UE_LOG(LogInit, Error, TEXT("DeviceProfileMatching: Error while parsing Matching Rules (%s) : %s"), *Stage, V);
		NumErrors++;
	}
};

static bool DPHasMatchingRules(const FString& ParentDP, FConfigCacheIni* ConfigSystem)
{
	FString SectionSuffix = *FString::Printf(TEXT(" %s"), *UDeviceProfile::StaticClass()->GetName());
	FString CurrentSectionName = ParentDP + SectionSuffix;
	FString ArrayName(TEXT("MatchingRules"));
	TArray<FString> MatchingRulesArray;
	return ConfigSystem->GetArray(*CurrentSectionName, *ArrayName, MatchingRulesArray, GDeviceProfilesIni) != 0;
}

static FString LoadAndProcessMatchingRulesFromConfig(const FString& ParentDP, IDeviceProfileSelectorModule* DPSelector, FConfigCacheIni* ConfigSystem)
{
	FString SectionSuffix = *FString::Printf(TEXT(" %s"), *UDeviceProfile::StaticClass()->GetName());
	FString CurrentSectionName = ParentDP + SectionSuffix;
	FString ArrayName(TEXT("MatchingRules"));
	TArray<FString> MatchingRulesArray;
	ConfigSystem->GetArray(*CurrentSectionName, *ArrayName, MatchingRulesArray, GDeviceProfilesIni);

	TMap<FName, FString> UserDefinedSrc;
	FString SelectedFragments;
	FDeviceProfileMatchingErrorContext DPMatchingErrorOutput;
	int Count = 0;
	for(const FString& RuleText : MatchingRulesArray)
	{
		DPMatchingErrorOutput.Stage = FString::Printf(TEXT("%s rule #%d"), *ParentDP, Count);
		Count++;
		FDPMatchingRulestruct RuleStruct;
		FDPMatchingRulestruct::StaticStruct()->ImportText(*RuleText, &RuleStruct, nullptr, EPropertyPortFlags::PPF_None, &DPMatchingErrorOutput, FDPMatchingRulestruct::StaticStruct()->GetName(), true);
		EvaluateMatch(DPSelector, RuleStruct, UserDefinedSrc, SelectedFragments, &DPMatchingErrorOutput);
	}
#if UE_BUILD_SHIPPING
	UE_CLOG(DPMatchingErrorOutput.NumErrors > 0, LogInit, Error, TEXT("DeviceProfileMatching: %d Error(s) encountered while processing MatchedRules for %s"), DPMatchingErrorOutput.NumErrors, *ParentDP);
#else
	UE_CLOG(DPMatchingErrorOutput.NumErrors > 0, LogInit, Fatal, TEXT("DeviceProfileMatching: %d Error(s) encountered while processing MatchedRules for %s"), DPMatchingErrorOutput.NumErrors, *ParentDP);
#endif

	return SelectedFragments;
}

static FString RemoveAllWhiteSpace(const FString& StringIN)
{
	FString Ret;
	Ret.Reserve(StringIN.Len());
	for (TCHAR Character : StringIN)
	{
		if (!FChar::IsWhitespace(Character))
		{
			Ret += Character;
		}
	}
	return Ret;
}

TArray<FSelectedFragmentProperties> UDeviceProfileManager::FindMatchingFragments(const FString& ParentDP, FConfigCacheIni* ConfigSystem)
{
	FString SelectedFragments;
#if !UE_BUILD_SHIPPING
	// Override selected fragments with commandline specified list -DPFragments=fragmentname,fragmentname2,[taggedname]fragment,... 
	FString DPFragmentString;
	if (FParse::Value(FCommandLine::Get(), TEXT("DPFragments="), DPFragmentString, false))
	{
		SelectedFragments = DPFragmentString;
	}
	else
#endif
	{
		bool bIsPreview = false;
#if ALLOW_OTHER_PLATFORM_CONFIG
		bIsPreview = ConfigSystem != GConfig;
#endif
		IDeviceProfileSelectorModule* DPSelector = bIsPreview ? GetPreviewDeviceProfileSelectorModule(ConfigSystem) : GetDeviceProfileSelectorModule();
		SelectedFragments = LoadAndProcessMatchingRulesFromConfig(ParentDP, DPSelector, ConfigSystem);
		
		// previewing a DP with matching rules will run if conditions with the host device's data sources. It will likely not match the preview device's behavior.
		UE_CLOG(bIsPreview && !DPSelector && DPHasMatchingRules(ParentDP, ConfigSystem), LogInit, Warning, TEXT("Preview DP %s contains fragment matching rules, but no preview profile selector was found. The selected fragments for %s will likely not match the behavior of the intended preview device."), *ParentDP, *ParentDP);
	}
	SelectedFragments = RemoveAllWhiteSpace(SelectedFragments);
	if(!SelectedFragments.IsEmpty())
	{
		FGenericCrashContext::SetEngineData(TEXT("DeviceProfile.MatchedFragments"), SelectedFragments);
	}

	UE_CLOG(!SelectedFragments.IsEmpty(), LogInit, Log, TEXT("MatchesRules:Fragment string %s"), *SelectedFragments);
	TArray<FSelectedFragmentProperties> MatchedFragments = FragmentStringToFragmentProperties(SelectedFragments);

	UE_CLOG(MatchedFragments.Num()>0, LogInit, Log, TEXT("MatchesRules: MatchedFragments:"));
	for (FSelectedFragmentProperties& MatchedFrag : MatchedFragments)
	{
		UE_CLOG(MatchedFrag.Tag == NAME_None, LogInit, Log, TEXT("MatchesRules: %s, enabled %d"), *MatchedFrag.Fragment, MatchedFrag.bEnabled);
		UE_CLOG(MatchedFrag.Tag != NAME_None, LogInit, Log, TEXT("MatchesRules: %s=%s, enabled %d"), *MatchedFrag.Tag.ToString(), *MatchedFrag.Fragment, MatchedFrag.bEnabled);
	}

	return MatchedFragments;
}

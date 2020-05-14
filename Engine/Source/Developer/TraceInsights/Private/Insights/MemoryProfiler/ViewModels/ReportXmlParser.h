// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FXmlNode;
class FXmlAttribute;

namespace Insights
{

struct FGraphConfig;
struct FReportSummaryTableConfig;
struct FReportTypeConfig;
struct FReportConfig;

class FReportXmlParser
{
public:
	void LoadReportGraphsXML(FReportConfig& ReportConfig, FString Filename);
	void LoadReportTypesXML(FReportConfig& ReportConfig, FString Filename);

	void AutoLoadLLMReportXML(FReportConfig& ReportConfig);

private:
	void ParseGraph(FGraphConfig& GraphConfig, const FXmlNode* XmlNode);
	void ParseReportSummaryTable(FReportSummaryTableConfig& ReportSummaryTable, const FXmlNode* XmlNode);
	void ParseReportType(FReportTypeConfig& ReportType, const FXmlNode* XmlNode);
	void UnknownXmlNode(const FXmlNode* XmlNode, const FXmlNode* XmlParentNode = nullptr);
	void UnknownXmlAttribute(const FXmlNode* XmlNode, const FXmlAttribute& XmlAttribute);
};

} // namespace Insights

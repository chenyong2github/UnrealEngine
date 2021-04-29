// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class FReportDialog;

class FReportWindow
{
  public:
	static void Create();
	static void Delete();
	static void Update();

  private:
	FReportWindow();

	~FReportWindow();

	void Start();

	void Stop();

	FReportDialog* ReportDialog = nullptr;
};

class FTraceListener : public ITraceListener
{
  public:
	static FTraceListener& Get();

	static void Delete();

	FTraceListener();

	virtual void NewTrace(EP2DB InTraceLevel, const utf8_string& InMsg) override;

	static GSErrCode Register();

	// Install command handler and start checking thread
	static void Initialize();

	bool		bScheduledForUpdate = false;
	std::string Traces;

	// Show newer version alert
	static GSErrCode __ACENV_CALL UpdateTraces(GSHandle paramHandle, GSPtr resultData, bool silentMode);
};

END_NAMESPACE_UE_AC

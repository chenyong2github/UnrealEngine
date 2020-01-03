// Copyright Epic Games, Inc. All Rights Reserved.

import {runTests as notifications_runTests} from './notifications'
import {runTests as branchmap_runTests} from './branchmap'

let failed = 0
failed += notifications_runTests()
failed += branchmap_runTests()

process.exitCode = failed

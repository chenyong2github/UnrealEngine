// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from '../common/logger';
// import { runTests as graph_runTests } from '../new/graph';
// import { runTests as branchgraph_runTests } from './branchgraph';
// import { runTests as notifications_runTests } from './notifications';
// import { runTests as ztag_runTests } from '../common/ztag';

const unitTestsLogger = new ContextualLogger('Unit Tests')

const tests = [
	() => require('./notifications.js').runTests,
	() => require('./branchgraph.js').runTests,
	() => require('../new/graph.js').runTests,
	() => require('../common/ztag.js').runTests,
]

let failed = 0
for (const test of /**/tests/*/tests.slice(0, 3)/**/) {
	failed += test()(unitTestsLogger)	
}

process.exitCode = failed

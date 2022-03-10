// Copyright Epic Games, Inc. All Rights Reserved.

import { observable, action, when, reaction } from "mobx";
import { DataWrapper, TestDataWrapper, TestDataCollection } from '../../../backend/TestDataHandler'

export enum TestState {
    Failed = "Fail",
    Success = "Success",
    SuccessWithWarnings = "SuccessWithWarnings",
    NotRun = "NotRun",
    InProcess = "InProcess",
    Skipped = "Skipped",
    Unknown = "Unknown",
}

export enum EventType {
    Info = "Info",
    Error = "Error",
    Warning = "Warning",
}

export enum ArtifactType {
    ImageCompare = "image comparison",
    Approved = "approved",
    Unapproved = "unapproved",
    Difference = "difference",
}

export type TestArtifact = {
    Tag: string;
    ReferencePath: string;
}

export type TestEvent = {
    Message: string;
    Context: string;
    Type: string;
    Tag: string;
    Hash: string;
    DateTime: string;
    Artifacts: TestArtifact[];
}

export type TestResult = {
    Name: string;
    TestUID: string;
    Suite: string;
    State: string;
    DeviceAppInstanceName: string;
    ErrorCount: number;
    WarningCount: number;
    ErrorHashAggregate: string;
    DateTime: string;
    TimeElapseSec: number;
    // references that need to be initiated post fetch
    Details: TestResultDetails;
    Session: TestSessionWrapper;
}

export type TestResultDetails = {
    Events: TestEvent[];
}

export type TestDevice = MetadataHolder& {
    Name: string;
    AppInstanceName: string;
}

export type TestSessionInfo = {
    DateTime: string;
    TimeElapseSec: number;
    Tests: TestResultsMap;
    TestResultsTestDataUID: string;
}

export type IndexedError = {
    Message: string;
    Tag: string;
    TestUIDs: string[];
}

export type Metadata = {[Key in string]: string|undefined}

type MetadataHolder = {
    Metadata?: Metadata
}

const generatetHashFromString = (aggregate: string) => {
    let hash = 0;
    for (let i = 0; i < aggregate.length; i++) {
        const char = aggregate.charCodeAt(i);
        hash = ((hash << 5) - hash) + char;
        hash = hash & hash;
    }
    return hash.toString(16);
}

export const generateHashFromMetadata = (obj: Metadata) => {
    const keys = Object.keys(obj).sort();
    const aggregate = keys.map((key) => key + (obj[key]??'')).join();
    return generatetHashFromString(aggregate);
}

export class MetaWrapper {
    private collection?: Metadata
    private _hash: string

    constructor(collection?: Metadata) {
        this.collection = collection;
        this._hash = "";
    }

    get(key: string) : string | undefined {
        if(this.collection) {
            return this.collection[key];
        }

        return undefined;
    }

    map(mapFunc: (key: string) => any) : any[] {
        if (this.collection) {
            return Object.keys(this.collection).map(mapFunc);
        }
        return [];
    }

    private generateHashCode(): string {
        if(this.collection) {
            return generateHashFromMetadata(this.collection);
        }
        return "";
    }

    get hash(): string {
        if(!this._hash) {
            this._hash = this.generateHashCode();
        }
        return this._hash;
    }
}

export type IndexedErrorMap = {[Key in string]: IndexedError}

export type TestResultDetailsMap = {[Key in string]: TestResultDetails}

export type TestResultsMap = {[Key in string]: TestResult}

export type TestSession = MetadataHolder& {
    Name: string;
    Type: string;
    PreFlightChange: string;
    TestSessionInfo: TestSessionInfo;
    IndexedErrors: IndexedErrorMap;
    Devices: TestDevice[];
}

type CheckList = {[Key in string]: boolean}

export enum SessionFields {
    initiate = "data.Metadata,data.Name,data.PreFlightChange",
    remaining = "data.TestSessionInfo,data.IndexedErrors,data.Devices,data.Type",
}

export class TestSessionWrapper implements TestSession, DataWrapper {
    get Name(): string { return this.getOwnProperty('Name'); }
    get Type(): string { return this.getOwnProperty('Type'); }
    get PreFlightChange(): string { return this.getOwnProperty('PreFlightChange'); }
    get TestSessionInfo(): TestSessionInfo { return this.getOwnProperty('TestSessionInfo'); }
    get IndexedErrors(): IndexedErrorMap { return this.getOwnProperty('IndexedErrors'); }
    get Devices(): TestDevice[] { return this.getOwnProperty('Devices'); }
    get Metadata(): Metadata { return this.getOwnProperty('Metadata'); }

    Testdata: TestDataWrapper;
    MetaHandler?: MetaWrapper;

    @observable
    propertiesLoaded: CheckList = {
        TestSessionInfo: false
    };
    @action
    setPropertiesLoaded(...properties: string[]) {
        properties.forEach((key) => {
            this.propertiesLoaded[key] = true;
        })
    }
    isPropertyLoaded(key: string) {
        return this.propertiesLoaded[key];
    }
    isAllPropertiesLoaded() {
        return this.propertiesLoaded['TestSessionInfo']; // check TestSessionInfo only for now
    }
    getMissingProperties() {
        return SessionFields.remaining.split(',').map((item) => item.slice(5));
    }
    whenPropertyLoaded(key: string) {
        return when(() => this.propertiesLoaded[key]);
    }

    private _testsession: TestSession;

    constructor(testdata: TestDataWrapper) {
        this._testsession = testdata.data;
        this.Testdata = testdata;
        // first initialization, we should call the post Properties Update
        this.postPropertiesUpdate();
    }

    getOwnProperty<T>(key: string): T {
        return Object.getOwnPropertyDescriptor(this._testsession, key)?.value as T;
    }

    getUID(): string {
        return this.Name + (this.MetaHandler?.hash??'');
    }

    postPropertiesUpdate() {
        this.capitalizeKeys()
        const data = this._testsession;
        if(!this.MetaHandler && data.Metadata) {
            this.MetaHandler = new MetaWrapper(data.Metadata);
        }
        if(data.TestSessionInfo && !this.isPropertyLoaded('TestSessionInfo')) {
            for(const key in data.TestSessionInfo.Tests) {
                const test = data.TestSessionInfo.Tests[key];
                test.Session = this;
            }
            this.setPropertiesLoaded('TestSessionInfo');
        }
    }

    getTestResultByUID(uid: string) {
        if(this.TestSessionInfo) {
            return this.TestSessionInfo.Tests[uid];
        }

        return undefined
    }

    getDevice(AppInstanceName: string) {
        if(this.Devices) {
            return this.Devices.find((item) => item.AppInstanceName === AppInstanceName);
        }

        return undefined
    }

    private capitalizeKeys() {
        // Fix inconsistances in object keys fetched from Horde backend.
        const data = this._testsession as any;
        for (var key in data) {
            const first = key.charAt(0);
            const firstUpper = first.toUpperCase();
            if(first !== firstUpper) {
                const value = data[key];
                delete data[key];
                data[firstUpper + key.slice(1)] = value;
            }
        }
    }
}

export class TestStats {
    Failed: number = 0;
    Passed: number = 0;
    Skipped: number = 0;
    Unexecuted: number = 0;
    Incomplete: number = 0;
}

export class Section {
    Name: string;
    FullName: string;
    Type: FilterType;
    Stats: TestStats;
    Results: TestResult[];
    TestNames: Set<string>;

    constructor(name: string, fullName: string, type: FilterType) {
        this.Name = name;
        this.FullName = fullName;
        this.Type = type;
        this.Stats = new TestStats();
        this.Results = [];
        this.TestNames = new Set();
    }

    addTest(test: TestResult, testName?: string) {
        this.Results.push(test);
        this.TestNames.add(testName??test.Name);
        // update stats
        switch(test.State) {
        case TestState.Failed:
            this.Stats.Failed++;
            break;
        case TestState.NotRun:
            this.Stats.Unexecuted++;
            break;
        case TestState.Success:
            this.Stats.Passed++;
            break;
        case TestState.InProcess:
            this.Stats.Incomplete++;
            break;
        case TestState.Skipped:
            this.Stats.Skipped++;
            break;
        }
    }

    get isOneTest(): boolean {
        return this.TestNames.size === 1 && this.TestNames.has(this.FullName)
    }
}

export enum FilterType {
    suite = 'suite',
    name = 'name',
    project = 'project',
    testuid = 'testuid',
    metauid = 'metauid',
    testchange = 'testchange',
    view = 'view',
    namecontains = 'namecontains',
}

export type SectionFilter = {
    suite?: string;
    name?: string;
    project?: string;
    namecontains?: string;
}

export type ReportFilter = {
    testuid?: string;
    metauid?: string;
    testchange?: number;
    view?: string;
}

type GetKeyFunction = (test: TestResult) => [string, string];

export class SectionCollection {

    private fixedTestNameCache: Map<string, string>;
    private filterCache: Map<string, {hash: string, sections: Section[]}>;
    private onTheFlyCache: {filter: string, hash: string, sections: Section[]} | undefined;

    constructor() {
        this.fixedTestNameCache = new Map();
        this.filterCache = new Map();
    }

    extractSections(filter: SectionFilter, sessions: TestSessionCollection) {
        // Get the sections, first look for in the cache, otherwise generate it
        const filterCacheKey = generateHashFromMetadata(filter);
        const cache = this.filterCache.get(filterCacheKey);
        const isSameHash = cache && cache.hash === sessions.hash;
        const sections = cache && isSameHash? cache.sections : this.filterSections(filter, sessions.getTests());
        if (!this.filterCache.has(filterCacheKey) || !isSameHash) {
            this.filterCache.set(filterCacheKey, {hash: sessions.hash, sections:sections})
        }
        return sections;
    }

    reExtractSectionOnTheFly(filter: SectionFilter, testNameFilter: string, sessions: TestSessionCollection) {
        // to use only when iterating through cached data
        let sections = this.extractSections({...filter, namecontains: undefined}, sessions);
        const onTheFlyFilter = {...filter, namecontains: testNameFilter.toLowerCase()}
        sections = this.filterSections(onTheFlyFilter, sections.flatMap((section) => section.Results));
        this.onTheFlyCache = {filter: generateHashFromMetadata(onTheFlyFilter), hash: sessions.hash, sections};
        return sections;
    }

    promoteOnTheFlyCache() {
        if(this.onTheFlyCache) {
            this.filterCache.set(this.onTheFlyCache.filter, this.onTheFlyCache);
            this.onTheFlyCache = undefined;
        }
    }

    private filterSections(filter: SectionFilter, tests: TestResult[]) {
        const filterFunc = (test: TestResult) => {
            if(filter.project) {
                if(test.Session.MetaHandler?.get('Project') !== filter.project) {
                    return false;
                }
            }
            if(filter.suite) {
                if(test.Suite !== filter.suite) {
                    return false;
                }
            }
            if(filter.namecontains) {
                const substrings = filter.namecontains.split(' ');
                const testName = test.Name.toLowerCase();
                if(!substrings.some((value) => testName.indexOf(value) >= 0)) {
                    return false;
                }
            }
            if(filter.name) {
                if(this.fixTestName(test.Name).indexOf(filter.name + '.') !== 0) {
                    return false;
                }
            }
            return true;
        }
        const filteredTests = tests.filter(filterFunc);

        const commonSection = filter.name? this.getCommonSection(filteredTests.map((item) => this.fixTestName(item.Name))) : "";
        const sectionKeyFunc = (key: FilterType): GetKeyFunction | undefined => {
            switch(key) {
                case FilterType.suite:
                    return (test: TestResult) => {
                        return [test.Suite, test.Suite];
                    }
                case FilterType.name:
                    return (test: TestResult) => {
                        const suite = this.fixTestName(test.Name).slice(commonSection.length).split('.', 1)[0];
                        return [suite, commonSection+suite];
                    }
            }
        };
        const sectionFocus = filter.suite || filter.name? FilterType.name : FilterType.suite;
        const sections = this.buildSections(filteredTests, sectionFocus, sectionKeyFunc(sectionFocus)!);
        // sorting
        sections.forEach((section) => this.sortSectionResults(section));
        this.sortSections(sections);
        return sections;
    }

    private buildSections(tests: TestResult[], type: FilterType, keyFunc: GetKeyFunction): Section[] {
        const sections : Section[] = [];
        const sectionsMap = new Map<string, Section>(); 

        tests.forEach((test) => {
            const [ key, fullKey ] = keyFunc(test);
            let targetSection = sectionsMap.get(key);
            if(!targetSection) {
                targetSection = new Section(key, fullKey, type);
                sections.push(targetSection);
                sectionsMap.set(key, targetSection);
            }
            targetSection.addTest(test, this.fixTestName(test.Name));
        });

        return sections;
    }

    private  fixTestName(name: string): string {
        // It is necessary to sanitize the test names that contains / instead of . syntax
        if(this.fixedTestNameCache.has(name)) {
            return this.fixedTestNameCache.get(name)!;
        }
        let fixedName = name
        if(name.indexOf('/') > -1) {
            fixedName = name.replaceAll(/\.*\//g, ".");
        }
        this.fixedTestNameCache.set(name, fixedName);
        return fixedName;
    }

    private getCommonString(list: string[]): string {
        if(list.length === 0) {
            return "";
        }
        let shortest: string = list[0];
        let longest: string = "";
        list.forEach((item) => {
            if(item > longest) {
                longest = item;
                return;
            }
            if(item < shortest)
            {
                shortest = item;
            }
        });
        while(longest.indexOf(shortest) !== 0) {
            shortest = shortest.slice(0, -1)
        }

        return shortest;
    }

    private getCommonSection(list: string[]): string {
        let commonKey = this.getCommonString(list);
        if(commonKey[commonKey.length - 1] !== '.' && commonKey.indexOf('.') >= 0) {
            commonKey = commonKey.slice(0, commonKey.lastIndexOf('.') + 1);
        }

        return commonKey;
    }

    // sort comparing the number of Failed, Incomplete, Unexecuted and Passed
    private sortSections(sections: Section[]) {
        sections.sort((a, b) => {
            if (a.Stats.Failed > b.Stats.Failed) {
                return -1;
            } else if (a.Stats.Failed < b.Stats.Failed) {
                return 1;
            } else if (a.Stats.Incomplete > b.Stats.Incomplete) {
                return -1;
            } else if (a.Stats.Incomplete < b.Stats.Incomplete) {
                return 1;
            } else if (a.Stats.Unexecuted > b.Stats.Unexecuted) {
                return -1;
            } else if (a.Stats.Unexecuted < b.Stats.Unexecuted) {
                return 1;
            } else if (a.Stats.Passed > b.Stats.Passed) {
                return -1;
            } else if (a.Stats.Passed < b.Stats.Passed) {
                return 1;
            } else if (a.isOneTest && !b.isOneTest) {
                return 1;
            } else if (b.isOneTest && !a.isOneTest) {
                return -1;
            }
            return 0;
        });
    }

    // sort in that order Failed, Incomplete, Unexecuted, Passed, Skipped
    private sortSectionResults(section: Section) {
        section.Results.sort((a, b) => {
            if(a.State === TestState.Failed) {
                if(b.State !== TestState.Failed) {
                    return -1;
                }
            } else if(a.State === TestState.InProcess) {
                if(b.State === TestState.Failed) {
                    return 1;
                } else if(b.State !== TestState.InProcess) {
                    return -1;
                }
            } else if(a.State === TestState.NotRun) {
                if(b.State === TestState.Failed || b.State === TestState.InProcess) {
                    return 1;
                } else if(b.State !== TestState.NotRun) {
                    return -1;
                }
            } else if(a.State === TestState.Success) {
                if(b.State === TestState.Skipped) {
                    return -1;
                } else if(b.State !== TestState.Success || b.WarningCount > 0) {
                    return 1;
                } else if(a.WarningCount > 0) {
                    return -1;
                }
            } else if(a.State === TestState.Skipped) {
                if(b.State !== TestState.Skipped) {
                    return 1;
                }
            }

            if (a.Name > b.Name) {
                return 1;
            } else if (a.Name < b.Name) {
                return -1;
            }

            return 0;
        })
    }
}

type SessionReferences = {
    SessionId: string;
    ResultsId: string;
}

export class TestSessionCollection {

    @observable
    version: number = 0;
    @action
    updateVersion() {
        this.version++;
    }
    reactOnVersionChange(callback: (version: number) => void) {
        return reaction(
            () => this.version,
            (version) => callback(version)
        );
    }

    @observable
    loadingCount: number = 0;
    @action
    incrementLoading() {
        this.loadingCount++;
    }
    @action
    decrementLoading() {
        this.loadingCount--;
    }

    get size(): number { return this._collection.size; }
    get hash(): string {
        let aggregate = "";
        this.forEachSession( // only include fully loaded session
            (session) => aggregate += session.isAllPropertiesLoaded()?session.getUID():""
        );
        return generatetHashFromString(aggregate);
    }

    private _collection: Map<string, TestSessionWrapper>;
    private _datahandler: TestDataCollection;
    private _includePreflight: boolean = false;

    constructor(handler: TestDataCollection, includePreflight: boolean = false) {
        this._collection = new Map();
        this._datahandler = handler;
        this._includePreflight = includePreflight;
    }

    set(uid: string, item: TestSessionWrapper) {
        this._collection.set(uid, item);
        if(item.isAllPropertiesLoaded()) { this.updateVersion(); }
    }

    get(uid: string) {
        return this._collection.get(uid);
    }

    has(uid: string) {
        return this._collection.has(uid);
    }

    delete(uid: string) {
        this._collection.delete(uid);
        this.updateVersion();
    }

    clear() {
        this._collection.clear()
        this.updateVersion();
    }

    forEachSession(func: (item: TestSessionWrapper) => void) {
        this._collection.forEach(func);
    }

    *iterSessions() {
        const iterator = this._collection.values();
        let result = iterator.next();
        while(!result.done) {
            yield result.value;
            result = iterator.next();
        }
    }

    *iterTests() {
        const iterSession = this._collection.values();
        let resultSession = iterSession.next();
        while(!resultSession.done) {
            const sessionInfo = resultSession.value.TestSessionInfo;
            if (sessionInfo) {
                for (const key in sessionInfo.Tests) {
                    yield sessionInfo.Tests[key];
                }
            }
            resultSession = iterSession.next();
        }
    }

    async getSessionsForStream(streamId: string, maxCount: number = 1000) {
        this.incrementLoading();
        const onFetch = (items: (TestDataWrapper | undefined)[]) => this.onInitialFetch(items);
        try {
            if(streamId === this._datahandler.streamId
                && this._datahandler.items
                && this._datahandler.activated
                && maxCount > this._datahandler.items.size) {
                    // Max count resize
                    await this._datahandler.queueFetchItems(onFetch, SessionFields.initiate, maxCount);
            } else {
                // Initiale fetch or reload
                await this._datahandler.setFromStream(
                    streamId, "Automated Test Session", onFetch, SessionFields.initiate, maxCount
                );
            }
        } finally { this.decrementLoading() }
        await when(() => this.loadingCount === 0);
    }

    private onInitialFetch(items: (TestDataWrapper|undefined)[]) {
        const sessionsToFetch: string[] = [];
        items.forEach((item) => {
            if (item) {
                let data = item.getDataHandler() as TestSessionWrapper;
                if(!data) {
                    data = new TestSessionWrapper(item);
                    item.setDataHandler(data);
                }
                if (!this._includePreflight && data.PreFlightChange) {
                    return;
                }
                const session_uid = data.getUID();
                if(!this.has(session_uid)) {
                    this.set(session_uid, data);
                    if (!data.isAllPropertiesLoaded()) {
                        sessionsToFetch.push(item.id);
                    }
                }
            }
        });
        if(sessionsToFetch.length > 0) {
            this.incrementLoading();
            this._datahandler.fetchUpdateItems(
                sessionsToFetch, (items) => this.onRemainingFetch(items), SessionFields.remaining
            ).finally(() => this.decrementLoading());
        }
    }

    private onRemainingFetch(items: (TestDataWrapper|undefined)[]) {
        let hasUpdated = false;
        items.forEach((item) => {
            if (item) {
                let data = item.getDataHandler() as TestSessionWrapper;
                if(data && data.isAllPropertiesLoaded()) {
                    hasUpdated = true;
                }
            }
        });
        if(hasUpdated) { this.updateVersion() }
    }

    async getTestResultByUID(uid: string): Promise<TestResult[]> {
        const iterator = this._collection.values();
        const tests: TestResult[] = [];
        const toFetch: TestSessionWrapper[] = [];
        let item = iterator.next();
        while(!item.done) {
            const session = item.value;
            if (!session.isAllPropertiesLoaded()) {
                await session.whenPropertyLoaded('TestSessionInfo');
            }
            const test = session.getTestResultByUID(uid);
            if (test) {
                if (test.Details) {
                    tests.push(test);
                } else {
                    toFetch.push(session);
                }
            }
            item = iterator.next();
        }

        if(tests.length === 0 && this.loadingCount !== 0) {
            // Give a chance to the collection to be fully loaded if nothing was found
            // It is possible the test is run infrequently and is deep in the result collection
            await when(() => this.loadingCount === 0);
            // then retry
            return await this.getTestResultByUID(uid);
        }

        if(toFetch.length > 0) {
            // Fetch missing data from session test results
            await this.fetchSessionResults(
                toFetch.map((item) => {
                    return {SessionId: item.Testdata.id, ResultsId: item.TestSessionInfo.TestResultsTestDataUID}
                }), 'data.' + uid
            );
            toFetch.forEach((item) => {
                const test = item.getTestResultByUID(uid);
                if(test?.Details) {
                    tests.push(test);
                }
            });
        }

        return tests;
    }

    async getTestResultByQuery(uid: string, metauid: string, change: number): Promise<TestResult | undefined> {
        const latestTests = await this.getTestResultByUID(uid);
        let test: TestResult | undefined = undefined;

        if(latestTests.length > 0) {
            test = latestTests.find((test) => test.Session.Testdata.change === change && test.Session.MetaHandler?.hash === metauid);
            if(!test) {
                // Need to get the target change from the history
                const sessionName = latestTests[0].Session.Name;
                const changes = await this.getSessionHistory(sessionName);
                const sessionsChange = changes.find((items) => items[0].Testdata.change === change);
                const session = sessionsChange?.find((item) => item.MetaHandler?.hash === metauid);
                test = session?.getTestResultByUID(uid);
            }
        }

        if(test && !test.Details) {
            // Fetch missing data from the session test result
            await this.fetchSessionResults(
                [{SessionId: test.Session.Testdata.id, ResultsId: test.Session.TestSessionInfo.TestResultsTestDataUID}]
                , 'data.' + uid
            );
            if(!test.Details) {
                // if nothing was fetched or failed to fetch, return nothing
                return undefined;
            }
        }

        return test;
    }

    private async fetchSessionResults(items: SessionReferences[], filter?: string, parallelCount: number = 30) {
        let count = 0;
        for (let i = 0; i < items.length; i += parallelCount) {
            if (!this._datahandler.activated) {
                break;
            }
            const chunk = items.slice(i, i + parallelCount);
            count += (
                await Promise.all(chunk.map((item) => this.fetchResultItemFromSession(item, filter)))
            ).reduce((a, b) => a + b);
        }
        return count;
    }

    private async fetchResultItemFromSession(item: SessionReferences, filter?: string): Promise<number> {
        const update = await this._datahandler.fetchItemsFromAlternateKey(`${this._datahandler.key} Result Details::${item.ResultsId}`, undefined, 1, 0, filter);
        // We only expect one returned item
        if(update.length > 0) {
            const results = update[0].data as TestResultDetailsMap;
            const session = this._datahandler.items?.get(item.SessionId)?.getDataHandler() as TestSessionWrapper;
            if(session) {
                for(const uid in results ) {
                    const test = session.getTestResultByUID(uid);
                    if (test) {
                        test.Details = results[uid];
                    }
                }
                return 1;
            }
        }

        return 0;
    }

    getTests() {
        const tests: TestResult[] = [];
        const iterator = this._collection.values();
        let result = iterator.next();
        while(!result.done) {
            const sessionInfo = result.value.TestSessionInfo;
            if(sessionInfo) {
                tests.push(...Object.values(result.value.TestSessionInfo.Tests));
            }
            result = iterator.next();
        }
        return tests;
    }

    async getSessionHistory(sessionName: string, maxCount: number = 1000) {
        if(this._datahandler.cursor) {
            // for single view mode we need to fetch the whole pool for context
            const onFetch = (items: (TestDataWrapper|undefined)[]) => this.onInitialHistoryFetch(items)
            await this._datahandler.getCursorHistory(onFetch, SessionFields.initiate, maxCount)
        }

        const missingSessions: string[] = [];
        if(this.loadingCount !== 0) {
            // wait for the current collection to have loaded
            await when(() => this.loadingCount === 0);
        }

        const relatedSessionsByChange: TestSessionWrapper[][] = [];
        const iterator = this._datahandler.iterItemsByChange();
        let change = iterator.next();
        while (!change.done) {
            const sessions: TestSessionWrapper[] = [];
            change.value.forEach((item) => {
                const session = item.getDataHandler() as TestSessionWrapper;
                if(session.Name === sessionName) {
                    if (!this._includePreflight && session.PreFlightChange) {
                        return;
                    }
                    sessions.push(session);
                    if(!session.isAllPropertiesLoaded()) {
                        missingSessions.push(session.Testdata.id);
                    }
                }
            });
            if (sessions.length > 0) {
                relatedSessionsByChange.push(sessions);
            }
            change = iterator.next();
        }

        if(missingSessions.length > 0) {
            await this._datahandler.fetchUpdateItems(
                missingSessions, undefined, SessionFields.remaining
            )
        }
        return relatedSessionsByChange;
    }

    private onInitialHistoryFetch(items: (TestDataWrapper|undefined)[]) {
        items.forEach((item) => {
            if (item) {
                let data = item.getDataHandler() as TestSessionWrapper;
                if(!data) {
                    data = new TestSessionWrapper(item);
                    item.setDataHandler(data);
                }
            }
        });
    }

}

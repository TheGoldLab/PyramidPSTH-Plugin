#ifndef PYRAMID_RULE_ENGINE_H_
#define PYRAMID_RULE_ENGINE_H_

#include <JuceHeader.h>

#include <atomic>

class PyramidRuleEngine
{
public:
    enum class CodeType
    {
        time,
        id,
        value,
        unknown
    };

    enum class Operator
    {
        exists,
        equals,
        notEquals,
        contains,
        greaterThan,
        lessThan
    };

    struct Rule
    {
        String conditionName;
        String codeKey;
        CodeType codeType = CodeType::unknown;
        Operator op = Operator::exists;
        String expectedValue;
        int lookbackMs = 1500;
    };

    struct ParsedEvent
    {
        int64 sequenceNumber = 0;
        int64 systemTimeMs = 0;
        int64 sampleNumber = 0;
        String raw;
        StringPairArray fields;
    };

    struct EvalContext
    {
        int64 currentSystemTimeMs = 0;
        int64 ttlSampleNumber = 0;
    };

    struct Health
    {
        int64 eventsSeen = 0;
        int64 parseFailures = 0;
        int64 droppedEvents = 0;
        int64 rulesLoaded = 0;
        int64 evalErrors = 0;
    };

    PyramidRuleEngine();

    bool loadRulesFromCsvFiles (const Array<File>& files, String& statusMessage);
    void setManualRules (const Array<Rule>& newManualRules);
    void reset();

    void ingestTextEvent (const String& text, int64 sampleNumber, int64 systemTimeMs);
    bool evaluateCondition (const String& conditionName, const EvalContext& context, bool& matched, String& errorMessage);
    bool evaluateConditionInWindow (const String& conditionName,
                                    int64 startSequence,
                                    int64 endSequence,
                                    int64 startSystemTimeMs,
                                    int64 endSystemTimeMs,
                                    int preTrialBufferMs,
                                    bool& matched,
                                    String& errorMessage) const;
    bool evaluateConditionInWindow (const String& conditionName,
                                    int64 startSequence,
                                    int64 endSequence,
                                    int64 startSampleNumber,
                                    int64 endSampleNumber,
                                    int64 preTrialBufferSamples,
                                    bool& matched,
                                    String& errorMessage) const;
    bool findNearestMatchingEventSystemTime (const String& conditionName,
                                             int64 referenceSystemTimeMs,
                                             int maxAbsLagMs,
                                             int64& matchedEventSystemTimeMs) const;
    bool findNearestMatchingEventInWindow (const String& conditionName,
                                           int64 referenceSystemTimeMs,
                                           int64 startSequence,
                                           int64 endSequence,
                                           int64 startSystemTimeMs,
                                           int64 endSystemTimeMs,
                                           int preTrialBufferMs,
                                           ParsedEvent& matchedEvent) const;
    bool findNearestMatchingEventInWindow (const String& conditionName,
                                           int64 referenceSampleNumber,
                                           int64 startSequence,
                                           int64 endSequence,
                                           int64 startSampleNumber,
                                           int64 endSampleNumber,
                                           int64 preTrialBufferSamples,
                                           ParsedEvent& matchedEvent) const;

    Array<String> getConditionNames() const;
    const Array<Rule>& getManualRules() const { return manualRules; }
    Health getHealth() const;
    int64 getLatestParsedSequence() const;

private:
    bool parseCsvRuleLine (const StringArray& columns, Rule& rule) const;
    CodeType parseCodeType (const String& text) const;
    Operator parseOperator (const String& text) const;

    bool parseEvent (const String& text, int64 sampleNumber, int64 systemTimeMs, ParsedEvent& parsed) const;
    bool parseEventAsJson (const String& text, ParsedEvent& parsed) const;
    void parseEventAsKeyValue (const String& text, ParsedEvent& parsed) const;
    bool tryExtractSampleNumberFromEventFields (ParsedEvent& parsed) const;

    bool evaluateRule (const Rule& rule, const EvalContext& context, bool& matched) const;
    bool evaluateAgainstEvent (const Rule& rule, const ParsedEvent& event, bool& matched) const;
    bool conditionMatchesAnyEventInWindow (const String& conditionName,
                                           int64 startSequence,
                                           int64 endSequence,
                                           int64 startSystemTimeMs,
                                           int64 endSystemTimeMs,
                                           int preTrialBufferMs) const;
    bool conditionMatchesAnyEventInWindow (const String& conditionName,
                                           int64 startSequence,
                                           int64 endSequence,
                                           int64 startSampleNumber,
                                           int64 endSampleNumber,
                                           int64 preTrialBufferSamples) const;
    bool eventInWindow (const ParsedEvent& event,
                        int64 startSequence,
                        int64 endSequence,
                        int64 startSystemTimeMs,
                        int64 endSystemTimeMs,
                        int preTrialBufferMs) const;
    bool eventInWindow (const ParsedEvent& event,
                        int64 startSequence,
                        int64 endSequence,
                        int64 startSampleNumber,
                        int64 endSampleNumber,
                        int64 preTrialBufferSamples) const;

    bool tryGetFieldValueCaseInsensitive (const ParsedEvent& event,
                                          const String& key,
                                          String& value) const;
    String normalize (const String& value) const;
    void collectAllRulesForCondition (const String& conditionName, Array<Rule>& conditionRules) const;

    Array<Rule> rules;
    Array<Rule> manualRules;
    Array<ParsedEvent> recentEvents;

    int maxEvents = 2048;

    std::atomic<int64> eventsSeen { 0 };
    std::atomic<int64> parseFailures { 0 };
    std::atomic<int64> droppedEvents { 0 };
    std::atomic<int64> rulesLoaded { 0 };
    std::atomic<int64> evalErrors { 0 };
    std::atomic<int64> parsedEventSequence { 0 };

    StringPairArray aliasNameToCodeValue;
    StringPairArray aliasCodeValueToName;
};

#endif

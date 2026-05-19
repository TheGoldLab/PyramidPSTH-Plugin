#include "PyramidRuleEngine.h"

PyramidRuleEngine::PyramidRuleEngine() {}

bool PyramidRuleEngine::loadRulesFromCsvFiles (const Array<File>& files, String& statusMessage)
{
    reset();

    if (files.isEmpty())
    {
        statusMessage = "No CSV files selected.";
        return false;
    }

    int linesParsed = 0;
    bool sawDefinitionTable = false;
    bool sawRuleTable = false;

    for (const auto& file : files)
    {
        if (! file.existsAsFile())
            continue;

        StringArray lines;
        lines.addLines (file.loadFileAsString());

        bool isRuleTable = false;
        bool isDefinitionTable = false;

        for (const auto& line : lines)
        {
            const String headerCandidate = line.trim();

            if (headerCandidate.isEmpty() || headerCandidate.startsWithChar ('#'))
                continue;

            if (headerCandidate.containsIgnoreCase ("condition_name") && headerCandidate.containsIgnoreCase ("code_key"))
            {
                isRuleTable = true;
                sawRuleTable = true;
            }
            else if (headerCandidate.startsWithIgnoreCase ("type,value,name"))
            {
                isDefinitionTable = true;
                sawDefinitionTable = true;
            }

            break;
        }

        for (int i = 0; i < lines.size(); ++i)
        {
            const String trimmed = lines[i].trim();

            if (trimmed.isEmpty() || trimmed.startsWithChar ('#'))
                continue;

            StringArray columns;
            columns.addTokens (trimmed, ",", "\"");
            columns.trim();

            if (columns.size() < 3)
                continue;

            if (i == 0)
                continue;

            Rule rule;

            if (isRuleTable && parseCsvRuleLine (columns, rule))
            {
                rules.add (rule);
                linesParsed++;
            }
            else if (isDefinitionTable)
            {
                const String type = columns[0].trim();
                const String codeValue = columns[1].trim();
                const String name = columns[2].trim();

                if (name.isNotEmpty() && codeValue.isNotEmpty())
                {
                    const String normalizedName = normalize (name);
                    const String normalizedCodeValue = normalize (codeValue);

                    if (aliasNameToCodeValue[normalizedName].isEmpty())
                        aliasNameToCodeValue.set (normalizedName, normalizedCodeValue);

                    if (aliasCodeValueToName[normalizedCodeValue].isEmpty())
                        aliasCodeValueToName.set (normalizedCodeValue, normalizedName);
                }

                rule.conditionName = name;
                rule.codeKey = "name";
                rule.codeType = parseCodeType (type);
                rule.op = Operator::equals;
                rule.expectedValue = name;

                if (rule.conditionName.isNotEmpty() && rule.codeType != CodeType::unknown)
                {
                    rules.add (rule);
                    linesParsed++;

                    if (codeValue.isNotEmpty())
                    {
                        Rule numericNameRule = rule;
                        numericNameRule.codeKey = "name";
                        numericNameRule.op = Operator::equals;
                        numericNameRule.expectedValue = codeValue;
                        rules.add (numericNameRule);
                        linesParsed++;

                        Rule numericRule = rule;
                        numericRule.codeKey = "value";
                        numericRule.op = Operator::equals;
                        numericRule.expectedValue = codeValue;
                        rules.add (numericRule);
                        linesParsed++;
                    }
                }
            }
        }
    }

    rulesLoaded = rules.size() + manualRules.size();
    const int uniqueConditions = getConditionNames().size();

    if (rulesLoaded > 0)
    {
        if (sawRuleTable)
            statusMessage = "Loaded " + String (uniqueConditions) + " conditions (" + String (rulesLoaded.load()) + " rule rows) from rule CSV.";
        else if (sawDefinitionTable)
            statusMessage = "Loaded " + String (uniqueConditions) + " conditions (" + String (rulesLoaded.load()) + " rule rows incl. aliases) from ecode definition CSV.";
        else
            statusMessage = "Loaded " + String (uniqueConditions) + " conditions (" + String (rulesLoaded.load()) + " rule rows).";
    }
    else
    {
        statusMessage = "No valid rules found. Expected rule header: condition_name,code_key,code_type,... or ecode header: type,value,name,...";
    }

    return rules.size() > 0;
}

void PyramidRuleEngine::setManualRules (const Array<Rule>& newManualRules)
{
    manualRules.clearQuick();

    for (const auto& rule : newManualRules)
    {
        if (rule.conditionName.isNotEmpty() && rule.codeKey.isNotEmpty())
            manualRules.add (rule);
    }

    rulesLoaded = rules.size() + manualRules.size();
}

void PyramidRuleEngine::reset()
{
    rules.clear();
    manualRules.clear();
    recentEvents.clear();
    aliasNameToCodeValue.clear();
    aliasCodeValueToName.clear();
    eventsSeen = 0;
    parseFailures = 0;
    droppedEvents = 0;
    rulesLoaded = 0;
    evalErrors = 0;
    parsedEventSequence = 0;
    sampleOverrideCount = 0;
    sampleOverrideLastValue = -1;
}

void PyramidRuleEngine::ingestTextEvent (const String& text, int64 sampleNumber, int64 systemTimeMs)
{
    eventsSeen++;

    ParsedEvent parsed;

    if (! parseEvent (text, sampleNumber, systemTimeMs, parsed))
    {
        parseFailures++;
        return;
    }

    if (sampleNumber >= 0 && parsed.sampleNumber >= 0 && parsed.sampleNumber != sampleNumber)
    {
        sampleOverrideCount++;
        sampleOverrideLastValue = parsed.sampleNumber;
    }

    parsed.sequenceNumber = ++parsedEventSequence;
    recentEvents.add (parsed);

    if (recentEvents.size() > maxEvents)
    {
        recentEvents.remove (0);
        droppedEvents++;
    }
}

bool PyramidRuleEngine::evaluateCondition (const String& conditionName, const EvalContext& context, bool& matched, String& errorMessage)
{
    matched = false;

    Array<Rule> conditionRules;
    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    bool anyRuleMatched = false;

    for (const auto& rule : conditionRules)
    {
        bool thisRuleMatched = false;

        if (! evaluateRule (rule, context, thisRuleMatched))
        {
            evalErrors++;
            errorMessage = "Rule evaluation failed for condition " + conditionName;
            return false;
        }

        anyRuleMatched = anyRuleMatched || thisRuleMatched;
    }

    matched = anyRuleMatched;
    return true;
}

bool PyramidRuleEngine::evaluateConditionInWindow (const String& conditionName,
                                                   int64 startSequence,
                                                   int64 endSequence,
                                                   int64 startSystemTimeMs,
                                                   int64 endSystemTimeMs,
                                                   int preTrialBufferMs,
                                                   bool& matched,
                                                   String& errorMessage) const
{
    ignoreUnused (errorMessage);
    matched = false;

    if (conditionName.isEmpty() || startSequence > endSequence)
        return true;

    matched = conditionMatchesAnyEventInWindow (conditionName,
                                                startSequence,
                                                endSequence,
                                                startSystemTimeMs,
                                                endSystemTimeMs,
                                                preTrialBufferMs);
    return true;
}

bool PyramidRuleEngine::evaluateConditionInWindow (const String& conditionName,
                                                   int64 startSequence,
                                                   int64 endSequence,
                                                   int64 startSampleNumber,
                                                   int64 endSampleNumber,
                                                   int64 preTrialBufferSamples,
                                                   bool& matched,
                                                   String& errorMessage) const
{
    ignoreUnused (errorMessage);
    matched = false;

    if (conditionName.isEmpty() || startSequence > endSequence)
        return true;

    matched = conditionMatchesAnyEventInWindow (conditionName,
                                                startSequence,
                                                endSequence,
                                                startSampleNumber,
                                                endSampleNumber,
                                                preTrialBufferSamples);
    return true;
}

bool PyramidRuleEngine::findNearestMatchingEventSystemTime (const String& conditionName,
                                                            int64 referenceSystemTimeMs,
                                                            int maxAbsLagMs,
                                                            int64& matchedEventSystemTimeMs) const
{
    matchedEventSystemTimeMs = 0;

    if (conditionName.isEmpty() || maxAbsLagMs <= 0)
        return false;

    Array<Rule> conditionRules;
    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    bool found = false;
    int64 bestAbsLag = std::numeric_limits<int64>::max();

    for (const auto& event : recentEvents)
    {
        const int64 lagMs = referenceSystemTimeMs - event.systemTimeMs;
        const int64 absLagMs = std::llabs (lagMs);

        if (absLagMs > maxAbsLagMs)
            continue;

        bool matched = false;
        for (const auto& rule : conditionRules)
        {
            bool ruleMatched = false;
            if (evaluateAgainstEvent (rule, event, ruleMatched) && ruleMatched)
            {
                matched = true;
                break;
            }
        }

        if (! matched)
            continue;

        if (absLagMs < bestAbsLag)
        {
            bestAbsLag = absLagMs;
            matchedEventSystemTimeMs = event.systemTimeMs;
            found = true;
        }
    }

    return found;
}

bool PyramidRuleEngine::findNearestMatchingEventInWindow (const String& conditionName,
                                                           int64 referenceSystemTimeMs,
                                                           int64 startSequence,
                                                           int64 endSequence,
                                                           int64 startSystemTimeMs,
                                                           int64 endSystemTimeMs,
                                                           int preTrialBufferMs,
                                                           ParsedEvent& matchedEvent) const
{
    matchedEvent = ParsedEvent();

    if (conditionName.isEmpty() || startSequence > endSequence)
        return false;

    Array<Rule> conditionRules;
    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    bool found = false;
    int64 bestSequenceNumber = -1;

    for (const auto& event : recentEvents)
    {
        if (! eventInWindow (event,
                             startSequence,
                             endSequence,
                             startSystemTimeMs,
                             endSystemTimeMs,
                             preTrialBufferMs))
        {
            continue;
        }

        bool matched = false;
        for (const auto& rule : conditionRules)
        {
            bool ruleMatched = false;
            if (evaluateAgainstEvent (rule, event, ruleMatched) && ruleMatched)
            {
                matched = true;
                break;
            }
        }

        if (! matched)
            continue;

        // Prefer the most recently received event (highest sequence number)
        if (event.sequenceNumber > bestSequenceNumber)
        {
            bestSequenceNumber = event.sequenceNumber;
            matchedEvent = event;
            found = true;
        }
    }

    return found;
}

bool PyramidRuleEngine::findNearestMatchingEventInWindow (const String& conditionName,
                                                           int64 referenceSampleNumber,
                                                           int64 startSequence,
                                                           int64 endSequence,
                                                           int64 startSampleNumber,
                                                           int64 endSampleNumber,
                                                           int64 preTrialBufferSamples,
                                                           ParsedEvent& matchedEvent) const
{
    matchedEvent = ParsedEvent();

    if (conditionName.isEmpty() || startSequence > endSequence)
        return false;

    Array<Rule> conditionRules;
    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    bool found = false;
    int64 bestSequenceNumber = -1;

    for (const auto& event : recentEvents)
    {
        if (! eventInWindow (event,
                             startSequence,
                             endSequence,
                             startSampleNumber,
                             endSampleNumber,
                             preTrialBufferSamples))
        {
            continue;
        }

        bool matched = false;
        for (const auto& rule : conditionRules)
        {
            bool ruleMatched = false;
            if (evaluateAgainstEvent (rule, event, ruleMatched) && ruleMatched)
            {
                matched = true;
                break;
            }
        }

        if (! matched)
            continue;

        // Prefer the most recently received event (highest sequence number)
        if (event.sequenceNumber > bestSequenceNumber)
        {
            bestSequenceNumber = event.sequenceNumber;
            matchedEvent = event;
            found = true;
        }
    }

    return found;
}

Array<String> PyramidRuleEngine::getConditionNames() const
{
    Array<String> names;

    for (const auto& rule : rules)
    {
        if (! names.contains (rule.conditionName))
            names.add (rule.conditionName);
    }

    for (const auto& rule : manualRules)
    {
        if (! names.contains (rule.conditionName))
            names.add (rule.conditionName);
    }

    return names;
}

int PyramidRuleEngine::getRuleCountForCondition (const String& conditionName) const
{
    if (conditionName.trim().isEmpty())
        return 0;

    Array<Rule> conditionRules;
    collectAllRulesForCondition (conditionName.trim(), conditionRules);
    return conditionRules.size();
}

PyramidRuleEngine::Health PyramidRuleEngine::getHealth() const
{
    Health health;
    health.eventsSeen = eventsSeen.load();
    health.parseFailures = parseFailures.load();
    health.droppedEvents = droppedEvents.load();
    health.rulesLoaded = rulesLoaded.load();
    health.evalErrors = evalErrors.load();
    health.sampleOverrideCount = sampleOverrideCount.load();
    health.sampleOverrideLastValue = sampleOverrideLastValue.load();
    return health;
}

int64 PyramidRuleEngine::getLatestParsedSequence() const
{
    return parsedEventSequence.load();
}

bool PyramidRuleEngine::parseCsvRuleLine (const StringArray& columns, Rule& rule) const
{
    rule.conditionName = columns[0].trim();
    rule.codeKey = columns[1].trim();
    rule.codeType = parseCodeType (columns[2].trim());

    if (columns.size() > 3)
        rule.op = parseOperator (columns[3].trim());

    if (columns.size() > 4)
        rule.expectedValue = columns[4].trim();

    if (columns.size() > 5)
        rule.lookbackMs = columns[5].getIntValue();

    return rule.conditionName.isNotEmpty() && rule.codeKey.isNotEmpty() && rule.codeType != CodeType::unknown;
}

PyramidRuleEngine::CodeType PyramidRuleEngine::parseCodeType (const String& text) const
{
    if (text.equalsIgnoreCase ("time"))
        return CodeType::time;
    if (text.equalsIgnoreCase ("id"))
        return CodeType::id;
    if (text.equalsIgnoreCase ("value"))
        return CodeType::value;

    return CodeType::unknown;
}

PyramidRuleEngine::Operator PyramidRuleEngine::parseOperator (const String& text) const
{
    if (text.equalsIgnoreCase ("=") || text.equalsIgnoreCase ("==") || text.equalsIgnoreCase ("equals"))
        return Operator::equals;
    if (text.equalsIgnoreCase ("!=") || text.equalsIgnoreCase ("not_equals"))
        return Operator::notEquals;
    if (text.equalsIgnoreCase ("contains"))
        return Operator::contains;
    if (text.equalsIgnoreCase (">") || text.equalsIgnoreCase ("gt"))
        return Operator::greaterThan;
    if (text.equalsIgnoreCase ("<") || text.equalsIgnoreCase ("lt"))
        return Operator::lessThan;

    return Operator::exists;
}

bool PyramidRuleEngine::parseEvent (const String& text, int64 sampleNumber, int64 systemTimeMs, ParsedEvent& parsed) const
{
    String cleanedText = text.trim();

    if (cleanedText.startsWith ("b\"") && cleanedText.endsWithChar ('\"'))
        cleanedText = cleanedText.substring (2, cleanedText.length() - 1);
    else if (cleanedText.startsWith ("b'") && cleanedText.endsWithChar ('\''))
        cleanedText = cleanedText.substring (2, cleanedText.length() - 1);

    parsed.raw = cleanedText;
    parsed.sampleNumber = sampleNumber;
    parsed.systemTimeMs = systemTimeMs;

    if (parseEventAsJson (cleanedText, parsed))
    {
        tryExtractSampleNumberFromEventFields (parsed);
        return true;
    }

    parseEventAsKeyValue (cleanedText, parsed);
    tryExtractSampleNumberFromEventFields (parsed);
    return parsed.fields.size() > 0;
}

bool PyramidRuleEngine::parseEventAsJson (const String& text, ParsedEvent& parsed) const
{
    auto parsedJson = JSON::parse (text);

    if (! parsedJson.isObject())
        return false;

    DynamicObject* object = parsedJson.getDynamicObject();

    if (object == nullptr)
        return false;

    for (const auto& property : object->getProperties())
    {
        parsed.fields.set (property.name.toString(), property.value.toString());
    }

    return parsed.fields.size() > 0;
}

void PyramidRuleEngine::parseEventAsKeyValue (const String& text, ParsedEvent& parsed) const
{
    String normalizedText = text;
    normalizedText = normalizedText.replace (";", ",");
    normalizedText = normalizedText.replace ("|", ",");

    StringArray tokens;
    tokens.addTokens (normalizedText, ",", "");

    for (const auto& token : tokens)
    {
        const int split = token.indexOfChar ('=');

        if (split <= 0)
            continue;

        const String key = token.substring (0, split).trim();
        String value = token.substring (split + 1).trim();

        const int atIndex = value.indexOfChar ('@');
        if (atIndex > 0)
        {
            const String suffix = value.substring (atIndex + 1).trim();
            String sampleCandidate = suffix;
            const int equalsIndex = sampleCandidate.lastIndexOfChar ('=');
            if (equalsIndex >= 0 && equalsIndex < sampleCandidate.length() - 1)
                sampleCandidate = sampleCandidate.substring (equalsIndex + 1).trim();

            if (sampleCandidate.containsOnly ("0123456789"))
            {
                const int64 suffixValue = sampleCandidate.getLargeIntValue();
                if (suffixValue > 0)
                    parsed.sampleNumber = suffixValue;
            }

            value = value.substring (0, atIndex).trim();
        }

        if (key.isNotEmpty())
            parsed.fields.set (key, value);
    }
}

bool PyramidRuleEngine::tryExtractSampleNumberFromEventFields (ParsedEvent& parsed) const
{
    auto parseNumericCandidate = [] (String candidate, int64& valueOut)
    {
        candidate = candidate.trim();
        if (candidate.isEmpty())
            return false;

        const int atIndex = candidate.lastIndexOfChar ('@');
        if (atIndex >= 0 && atIndex < candidate.length() - 1)
            candidate = candidate.substring (atIndex + 1).trim();

        if (candidate.containsOnly ("0123456789"))
        {
            valueOut = candidate.getLargeIntValue();
            return valueOut > 0;
        }

        String digits;
        bool started = false;
        for (int i = 0; i < candidate.length(); ++i)
        {
            const juce_wchar c = candidate[i];
            if (CharacterFunctions::isDigit (c))
            {
                digits += String::charToString (c);
                started = true;
            }
            else if (started)
            {
                break;
            }
        }

        if (digits.isNotEmpty())
        {
            valueOut = digits.getLargeIntValue();
            return valueOut > 0;
        }

        return false;
    };

    const StringArray candidateKeys {
        "sample_number",
        "sampleNumber",
        "sample_num",
        "sample",
        "event_sample",
        "oe_sample",
        "sample_index",
        "sampleIndex"
    };

    int64 extractedSample = -1;

    for (const auto& key : candidateKeys)
    {
        String value;
        if (tryGetFieldValueCaseInsensitive (parsed, key, value) && parseNumericCandidate (value, extractedSample))
        {
            parsed.sampleNumber = extractedSample;
            return true;
        }
    }

    return false;
}

bool PyramidRuleEngine::evaluateRule (const Rule& rule, const EvalContext& context, bool& matched) const
{
    matched = false;

    const int64 minTime = context.currentSystemTimeMs - rule.lookbackMs;

    for (int i = recentEvents.size() - 1; i >= 0; --i)
    {
        const auto& event = recentEvents.getReference (i);

        if (event.systemTimeMs < minTime)
            break;

        if (evaluateAgainstEvent (rule, event, matched) && matched)
            return true;
    }

    return true;
}

bool PyramidRuleEngine::evaluateAgainstEvent (const Rule& rule, const ParsedEvent& event, bool& matched) const
{
    matched = false;

    const String key = normalize (rule.codeKey);
    const String resolvedCodeValue = aliasNameToCodeValue[key].isNotEmpty()
                                       ? aliasNameToCodeValue[key]
                                       : (key.containsOnly ("0123456789") ? key : String());
    String eventName;
    String eventValue;
    const bool hasEventName = tryGetFieldValueCaseInsensitive (event, "name", eventName);
    const bool hasEventValue = tryGetFieldValueCaseInsensitive (event, "value", eventValue);

    auto matchesWithOperator = [&] (const String& foundValue,
                                    const String& expectedValue,
                                    Operator opToUse)
    {
        const String normalizedFound = normalize (foundValue);
        const String normalizedExpected = normalize (expectedValue);

        switch (opToUse)
        {
            case Operator::exists:
                return true;
            case Operator::equals:
                return normalizedFound == normalizedExpected;
            case Operator::notEquals:
                return normalizedFound != normalizedExpected;
            case Operator::contains:
                return normalizedFound.contains (normalizedExpected);
            case Operator::greaterThan:
                return foundValue.getDoubleValue() > expectedValue.getDoubleValue();
            case Operator::lessThan:
                return foundValue.getDoubleValue() < expectedValue.getDoubleValue();
            default:
                return false;
        }
    };

    if (resolvedCodeValue.isEmpty())
    {
        if ((key == "name" || key == "id") && hasEventName)
        {
            if (rule.expectedValue.isEmpty())
            {
                matched = (rule.op == Operator::exists);
                return true;
            }

            matched = matchesWithOperator (eventName, rule.expectedValue, rule.op);
            return true;
        }

        if (key == "value" && hasEventValue)
        {
            if (rule.expectedValue.isEmpty())
            {
                matched = (rule.op == Operator::exists);
                return true;
            }

            matched = matchesWithOperator (eventValue, rule.expectedValue, rule.op);
            return true;
        }
    }

    const bool codeMatches = resolvedCodeValue.isNotEmpty() && hasEventName && normalize (eventName) == resolvedCodeValue;

    if (rule.codeType == CodeType::time)
    {
        matched = false;

        if (codeMatches)
        {
            if (rule.expectedValue.isEmpty())
            {
                matched = true;
            }
            else if (hasEventValue)
            {
                matched = matchesWithOperator (eventValue, rule.expectedValue, rule.op);
            }
        }

        return true;
    }

    if (! codeMatches)
        return true;

    if (rule.expectedValue.isEmpty())
    {
        matched = true;
        return true;
    }

    if (! hasEventValue)
        return true;

    matched = matchesWithOperator (eventValue, rule.expectedValue, rule.op);

    return true;
}

bool PyramidRuleEngine::conditionMatchesAnyEventInWindow (const String& conditionName,
                                                          int64 startSequence,
                                                          int64 endSequence,
                                                          int64 startSystemTimeMs,
                                                          int64 endSystemTimeMs,
                                                          int preTrialBufferMs) const
{
    Array<Rule> conditionRules;

    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    for (const auto& event : recentEvents)
    {
        if (! eventInWindow (event,
                             startSequence,
                             endSequence,
                             startSystemTimeMs,
                             endSystemTimeMs,
                             preTrialBufferMs))
        {
            continue;
        }

        for (const auto& rule : conditionRules)
        {
            bool ruleMatched = false;
            if (evaluateAgainstEvent (rule, event, ruleMatched) && ruleMatched)
                return true;
        }
    }

    return false;
}

bool PyramidRuleEngine::conditionMatchesAnyEventInWindow (const String& conditionName,
                                                          int64 startSequence,
                                                          int64 endSequence,
                                                          int64 startSampleNumber,
                                                          int64 endSampleNumber,
                                                          int64 preTrialBufferSamples) const
{
    Array<Rule> conditionRules;

    collectAllRulesForCondition (conditionName, conditionRules);

    if (conditionRules.isEmpty())
        return false;

    for (const auto& event : recentEvents)
    {
        if (! eventInWindow (event,
                             startSequence,
                             endSequence,
                             startSampleNumber,
                             endSampleNumber,
                             preTrialBufferSamples))
        {
            continue;
        }

        for (const auto& rule : conditionRules)
        {
            bool ruleMatched = false;
            if (evaluateAgainstEvent (rule, event, ruleMatched) && ruleMatched)
                return true;
        }
    }

    return false;
}

bool PyramidRuleEngine::eventInWindow (const ParsedEvent& event,
                                       int64 startSequence,
                                       int64 endSequence,
                                       int64 startSystemTimeMs,
                                       int64 endSystemTimeMs,
                                       int preTrialBufferMs) const
{
    const int64 minSystemTime = startSystemTimeMs - jmax (0, preTrialBufferMs);
    const bool inSequenceWindow = event.sequenceNumber >= startSequence && event.sequenceNumber <= endSequence;
    const bool inBufferedTimeWindow = event.systemTimeMs >= minSystemTime && event.systemTimeMs <= endSystemTimeMs;
    return inSequenceWindow || inBufferedTimeWindow;
}

bool PyramidRuleEngine::eventInWindow (const ParsedEvent& event,
                                       int64 startSequence,
                                       int64 endSequence,
                                       int64 startSampleNumber,
                                       int64 endSampleNumber,
                                       int64 preTrialBufferSamples) const
{
    const int64 minSampleNumber = startSampleNumber - jmax<int64> (0, preTrialBufferSamples);
    const bool inSequenceWindow = event.sequenceNumber >= startSequence && event.sequenceNumber <= endSequence;
    const bool inBufferedSampleWindow = event.sampleNumber >= minSampleNumber && event.sampleNumber <= endSampleNumber;

    const bool hasValidSampleWindow = startSampleNumber >= 0 && endSampleNumber >= startSampleNumber;
    const bool eventHasSample = event.sampleNumber >= 0;

    if (hasValidSampleWindow && eventHasSample)
        return inBufferedSampleWindow;

    return inSequenceWindow || inBufferedSampleWindow;
}

String PyramidRuleEngine::normalize (const String& value) const
{
    return value.trim().toLowerCase();
}

bool PyramidRuleEngine::tryGetFieldValueCaseInsensitive (const ParsedEvent& event,
                                                         const String& key,
                                                         String& value) const
{
    const String normalizedKey = normalize (key);
    const StringArray fieldKeys = event.fields.getAllKeys();

    for (int i = 0; i < fieldKeys.size(); ++i)
    {
        const String fieldKey = fieldKeys[i];

        if (normalize (fieldKey) == normalizedKey)
        {
            value = event.fields.getValue (fieldKey, "");
            return true;
        }
    }

    value.clear();
    return false;
}

void PyramidRuleEngine::collectAllRulesForCondition (const String& conditionName, Array<Rule>& conditionRules) const
{
    conditionRules.clearQuick();

    for (const auto& rule : manualRules)
    {
        if (rule.conditionName.equalsIgnoreCase (conditionName))
            conditionRules.add (rule);
    }

    if (! conditionRules.isEmpty())
        return;

    for (const auto& rule : rules)
    {
        if (rule.conditionName.equalsIgnoreCase (conditionName))
            conditionRules.add (rule);
    }
}

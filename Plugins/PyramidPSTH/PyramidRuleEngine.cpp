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
    eventsSeen = 0;
    parseFailures = 0;
    droppedEvents = 0;
    rulesLoaded = 0;
    evalErrors = 0;
    parsedEventSequence = 0;
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
    int64 bestAbsLag = std::numeric_limits<int64>::max();

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

        const int64 lagMs = referenceSystemTimeMs - event.systemTimeMs;
        const int64 absLagMs = std::llabs (lagMs);
        if (absLagMs < bestAbsLag)
        {
            bestAbsLag = absLagMs;
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

PyramidRuleEngine::Health PyramidRuleEngine::getHealth() const
{
    Health health;
    health.eventsSeen = eventsSeen.load();
    health.parseFailures = parseFailures.load();
    health.droppedEvents = droppedEvents.load();
    health.rulesLoaded = rulesLoaded.load();
    health.evalErrors = evalErrors.load();
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
        return true;

    parseEventAsKeyValue (cleanedText, parsed);
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
            value = value.substring (0, atIndex).trim();

        if (key.isNotEmpty())
            parsed.fields.set (key, value);
    }
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
    String foundValue;

    const StringArray fieldKeys = event.fields.getAllKeys();

    for (int i = 0; i < fieldKeys.size(); ++i)
    {
        const String fieldKey = fieldKeys[i];

        if (normalize (fieldKey) == key)
        {
            foundValue = event.fields.getValue (fieldKey, "");
            break;
        }
    }

    if (rule.codeType == CodeType::time)
    {
        if (rule.op == Operator::equals && rule.expectedValue.isNotEmpty())
            matched = normalize (foundValue) == normalize (rule.expectedValue);
        else
            matched = foundValue.isNotEmpty();

        if (! matched)
        {
            const String normalizedExpected = normalize (rule.expectedValue.isNotEmpty() ? rule.expectedValue : rule.conditionName);
            const String normalizedRaw = normalize (event.raw);

            if (normalizedExpected.isNotEmpty())
                matched = normalizedRaw.contains (normalizedExpected);
        }

        return true;
    }

    if (foundValue.isEmpty())
    {
        const String normalizedExpected = normalize (rule.expectedValue.isNotEmpty() ? rule.expectedValue : rule.conditionName);
        const String normalizedRaw = normalize (event.raw);
        matched = normalizedExpected.isNotEmpty() && normalizedRaw.contains (normalizedExpected);
        return true;
    }

    const String normalizedFound = normalize (foundValue);
    const String normalizedExpected = normalize (rule.expectedValue);

    switch (rule.op)
    {
        case Operator::exists:
            matched = true;
            break;
        case Operator::equals:
            matched = normalizedFound == normalizedExpected;
            break;
        case Operator::notEquals:
            matched = normalizedFound != normalizedExpected;
            break;
        case Operator::contains:
            matched = normalizedFound.contains (normalizedExpected);
            break;
        case Operator::greaterThan:
            matched = foundValue.getDoubleValue() > rule.expectedValue.getDoubleValue();
            break;
        case Operator::lessThan:
            matched = foundValue.getDoubleValue() < rule.expectedValue.getDoubleValue();
            break;
        default:
            matched = false;
            break;
    }

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

String PyramidRuleEngine::normalize (const String& value) const
{
    return value.trim().toLowerCase();
}

void PyramidRuleEngine::collectAllRulesForCondition (const String& conditionName, Array<Rule>& conditionRules) const
{
    conditionRules.clearQuick();

    for (const auto& rule : rules)
    {
        if (rule.conditionName.equalsIgnoreCase (conditionName))
            conditionRules.add (rule);
    }

    for (const auto& rule : manualRules)
    {
        if (rule.conditionName.equalsIgnoreCase (conditionName))
            conditionRules.add (rule);
    }
}

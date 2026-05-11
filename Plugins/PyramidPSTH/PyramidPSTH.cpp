#include "PyramidPSTH.h"

namespace
{
String codeTypeToString (PyramidRuleEngine::CodeType codeType)
{
    switch (codeType)
    {
        case PyramidRuleEngine::CodeType::time: return "time";
        case PyramidRuleEngine::CodeType::id: return "id";
        case PyramidRuleEngine::CodeType::value: return "value";
        default: return "unknown";
    }
}

PyramidRuleEngine::CodeType codeTypeFromString (const String& text)
{
    if (text.equalsIgnoreCase ("time"))
        return PyramidRuleEngine::CodeType::time;
    if (text.equalsIgnoreCase ("id"))
        return PyramidRuleEngine::CodeType::id;
    if (text.equalsIgnoreCase ("value"))
        return PyramidRuleEngine::CodeType::value;
    return PyramidRuleEngine::CodeType::unknown;
}

String opToString (PyramidRuleEngine::Operator op)
{
    switch (op)
    {
        case PyramidRuleEngine::Operator::exists: return "exists";
        case PyramidRuleEngine::Operator::equals: return "equals";
        case PyramidRuleEngine::Operator::notEquals: return "not_equals";
        case PyramidRuleEngine::Operator::contains: return "contains";
        case PyramidRuleEngine::Operator::greaterThan: return "greater_than";
        case PyramidRuleEngine::Operator::lessThan: return "less_than";
        default: return "exists";
    }
}

PyramidRuleEngine::Operator opFromString (const String& text)
{
    if (text.equalsIgnoreCase ("equals"))
        return PyramidRuleEngine::Operator::equals;
    if (text.equalsIgnoreCase ("not_equals"))
        return PyramidRuleEngine::Operator::notEquals;
    if (text.equalsIgnoreCase ("contains"))
        return PyramidRuleEngine::Operator::contains;
    if (text.equalsIgnoreCase ("greater_than"))
        return PyramidRuleEngine::Operator::greaterThan;
    if (text.equalsIgnoreCase ("less_than"))
        return PyramidRuleEngine::Operator::lessThan;
    return PyramidRuleEngine::Operator::exists;
}
}

PyramidPSTH::PyramidPSTH()
    : GenericProcessor ("Pyramid PSTH")
{
}

void PyramidPSTH::registerParameters()
{
    addSelectedStreamParameter (Parameter::PROCESSOR_SCOPE,
                                "ttl_stream",
                                "TTL Stream",
                                "Data stream that provides trial and align TTL lines",
                                {},
                                0,
                                false,
                                false);

    addTtlLineParameter (Parameter::STREAM_SCOPE,
                         "trigger_line",
                         "Align line",
                         "TTL line reserved for PSTH alignment",
                         16);

    addTtlLineParameter (Parameter::STREAM_SCOPE,
                         "trial_line",
                         "Trial line",
                         "TTL line that marks trial start high and trial end low",
                         16);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "pre_trial_buffer_ms",
                     "Pre-trial buffer (ms)",
                     "Include condition events arriving shortly before Trial line rises",
                     preTrialBufferMs,
                     0,
                     2000);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "pre_ms",
                     "Pre MS",
                     "PSTH pre-window in milliseconds",
                     psthPreMs,
                     10,
                     5000);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "post_ms",
                     "Post MS",
                     "PSTH post-window in milliseconds",
                     psthPostMs,
                     10,
                     5000);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     "bin_size",
                     "Bin Size",
                     "PSTH bin width in milliseconds",
                     psthBinMs,
                     1,
                     1000);
}

AudioProcessorEditor* PyramidPSTH::createEditor()
{
    editor = std::make_unique<PyramidPSTHEditor> (this);
    return editor.get();
}

void PyramidPSTH::process (AudioBuffer<float>& buffer)
{
    ignoreUnused (buffer);
    refreshTtlStreamParameterOptions();
    resizePsthBinsIfNeeded();
    checkForEvents (true);
}

void PyramidPSTH::updateSettings()
{
    refreshTtlStreamParameterOptions();
}

void PyramidPSTH::handleBroadcastMessage (const String& message, const int64 sampleNumber)
{
    try
    {
        const int64 systemTimeMs = Time::currentTimeMillis();
        ruleEngine.ingestTextEvent (message, sampleNumber, systemTimeMs);
    }
    catch (...)
    {
        return;
    }
}

void PyramidPSTH::handleTTLEvent (TTLEventPtr event)
{
    if (event == nullptr)
        return;

    ttlSeen++;

    const DataStream* stream = getDataStream (event->getStreamId());
    if (stream == nullptr)
        return;

    uint16 configuredTtlStreamId = 0;
    if (getConfiguredTtlStreamId (configuredTtlStreamId)
        && event->getStreamId() != configuredTtlStreamId)
    {
        ttlFilteredOut++;
        return;
    }

    const int triggerLine = int ((*stream) ["trigger_line"]);
    const int trialLine = int ((*stream) ["trial_line"]);

    if (event->getLine() == triggerLine && event->getState())
    {
        alignmentSeen++;

        if ((trialActive || event->getLine() == trialLine) && activeAlignSample < 0)
        {
            activeAlignSample = event->getSampleNumber();
            activeAlignTimestampSeconds = event->getTimestampInSeconds();
        }
    }

    if (event->getLine() != trialLine)
        return;

    if (event->getState())
    {
        trialActive = true;
        activeTrialStreamId = event->getStreamId();
        activeTrialStartSequence = ruleEngine.getLatestParsedSequence() + 1;
        activeTrialStartSystemTimeMs = Time::currentTimeMillis();
        activeAlignSample = -1;
        activeAlignTimestampSeconds = -1.0;
        activeTrialSpikes.clearQuick();

        activeTrialSampleRate = stream->getSampleRate();

        const auto names = getConditionNamesForEvaluation();
        if (names.size() > 0)
        {
            int64 matchedEventSystemTimeMs = 0;
            if (ruleEngine.findNearestMatchingEventSystemTime (names[0],
                                                               activeTrialStartSystemTimeMs,
                                                               lagSearchWindowMs,
                                                               matchedEventSystemTimeMs))
            {
                const int lagMs = int (activeTrialStartSystemTimeMs - matchedEventSystemTimeMs);

                const ScopedLock lock (stateLock);
                lastLagMs = lagMs;
                hasLagSample = true;
                lagSampleCount++;
                const double delta = double (lagMs) - lagMeanMs;
                lagMeanMs += delta / double (lagSampleCount);
                const double delta2 = double (lagMs) - lagMeanMs;
                lagM2Ms += delta * delta2;
            }
        }

        trialStartsSeen++;
        return;
    }

    if (! trialActive)
        return;

    trialActive = false;
    trialEndsSeen++;

    const int configuredPreTrialBufferMs = int (getParameter ("pre_trial_buffer_ms")->getValue());
    const auto conditionNames = getConditionNamesForEvaluation();
    const int64 windowEndSystemTimeMs = Time::currentTimeMillis();

    if (trialFilterEnabled && trialFilterConditionName.isNotEmpty())
    {
        bool filterMatched = false;
        String errorMessage;
        bool ok = true;

        try
        {
            ok = ruleEngine.evaluateConditionInWindow (trialFilterConditionName,
                                                       activeTrialStartSequence,
                                                       ruleEngine.getLatestParsedSequence(),
                                                       activeTrialStartSystemTimeMs,
                                                       windowEndSystemTimeMs,
                                                       configuredPreTrialBufferMs,
                                                       filterMatched,
                                                       errorMessage);
        }
        catch (...)
        {
            ok = false;
        }

        if (! ok || ! filterMatched)
        {
            activeTrialSpikes.clearQuick();
            activeAlignSample = -1;
            activeAlignTimestampSeconds = -1.0;
            activeTrialSampleRate = 0.0f;
            return;
        }
    }

    auto hasViableAlignmentForTrial = [&] (const String& candidateConditionName,
                                           int64& resolvedAlignSample,
                                           double& resolvedAlignTimestampSeconds)
    {
        resolvedAlignSample = -1;
        resolvedAlignTimestampSeconds = -1.0;

        if (alignmentMode == AlignmentMode::eventCode)
        {
            const String alignCondition = alignmentEventConditionName.trim();
            if (alignCondition.isEmpty())
                return false;

            PyramidRuleEngine::ParsedEvent matchedEvent;
            const bool foundAlignEvent = ruleEngine.findNearestMatchingEventInWindow (alignCondition,
                                                                                       activeTrialStartSystemTimeMs,
                                                                                       activeTrialStartSequence,
                                                                                       ruleEngine.getLatestParsedSequence(),
                                                                                       activeTrialStartSystemTimeMs,
                                                                                       windowEndSystemTimeMs,
                                                                                       configuredPreTrialBufferMs,
                                                                                       matchedEvent);
            if (! foundAlignEvent)
                return false;

            if (matchedEvent.sampleNumber < 0)
                return false;

            resolvedAlignSample = matchedEvent.sampleNumber;
            ignoreUnused (candidateConditionName);
            return true;
        }

        resolvedAlignSample = activeAlignSample;
        resolvedAlignTimestampSeconds = activeAlignTimestampSeconds;
        return resolvedAlignSample >= 0 || resolvedAlignTimestampSeconds >= 0.0;
    };

    if (conditionNames.isEmpty())
    {
        static const String allTrialsConditionName = "All Trials";
        int64 resolvedAlignSample = -1;
        double resolvedAlignTimestampSeconds = -1.0;

        if (hasViableAlignmentForTrial (allTrialsConditionName, resolvedAlignSample, resolvedAlignTimestampSeconds))
        {
            activeAlignSample = resolvedAlignSample;
            activeAlignTimestampSeconds = resolvedAlignTimestampSeconds;
            trialsEmitted++;
            conditionMatchedTrialCounts[allTrialsConditionName] += 1;
            accumulateMatchedTrial (allTrialsConditionName);
        }
    }

    for (const auto& conditionName : conditionNames)
    {
        bool matched = false;
        String errorMessage;
        bool ok = true;

        try
        {
            ok = ruleEngine.evaluateConditionInWindow (conditionName,
                                                       activeTrialStartSequence,
                                                       ruleEngine.getLatestParsedSequence(),
                                                       activeTrialStartSystemTimeMs,
                                                       windowEndSystemTimeMs,
                                                       configuredPreTrialBufferMs,
                                                       matched,
                                                       errorMessage);
        }
        catch (...)
        {
            ok = false;
            errorMessage = "Exception while evaluating rule.";
        }

        if (! ok || ! matched)
            continue;

        int64 resolvedAlignSample = -1;
        double resolvedAlignTimestampSeconds = -1.0;

        if (! hasViableAlignmentForTrial (conditionName, resolvedAlignSample, resolvedAlignTimestampSeconds))
            continue;

        activeAlignSample = resolvedAlignSample;
        activeAlignTimestampSeconds = resolvedAlignTimestampSeconds;

        trialsEmitted++;
        conditionMatchedTrialCounts[conditionName] += 1;
        accumulateMatchedTrial (conditionName);
    }

    activeTrialSpikes.clearQuick();
    activeAlignSample = -1;
    activeAlignTimestampSeconds = -1.0;
    activeTrialSampleRate = 0.0f;
}

void PyramidPSTH::handleSpike (SpikePtr spike)
{
    if (spike == nullptr)
        return;

    spikeSeen++;

    String electrodeName;
    if (const auto* channelInfo = spike->getChannelInfo())
        electrodeName = channelInfo->getName();

    if (electrodeName.isEmpty())
        electrodeName = "Electrode";

    knownElectrodeNames.addIfNotAlreadyThere (electrodeName);

    if (! trialActive)
        return;

    TrialSpike trialSpike;
    trialSpike.sampleNumber = spike->getSampleNumber();
    trialSpike.timestampSeconds = spike->getTimestampInSeconds();
    trialSpike.streamId = spike->getStreamId();
    trialSpike.sortedUnit = int (spike->getSortedId());
    trialSpike.electrodeName = electrodeName;

    activeTrialSpikes.add (trialSpike);
}

bool PyramidPSTH::loadRulesFromFiles (const Array<File>& files, String& statusMessage)
{
    bool loaded = false;

    try
    {
        loaded = ruleEngine.loadRulesFromCsvFiles (files, statusMessage);
    }
    catch (...)
    {
        statusMessage = "Failed to load rules. Existing acquisition is unaffected.";
        loaded = false;
    }

    loadedRuleCsvPaths.clearQuick();
    for (const auto& file : files)
    {
        if (file.existsAsFile())
            loadedRuleCsvPaths.addIfNotAlreadyThere (file.getFullPathName());
    }

    return loaded;
}

void PyramidPSTH::clearRules()
{
    ruleEngine.reset();
    trialActive = false;
    activeTrialStartSequence = 0;
    activeTrialStartSystemTimeMs = 0;
    ttlSeen = 0;
    trialsEmitted = 0;
    spikeSeen = 0;
    spikesAccepted = 0;
    spikeMappingFailed = 0;
    alignmentSeen = 0;
    trialStartsSeen = 0;
    trialEndsSeen = 0;
    ttlFilteredOut = 0;

    lagSampleCount = 0;
    lagMeanMs = 0.0;
    lagM2Ms = 0.0;
    lastLagMs = 0;
    hasLagSample = false;

    activeTrialStreamId = 0;
    activeAlignSample = -1;
    activeAlignTimestampSeconds = -1.0;
    activeTrialSampleRate = 0.0f;
    activeTrialSpikes.clearQuick();
    clearPsthAccumulation();
    configuredConditionNames.clearQuick();
    trialFilterEnabled = false;
    trialFilterConditionName.clear();
    knownElectrodeNames.clearQuick();
    loadedRuleCsvPaths.clearQuick();
}

void PyramidPSTH::saveCustomParametersToXml (XmlElement* parentElement)
{
    if (parentElement == nullptr)
        return;

    parentElement->setAttribute ("visualizer_row_height", visualizerRowHeight);
    parentElement->setAttribute ("visualizer_num_cols", visualizerNumCols);
    parentElement->setAttribute ("visualizer_plot_type", visualizerPlotType);
    parentElement->setAttribute ("max_raster_trials", maxRasterTrials);
    parentElement->setAttribute ("electrode_filter_spec", electrodeFilterSpec);
    parentElement->setAttribute ("trial_filter_enabled", trialFilterEnabled ? 1 : 0);
    parentElement->setAttribute ("trial_filter_condition_name", trialFilterConditionName);
    parentElement->setAttribute ("active_condition_name", activeConditionName);
    parentElement->setAttribute ("alignment_mode", int (alignmentMode));
    parentElement->setAttribute ("alignment_event_condition_name", alignmentEventConditionName);

    auto* csvFilesElement = parentElement->createNewChildElement ("RULE_CSV_FILES");
    for (const auto& csvPath : loadedRuleCsvPaths)
    {
        auto* fileElement = csvFilesElement->createNewChildElement ("FILE");
        fileElement->setAttribute ("path", csvPath);
    }

    auto* configuredConditionsElement = parentElement->createNewChildElement ("CONFIGURED_CONDITIONS");
    for (const auto& conditionName : configuredConditionNames)
    {
        auto* conditionElement = configuredConditionsElement->createNewChildElement ("CONDITION");
        conditionElement->setAttribute ("name", conditionName);
    }

    auto* manualRulesElement = parentElement->createNewChildElement ("MANUAL_RULES");
    for (const auto& rule : ruleEngine.getManualRules())
    {
        auto* ruleElement = manualRulesElement->createNewChildElement ("RULE");
        ruleElement->setAttribute ("condition_name", rule.conditionName);
        ruleElement->setAttribute ("code_key", rule.codeKey);
        ruleElement->setAttribute ("code_type", codeTypeToString (rule.codeType));
        ruleElement->setAttribute ("operator", opToString (rule.op));
        ruleElement->setAttribute ("expected_value", rule.expectedValue);
        ruleElement->setAttribute ("lookback_ms", rule.lookbackMs);
    }
}

void PyramidPSTH::loadCustomParametersFromXml (XmlElement* customParamsXml)
{
    if (customParamsXml == nullptr)
        return;

    const int restoredRowHeight = customParamsXml->getIntAttribute ("visualizer_row_height", visualizerRowHeight);
    const int restoredNumCols = customParamsXml->getIntAttribute ("visualizer_num_cols", visualizerNumCols);
    const int restoredPlotType = customParamsXml->getIntAttribute ("visualizer_plot_type", visualizerPlotType);
    setVisualizerLayoutOptions (restoredRowHeight, restoredNumCols, restoredPlotType);

    const int restoredMaxRasterTrials = customParamsXml->getIntAttribute ("max_raster_trials", maxRasterTrials);
    setMaxRasterTrials (restoredMaxRasterTrials);

    setElectrodeFilterSpec (customParamsXml->getStringAttribute ("electrode_filter_spec", electrodeFilterSpec));

    const bool restoredTrialFilterEnabled = customParamsXml->getBoolAttribute ("trial_filter_enabled", trialFilterEnabled);
    const String restoredTrialFilterConditionName = customParamsXml->getStringAttribute ("trial_filter_condition_name", trialFilterConditionName);
    const String restoredActiveConditionName = customParamsXml->getStringAttribute ("active_condition_name", activeConditionName);
    const int restoredAlignmentMode = customParamsXml->getIntAttribute ("alignment_mode", int (alignmentMode));
    const String restoredAlignmentEventConditionName = customParamsXml->getStringAttribute ("alignment_event_condition_name", alignmentEventConditionName);

    Array<File> csvFilesToLoad;
    loadedRuleCsvPaths.clearQuick();

    if (auto* csvFilesElement = customParamsXml->getChildByName ("RULE_CSV_FILES"))
    {
        for (auto* fileElement : csvFilesElement->getChildIterator())
        {
            if (! fileElement->hasTagName ("FILE"))
                continue;

            const String path = fileElement->getStringAttribute ("path").trim();
            if (path.isEmpty())
                continue;

            const File file (path);
            if (file.existsAsFile())
            {
                csvFilesToLoad.add (file);
                loadedRuleCsvPaths.addIfNotAlreadyThere (file.getFullPathName());
            }
        }
    }

    if (csvFilesToLoad.size() > 0)
    {
        String statusMessage;
        loadRulesFromFiles (csvFilesToLoad, statusMessage);
    }

    Array<PyramidRuleEngine::Rule> restoredManualRules;
    if (auto* manualRulesElement = customParamsXml->getChildByName ("MANUAL_RULES"))
    {
        for (auto* ruleElement : manualRulesElement->getChildIterator())
        {
            if (! ruleElement->hasTagName ("RULE"))
                continue;

            PyramidRuleEngine::Rule rule;
            rule.conditionName = ruleElement->getStringAttribute ("condition_name").trim();
            rule.codeKey = ruleElement->getStringAttribute ("code_key").trim();
            rule.codeType = codeTypeFromString (ruleElement->getStringAttribute ("code_type").trim());
            rule.op = opFromString (ruleElement->getStringAttribute ("operator").trim());
            rule.expectedValue = ruleElement->getStringAttribute ("expected_value").trim();
            rule.lookbackMs = ruleElement->getIntAttribute ("lookback_ms", 1500);

            if (rule.conditionName.isNotEmpty() && rule.codeKey.isNotEmpty())
                restoredManualRules.add (rule);
        }
    }

    setManualConditionRules (restoredManualRules);

    Array<String> restoredConditionNames;
    if (auto* configuredConditionsElement = customParamsXml->getChildByName ("CONFIGURED_CONDITIONS"))
    {
        for (auto* conditionElement : configuredConditionsElement->getChildIterator())
        {
            if (! conditionElement->hasTagName ("CONDITION"))
                continue;

            const String conditionName = conditionElement->getStringAttribute ("name").trim();
            if (conditionName.isNotEmpty() && ! restoredConditionNames.contains (conditionName))
                restoredConditionNames.add (conditionName);
        }
    }

    if (restoredConditionNames.isEmpty())
    {
        for (const auto& rule : restoredManualRules)
        {
            if (rule.conditionName.isNotEmpty() && ! restoredConditionNames.contains (rule.conditionName))
                restoredConditionNames.add (rule.conditionName);
        }
    }

    setConfiguredConditionNames (restoredConditionNames);
    setTrialFilterState (restoredTrialFilterEnabled, restoredTrialFilterConditionName);
    setAlignmentMode (restoredAlignmentMode == int (AlignmentMode::eventCode)
                          ? AlignmentMode::eventCode
                          : AlignmentMode::ttl);
    setAlignmentEventConditionName (restoredAlignmentEventConditionName);
    if (restoredActiveConditionName.isNotEmpty())
        setSelectedConditionName (restoredActiveConditionName);
}

String PyramidPSTH::getStatusText() const
{
    auto health = ruleEngine.getHealth();
    const int conditionCount = getConditionNamesForEvaluation().size();

    return String()
        + "conditions=" + String (conditionCount)
        + " rules=" + String (health.rulesLoaded)
        + " prebuf_ms=" + String (int (getParameter ("pre_trial_buffer_ms")->getValue()))
        + " text_seen=" + String (health.eventsSeen)
        + " parse_fail=" + String (health.parseFailures)
        + " dropped=" + String (health.droppedEvents)
        + " eval_err=" + String (health.evalErrors)
        + " ttl_filtered=" + String (ttlFilteredOut.load())
        + " align=" + String (alignmentSeen.load())
        + " trial_start=" + String (trialStartsSeen.load())
        + " trial_end=" + String (trialEndsSeen.load())
        + " matched=" + String (trialsEmitted.load())
        + " spikes_seen=" + String (spikeSeen.load())
        + " spikes_accepted=" + String (spikesAccepted.load())
        + " mapping_fail=" + String (spikeMappingFailed.load())
        + " align_mode=" + String (alignmentMode == AlignmentMode::eventCode ? "event_code" : "ttl")
        + " align_event=" + (alignmentEventConditionName.isEmpty() ? String ("none") : alignmentEventConditionName);
}

bool PyramidPSTH::hasRulesLoaded() const
{
    return ruleEngine.getHealth().rulesLoaded > 0;
}

void PyramidPSTH::setManualConditionRules (const Array<PyramidRuleEngine::Rule>& rules)
{
    ruleEngine.setManualRules (rules);
    clearPsthAccumulation();
}

void PyramidPSTH::setConfiguredConditionNames (const Array<String>& conditionNames)
{
    configuredConditionNames.clearQuick();

    for (const auto& name : conditionNames)
    {
        const String trimmed = name.trim();
        if (trimmed.isNotEmpty() && ! configuredConditionNames.contains (trimmed))
            configuredConditionNames.add (trimmed);
    }

    if (configuredConditionNames.size() > 0 && ! configuredConditionNames.contains (activeConditionName))
        activeConditionName = configuredConditionNames[0];

    clearPsthAccumulation();
}

void PyramidPSTH::setTrialFilterState (bool enabled, const String& filterConditionName)
{
    trialFilterEnabled = enabled;
    trialFilterConditionName = filterConditionName.trim();
    clearPsthAccumulation();
}

void PyramidPSTH::setSelectedConditionName (const String& conditionName)
{
    const ScopedLock lock (stateLock);
    activeConditionName = conditionName.trim();
}

String PyramidPSTH::getSelectedConditionName() const
{
    const ScopedLock lock (stateLock);
    return activeConditionName;
}

Array<String> PyramidPSTH::getAvailableConditionNames() const
{
    Array<String> names;

    if (configuredConditionNames.size() > 0)
        names = configuredConditionNames;
    else
        names.add ("All Trials");

    return names;
}

Array<String> PyramidPSTH::getRuleConditionNames() const
{
    return ruleEngine.getConditionNames();
}

void PyramidPSTH::setAlignmentMode (AlignmentMode mode)
{
    alignmentMode = mode;
}

PyramidPSTH::AlignmentMode PyramidPSTH::getAlignmentMode() const
{
    return alignmentMode;
}

void PyramidPSTH::setAlignmentEventConditionName (const String& conditionName)
{
    alignmentEventConditionName = conditionName.trim();
}

String PyramidPSTH::getAlignmentEventConditionName() const
{
    return alignmentEventConditionName;
}

void PyramidPSTH::setVisualizerLayoutOptions (int rowHeightPixels, int numColumns, int plotTypeId)
{
    visualizerRowHeight = jlimit (100, 450, rowHeightPixels);
    visualizerNumCols = jlimit (1, 6, numColumns);
    visualizerPlotType = jlimit (1, 5, plotTypeId);
}

void PyramidPSTH::getVisualizerLayoutOptions (int& rowHeightPixels, int& numColumns, int& plotTypeId) const
{
    rowHeightPixels = visualizerRowHeight;
    numColumns = visualizerNumCols;
    plotTypeId = visualizerPlotType;
}

void PyramidPSTH::setPsthWindowOptions (int preMs, int postMs, int binMs)
{
    psthPreMs = jlimit (10, 5000, preMs);
    psthPostMs = jlimit (10, 5000, postMs);
    psthBinMs = jlimit (1, 1000, binMs);

    if (auto* preParam = getParameter ("pre_ms"))
        preParam->setNextValue (psthPreMs);
    if (auto* postParam = getParameter ("post_ms"))
        postParam->setNextValue (psthPostMs);
    if (auto* binParam = getParameter ("bin_size"))
        binParam->setNextValue (psthBinMs);

    resizePsthBinsIfNeeded();
}

void PyramidPSTH::setMaxRasterTrials (int maxTrials)
{
    maxRasterTrials = jlimit (1, 1000, maxTrials);

    for (auto& accumulator : conditionAccumulators)
    {
        while (accumulator.rasterTrials.size() > size_t (maxRasterTrials))
            accumulator.rasterTrials.pop_front();
    }
}

int PyramidPSTH::getMaxRasterTrials() const
{
    return maxRasterTrials;
}

void PyramidPSTH::clearPsthData()
{
    clearPsthAccumulation();
}

void PyramidPSTH::setElectrodeFilterSpec (const String& filterSpec)
{
    electrodeFilterSpec = filterSpec.trim();
}

String PyramidPSTH::getElectrodeFilterSpec() const
{
    return electrodeFilterSpec;
}

StringArray PyramidPSTH::getKnownElectrodeNames() const
{
    StringArray names;

    const int totalSpikeChannels = getTotalSpikeChannels();
    for (int i = 0; i < totalSpikeChannels; ++i)
    {
        if (const auto* channel = getSpikeChannel (i))
        {
            const String channelName = channel->getName().trim();
            if (channelName.isNotEmpty())
                names.addIfNotAlreadyThere (channelName);
        }
    }

    for (const auto& observedName : knownElectrodeNames)
    {
        const String trimmed = observedName.trim();
        if (trimmed.isNotEmpty())
            names.addIfNotAlreadyThere (trimmed);
    }

    return names;
}

void PyramidPSTH::getPsthWindowOptions (int& preMs, int& postMs, int& binMs) const
{
    preMs = psthPreMs;
    postMs = psthPostMs;
    binMs = psthBinMs;
}

void PyramidPSTH::getConditionSnapshots (std::vector<ConditionSnapshot>& snapshots,
                                         float& windowStartMs,
                                         float& windowEndMs,
                                         float& binWidthMs) const
{
    snapshots.clear();

    windowStartMs = -float (psthPreMs);
    windowEndMs = float (psthPostMs);
    binWidthMs = float (psthBinMs);

    const auto conditionNames = getConditionNamesForEvaluation();
    const float binWidthSec = float (psthBinMs) / 1000.0f;

    auto appendSnapshot = [&] (const ConditionAccumulator& accumulator)
    {
        if (! panelMatchesElectrodeFilter (accumulator))
            return;

        ConditionSnapshot snapshot;
        snapshot.conditionName = accumulator.conditionName;
        snapshot.electrodeName = accumulator.electrodeName;
        snapshot.sortedUnit = accumulator.sortedUnit;
        snapshot.panelLabel = accumulator.panelLabel;
        snapshot.ratesHz.insertMultiple (0, 0.0f, psthBins);
        snapshot.matchedTrialCount = accumulator.matchedTrials;
        snapshot.rasterTrials.insert (snapshot.rasterTrials.end(),
                                      accumulator.rasterTrials.begin(),
                                      accumulator.rasterTrials.end());

        if (accumulator.matchedTrials > 0)
        {
            for (int i = 0; i < accumulator.spikeCounts.size(); ++i)
                snapshot.ratesHz.set (i, float (accumulator.spikeCounts[i]) / float (accumulator.matchedTrials) / binWidthSec);
        }

        snapshots.push_back (std::move (snapshot));
    };

    if (conditionNames.isEmpty())
    {
        for (const auto& accumulator : conditionAccumulators)
            appendSnapshot (accumulator);
        return;
    }

    for (const auto& conditionName : conditionNames)
    {
        for (const auto& accumulator : conditionAccumulators)
        {
            if (accumulator.conditionName != conditionName)
                continue;
            appendSnapshot (accumulator);
        }
    }
}

void PyramidPSTH::getPsthSnapshot (Array<float>& ratesHz,
                                   int64& matchedTrialCount,
                                   float& windowStartMs,
                                   float& windowEndMs,
                                   float& binWidthMs) const
{
    ratesHz.clearQuick();
    ratesHz.insertMultiple (0, 0.0f, psthBins);

    windowStartMs = -float (psthPreMs);
    windowEndMs = float (psthPostMs);
    binWidthMs = float (psthBinMs);
    const String selected = activeConditionName;
    const auto* accumulator = findConditionAccumulator (selected);
    matchedTrialCount = (accumulator != nullptr) ? accumulator->matchedTrials : 0;

    if (accumulator == nullptr || accumulator->matchedTrials <= 0)
        return;

    const float binWidthSec = float (psthBinMs) / 1000.0f;

    for (int i = 0; i < accumulator->spikeCounts.size(); ++i)
        ratesHz.set (i, float (accumulator->spikeCounts[i]) / float (accumulator->matchedTrials) / binWidthSec);
}

void PyramidPSTH::getRasterSnapshot (std::vector<std::vector<float>>& trialSpikeTimesMs,
                                     int64& matchedTrialCount,
                                     float& windowStartMs,
                                     float& windowEndMs) const
{
    trialSpikeTimesMs.clear();
    const String selected = activeConditionName;
    const auto* accumulator = findConditionAccumulator (selected);

    if (accumulator != nullptr)
    {
        trialSpikeTimesMs.insert (trialSpikeTimesMs.end(), accumulator->rasterTrials.begin(), accumulator->rasterTrials.end());
        matchedTrialCount = accumulator->matchedTrials;
    }
    else
    {
        matchedTrialCount = 0;
    }

    windowStartMs = -float (psthPreMs);
    windowEndMs = float (psthPostMs);
}

void PyramidPSTH::clearPsthAccumulation()
{
    conditionAccumulators.clearQuick();
    conditionMatchedTrialCounts.clear();
}

void PyramidPSTH::resizePsthBinsIfNeeded()
{
    const int preFromParam = int (getParameter ("pre_ms")->getValue());
    const int postFromParam = int (getParameter ("post_ms")->getValue());
    const int binFromParam = int (getParameter ("bin_size")->getValue());

    const int newPreMs = jlimit (10, 5000, preFromParam);
    const int newPostMs = jlimit (10, 5000, postFromParam);
    const int newBinMs = jlimit (1, 1000, binFromParam);
    const int totalMs = newPreMs + newPostMs;
    const int newBins = jmax (1, int (std::ceil (double (totalMs) / double (newBinMs))));

    if (newPreMs == psthPreMs && newPostMs == psthPostMs && newBinMs == psthBinMs && newBins == psthBins)
        return;

    psthPreMs = newPreMs;
    psthPostMs = newPostMs;
    psthBinMs = newBinMs;
    psthBins = newBins;
    clearPsthAccumulation();
}

PyramidPSTH::ConditionAccumulator* PyramidPSTH::getOrCreateConditionAccumulator (const String& conditionName,
                                                                                 const String& electrodeName,
                                                                                 int sortedUnit,
                                                                                 int64 conditionMatchedTrialCount)
{
    const String panelKey = buildPanelKey (conditionName, electrodeName, sortedUnit);

    for (auto& accumulator : conditionAccumulators)
    {
        if (accumulator.panelKey == panelKey)
            return &accumulator;
    }

    ConditionAccumulator accumulator;
    accumulator.conditionName = conditionName;
    accumulator.electrodeName = electrodeName;
    accumulator.sortedUnit = sortedUnit;
    accumulator.panelKey = panelKey;
    accumulator.panelLabel = buildPanelLabel (conditionName, electrodeName, sortedUnit);
    accumulator.spikeCounts.insertMultiple (0, 0, psthBins);
    accumulator.matchedTrials = conditionMatchedTrialCount;
    conditionAccumulators.add (accumulator);
    return &conditionAccumulators.getReference (conditionAccumulators.size() - 1);
}

const PyramidPSTH::ConditionAccumulator* PyramidPSTH::findConditionAccumulator (const String& conditionName) const
{
    for (const auto& accumulator : conditionAccumulators)
    {
        if (accumulator.conditionName == conditionName)
            return &accumulator;
    }

    return nullptr;
}

void PyramidPSTH::accumulateMatchedTrial (const String& conditionName)
{
    const int64 matchedTrialCount = conditionMatchedTrialCounts[conditionName];
    std::map<String, std::vector<float>> trialRelMsByPanel;

    const float windowStartMs = -float (psthPreMs);
    const float windowEndMs = float (psthPostMs);

    for (const auto& trialSpike : activeTrialSpikes)
    {
        bool mappingSucceeded = false;
        float relMs = 0.0f;

        if (activeAlignTimestampSeconds >= 0.0 && trialSpike.timestampSeconds >= 0.0)
        {
            relMs = float ((trialSpike.timestampSeconds - activeAlignTimestampSeconds) * 1000.0);
            mappingSucceeded = true;
        }
        else if (trialSpike.streamId == activeTrialStreamId && activeAlignSample >= 0 && activeTrialSampleRate > 1.0f)
        {
            relMs = float (trialSpike.sampleNumber - activeAlignSample) * 1000.0f / activeTrialSampleRate;
            mappingSucceeded = true;
        }

        if (! mappingSucceeded)
        {
            spikeMappingFailed++;
            continue;
        }

        if (relMs < windowStartMs || relMs >= windowEndMs)
            continue;

        spikesAccepted++;

        const String panelKey = buildPanelKey (conditionName, trialSpike.electrodeName, trialSpike.sortedUnit);
        trialRelMsByPanel[panelKey].push_back (relMs);

        auto* accumulator = getOrCreateConditionAccumulator (conditionName,
                                                             trialSpike.electrodeName,
                                                             trialSpike.sortedUnit,
                                                             matchedTrialCount);
        if (accumulator == nullptr)
            continue;

        const int bin = int ((relMs + float (psthPreMs)) / float (psthBinMs));
        if (isPositiveAndBelow (bin, accumulator->spikeCounts.size()))
            accumulator->spikeCounts.set (bin, accumulator->spikeCounts[bin] + 1);
    }

    for (auto& accumulator : conditionAccumulators)
    {
        if (accumulator.conditionName != conditionName)
            continue;

        accumulator.matchedTrials = matchedTrialCount;

        auto it = trialRelMsByPanel.find (accumulator.panelKey);
        if (it != trialRelMsByPanel.end())
            accumulator.rasterTrials.push_back (std::move (it->second));
        else
            accumulator.rasterTrials.push_back ({});

        while (accumulator.rasterTrials.size() > size_t (maxRasterTrials))
            accumulator.rasterTrials.pop_front();
    }
}

void PyramidPSTH::refreshTtlStreamParameterOptions()
{
    auto* ttlStreamParam = dynamic_cast<SelectedStreamParameter*> (getParameter ("ttl_stream"));
    if (ttlStreamParam == nullptr)
        return;

    Array<String> streamNames;
    for (auto* stream : dataStreams)
        streamNames.add (stream->getKey());

    auto currentNames = ttlStreamParam->getStreamNames();
    bool changed = currentNames.size() != streamNames.size();

    if (! changed)
    {
        for (int i = 0; i < streamNames.size(); ++i)
        {
            if (currentNames[i] != streamNames[i])
            {
                changed = true;
                break;
            }
        }
    }

    if (! changed)
        return;

    const int previousIndex = ttlStreamParam->getSelectedIndex();
    ttlStreamParam->setStreamNames (streamNames);

    if (streamNames.isEmpty())
    {
        ttlStreamParam->setNextValue (-1, false);
        return;
    }

    const int clampedIndex = jlimit (0, streamNames.size() - 1, previousIndex);
    ttlStreamParam->setNextValue (clampedIndex, false);
}

bool PyramidPSTH::getConfiguredTtlStreamId (uint16& streamId) const
{
    auto* ttlStreamParam = dynamic_cast<SelectedStreamParameter*> (getParameter ("ttl_stream"));
    if (ttlStreamParam == nullptr)
        return false;

    const int selectedIndex = ttlStreamParam->getSelectedIndex();
    if (! isPositiveAndBelow (selectedIndex, dataStreams.size()))
        return false;

    streamId = dataStreams[selectedIndex]->getStreamId();
    return true;
}

Array<String> PyramidPSTH::getConditionNamesForEvaluation() const
{
    return configuredConditionNames;
}

bool PyramidPSTH::panelMatchesElectrodeFilter (const ConditionAccumulator& panel) const
{
    const String spec = electrodeFilterSpec.trim();

    if (spec.isEmpty() || spec == "*" || spec == "all")
        return true;

    StringArray tokens;
    tokens.addTokens (spec, ",;", "");
    tokens.trim();

    const String normalizedElectrode = panel.electrodeName.trim().toLowerCase();

    for (const auto& token : tokens)
    {
        const String t = token.trim().toLowerCase();
        if (t.isEmpty())
            continue;

        if (normalizedElectrode.contains (t))
            return true;
    }

    return false;
}

String PyramidPSTH::buildPanelKey (const String& conditionName, const String& electrodeName, int sortedUnit) const
{
    return conditionName + "|" + electrodeName + "|" + String (sortedUnit);
}

String PyramidPSTH::buildPanelLabel (const String& conditionName, const String& electrodeName, int sortedUnit) const
{
    return conditionName + " | " + electrodeName + " | unit " + String (sortedUnit);
}

#ifndef PYRAMID_PSTH_H_
#define PYRAMID_PSTH_H_

#include "PyramidRuleEngine.h"
#include "PyramidPSTHEditor.h"

#include <ProcessorHeaders.h>

#include <cmath>
#include <deque>
#include <vector>

class PyramidPSTH : public GenericProcessor
{
public:
    enum class AlignmentMode
    {
        ttl = 0,
        eventCode = 1
    };

    struct ConditionSnapshot
    {
        String conditionName;
        String electrodeName;
        int sortedUnit = 0;
        String panelLabel;
        Array<float> ratesHz;
        std::vector<std::vector<float>> rasterTrials;
        int64 matchedTrialCount = 0;
    };

    PyramidPSTH();
    ~PyramidPSTH() {}

    void registerParameters() override;
    AudioProcessorEditor* createEditor() override;
    void process (AudioBuffer<float>& buffer) override;
    void updateSettings() override;
    void saveCustomParametersToXml (XmlElement* parentElement) override;
    void loadCustomParametersFromXml (XmlElement* customParamsXml) override;

    void handleBroadcastMessage (const String& message, const int64 sampleNumber) override;
    void handleTTLEvent (TTLEventPtr event) override;
    void handleSpike (SpikePtr spike) override;

    bool loadRulesFromFiles (const Array<File>& files, String& statusMessage);
    void clearRules();
    String getStatusText() const;
    bool hasRulesLoaded() const;
    void setManualConditionRules (const Array<PyramidRuleEngine::Rule>& rules);
    void setConfiguredConditionNames (const Array<String>& conditionNames);
    void setTrialFilterState (bool enabled, const String& filterConditionName);
    void setSelectedConditionName (const String& conditionName);
    String getSelectedConditionName() const;
    Array<String> getAvailableConditionNames() const;
    Array<String> getRuleConditionNames() const;

    void setAlignmentMode (AlignmentMode mode);
    AlignmentMode getAlignmentMode() const;
    void setAlignmentEventConditionName (const String& conditionName);
    String getAlignmentEventConditionName() const;

    void setVisualizerLayoutOptions (int rowHeightPixels, int numColumns, int plotTypeId);
    void getVisualizerLayoutOptions (int& rowHeightPixels, int& numColumns, int& plotTypeId) const;

    void setPsthWindowOptions (int preMs, int postMs, int binMs);
    void getPsthWindowOptions (int& preMs, int& postMs, int& binMs) const;
    void setMaxRasterTrials (int maxTrials);
    int getMaxRasterTrials() const;
    void clearPsthData();
    void setElectrodeFilterSpec (const String& filterSpec);
    String getElectrodeFilterSpec() const;
    StringArray getKnownElectrodeNames() const;
    void getConditionSnapshots (std::vector<ConditionSnapshot>& snapshots,
                                float& windowStartMs,
                                float& windowEndMs,
                                float& binWidthMs) const;

    void getPsthSnapshot (Array<float>& ratesHz,
                          int64& matchedTrialCount,
                          float& windowStartMs,
                          float& windowEndMs,
                          float& binWidthMs) const;
    void getRasterSnapshot (std::vector<std::vector<float>>& trialSpikeTimesMs,
                            int64& matchedTrialCount,
                            float& windowStartMs,
                            float& windowEndMs) const;

private:
    struct ConditionAccumulator
    {
        String conditionName;
        String electrodeName;
        int sortedUnit = 0;
        String panelKey;
        String panelLabel;
        Array<int> spikeCounts;
        int64 matchedTrials = 0;
        std::deque<std::vector<float>> rasterTrials;
    };

    struct TrialSpike
    {
        int64 sampleNumber = 0;
        double timestampSeconds = -1.0;
        uint16 streamId = 0;
        String electrodeName;
        int sortedUnit = 0;
    };

    void clearPsthAccumulation();
    void resizePsthBinsIfNeeded();
    ConditionAccumulator* getOrCreateConditionAccumulator (const String& conditionName,
                                                           const String& electrodeName,
                                                           int sortedUnit,
                                                           int64 conditionMatchedTrialCount);
    const ConditionAccumulator* findConditionAccumulator (const String& conditionName) const;
    void accumulateMatchedTrial (const String& conditionName);
    Array<String> getConditionNamesForEvaluation() const;
    bool panelMatchesElectrodeFilter (const ConditionAccumulator& panel) const;
    String buildPanelKey (const String& conditionName, const String& electrodeName, int sortedUnit) const;
    String buildPanelLabel (const String& conditionName, const String& electrodeName, int sortedUnit) const;
    void refreshTtlStreamParameterOptions();
    bool getConfiguredTtlStreamId (uint16& streamId) const;

    PyramidRuleEngine ruleEngine;

    std::map<uint16, int64> lastSampleForStream;

    String activeConditionName = "trial_start";
    CriticalSection stateLock;
    bool trialActive = false;
    int64 activeTrialStartSequence = 0;
    int64 activeTrialStartSystemTimeMs = 0;
    int preTrialBufferMs = 80;
    int lagSearchWindowMs = 500;
    AlignmentMode alignmentMode = AlignmentMode::ttl;
    String alignmentEventConditionName;

    int64 lagSampleCount = 0;
    double lagMeanMs = 0.0;
    double lagM2Ms = 0.0;
    int lastLagMs = 0;
    bool hasLagSample = false;

    int psthPreMs = 200;
    int psthPostMs = 600;
    int psthBinMs = 20;
    int psthBins = 40;

    Array<String> configuredConditionNames;
    bool trialFilterEnabled = false;
    String trialFilterConditionName;
    String electrodeFilterSpec;

    int visualizerRowHeight = 180;
    int visualizerNumCols = 1;
    int visualizerPlotType = 3;

    uint16 activeTrialStreamId = 0;
    int64 activeAlignSample = -1;
    double activeAlignTimestampSeconds = -1.0;
    float activeTrialSampleRate = 0.0f;
    Array<TrialSpike> activeTrialSpikes;

    Array<ConditionAccumulator> conditionAccumulators;
    std::map<String, int64> conditionMatchedTrialCounts;
    StringArray knownElectrodeNames;
    StringArray loadedRuleCsvPaths;
    int maxRasterTrials = 60;

    std::atomic<int64> ttlSeen { 0 };
    std::atomic<int64> trialsEmitted { 0 };
    std::atomic<int64> spikeSeen { 0 };
    std::atomic<int64> spikesAccepted { 0 };
    std::atomic<int64> spikeMappingFailed { 0 };
    std::atomic<int64> alignmentSeen { 0 };
    std::atomic<int64> trialStartsSeen { 0 };
    std::atomic<int64> trialEndsSeen { 0 };
    std::atomic<int64> ttlFilteredOut { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PyramidPSTH);
};

#endif

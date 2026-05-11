#include "PyramidPSTHEditor.h"

#include "PyramidPSTH.h"

#include <vector>

class PyramidPSTHVisualizer : public Visualizer,
                              public ComboBox::Listener,
                              public Button::Listener,
                              public TextEditor::Listener
{
public:
    PyramidPSTHVisualizer (PyramidPSTH* parent)
        : Visualizer (parent), processor (parent)
    {
        refreshRate = 20.0f;

        rowHeightSelector = std::make_unique<ComboBox> ("Row Height Selector");
        for (int h = 100; h <= 450; h += 50)
            rowHeightSelector->addItem (String (h) + " px", h);
        rowHeightSelector->addListener (this);
        addAndMakeVisible (rowHeightSelector.get());

        columnNumberSelector = std::make_unique<ComboBox> ("Column Number Selector");
        for (int c = 1; c <= 6; ++c)
            columnNumberSelector->addItem (String (c), c);
        columnNumberSelector->addListener (this);
        addAndMakeVisible (columnNumberSelector.get());

        plotTypeSelector = std::make_unique<ComboBox> ("Plot Type Selector");
        plotTypeSelector->addItem ("Histogram", 1);
        plotTypeSelector->addItem ("Raster", 2);
        plotTypeSelector->addItem ("Histogram + Raster", 3);
        plotTypeSelector->addItem ("Line", 4);
        plotTypeSelector->addItem ("Line + Raster", 5);
        plotTypeSelector->addListener (this);
        addAndMakeVisible (plotTypeSelector.get());

        preMsEditor = std::make_unique<TextEditor> ("Pre MS Editor");
        postMsEditor = std::make_unique<TextEditor> ("Post MS Editor");
        binSizeEditor = std::make_unique<TextEditor> ("Bin Size Editor");
        maxTrialsEditor = std::make_unique<TextEditor> ("Max Trials Editor");
        electrodeSelectButton = std::make_unique<UtilityButton> ("Channels");
        clearPlotsButton = std::make_unique<UtilityButton> ("Clear");
        electrodeSelectButton->addListener (this);
        clearPlotsButton->addListener (this);
        addAndMakeVisible (electrodeSelectButton.get());
        addAndMakeVisible (clearPlotsButton.get());

        for (auto* editor : { preMsEditor.get(), postMsEditor.get(), binSizeEditor.get(), maxTrialsEditor.get() })
        {
            editor->setMultiLine (false);
            editor->setReturnKeyStartsNewLine (false);
            editor->setJustification (Justification::centred);
            editor->addListener (this);
            addAndMakeVisible (editor);
        }

        preMsEditor->setInputRestrictions (4, "0123456789");
        postMsEditor->setInputRestrictions (4, "0123456789");
        binSizeEditor->setInputRestrictions (4, "0123456789");
        maxTrialsEditor->setInputRestrictions (4, "0123456789");

        startCallbacks();
    }

    ~PyramidPSTHVisualizer() override
    {
        stopCallbacks();
    }

    void refresh() override
    {
        if (processor == nullptr)
            return;

        processor->getConditionSnapshots (conditionSnapshots, startMs, endMs, binMs);
        processor->getVisualizerLayoutOptions (rowHeightPixels, numColumns, plotTypeId);
        processor->getPsthWindowOptions (preMs, postMs, binSizeMs);
        maxRasterTrials = processor->getMaxRasterTrials();
        electrodeFilterSpec = processor->getElectrodeFilterSpec();
        availableElectrodes = processor->getKnownElectrodeNames();
        availableElectrodes.sort (true);
        syncElectrodeSelectionFromSpec();
        refreshElectrodeSummaryText();

        if (conditionSnapshots.empty())
        {
            auto conditionNames = processor->getAvailableConditionNames();
            if (conditionNames.isEmpty())
                conditionNames.add ("All Trials");

            StringArray panelElectrodes;
            if (! noneSelected)
            {
                if (selectedElectrodes.isEmpty())
                    panelElectrodes = availableElectrodes;
                else
                    panelElectrodes = selectedElectrodes;
            }

            const int totalMs = jmax (1, preMs + postMs);
            const int bins = jmax (1, (totalMs + jmax (1, binSizeMs) - 1) / jmax (1, binSizeMs));

            for (const auto& conditionName : conditionNames)
            {
                if (panelElectrodes.isEmpty())
                {
                    PyramidPSTH::ConditionSnapshot snapshot;
                    snapshot.conditionName = conditionName;
                    snapshot.electrodeName = "";
                    snapshot.sortedUnit = -1;
                    snapshot.panelLabel = conditionName + " | waiting for channels";
                    snapshot.ratesHz.insertMultiple (0, 0.0f, bins);
                    snapshot.matchedTrialCount = 0;
                    conditionSnapshots.push_back (std::move (snapshot));
                    continue;
                }

                for (const auto& electrode : panelElectrodes)
                {
                    PyramidPSTH::ConditionSnapshot snapshot;
                    snapshot.conditionName = conditionName;
                    snapshot.electrodeName = electrode;
                    snapshot.sortedUnit = -1;
                    snapshot.panelLabel = conditionName + " | " + electrode + " | unit ?";
                    snapshot.ratesHz.insertMultiple (0, 0.0f, bins);
                    snapshot.matchedTrialCount = 0;
                    conditionSnapshots.push_back (std::move (snapshot));
                }
            }
        }

        syncComboSelections();
        syncTextEditors();

        repaint();
    }

    void refreshState() override
    {
        refresh();
    }

    void resized() override
    {
        const int optionsBarHeight = 58;
        const int verticalOffset = 22;
        const int fieldHeight = 25;
        const int rightMargin = 8;
        const int fieldGap = 6;
        const int windowFieldWidth = 66;
        const int binFieldWidth = 54;
        const int maxTrialsFieldWidth = 72;

        optionsBarBounds = getLocalBounds().removeFromBottom (optionsBarHeight);
        const int fieldY = optionsBarBounds.getY() + verticalOffset;

        const int maxTrialsX = optionsBarBounds.getRight() - rightMargin - maxTrialsFieldWidth;
        const int binX = maxTrialsX - fieldGap - binFieldWidth;
        const int postX = binX - fieldGap - windowFieldWidth;
        const int preX = postX - fieldGap - windowFieldWidth;

        const int plotWidth = 180;
        const int plotX = preX - 10 - plotWidth;
        const int colsWidth = 50;
        const int colsX = plotX - 10 - colsWidth;
        const int rowWidth = 80;
        const int rowX = colsX - 10 - rowWidth;

        const int electrodeX = 8;
        const int electrodeWidth = 96;
        const int clearX = electrodeX + electrodeWidth + 6;
        const int clearWidth = 56;
        electrodeSelectButton->setBounds (electrodeX, fieldY, electrodeWidth, fieldHeight);
        clearPlotsButton->setBounds (clearX, fieldY, clearWidth, fieldHeight);

        rowHeightSelector->setBounds (rowX, fieldY, rowWidth, fieldHeight);
        columnNumberSelector->setBounds (colsX, fieldY, colsWidth, fieldHeight);
        plotTypeSelector->setBounds (plotX, fieldY, plotWidth, fieldHeight);

        preMsEditor->setBounds (preX, fieldY, windowFieldWidth, fieldHeight);
        postMsEditor->setBounds (postX, fieldY, windowFieldWidth, fieldHeight);
        binSizeEditor->setBounds (binX, fieldY, binFieldWidth, fieldHeight);
        maxTrialsEditor->setBounds (maxTrialsX, fieldY, maxTrialsFieldWidth, fieldHeight);
    }

    void mouseWheelMove (const MouseEvent& event, const MouseWheelDetails& wheel) override
    {
        ignoreUnused (event);

        const auto area = getLocalBounds().withBottom (optionsBarBounds.getY()).reduced (10);
        const int maxOffset = jmax (0, contentHeight - area.getHeight());

        if (maxOffset <= 0)
            return;

        const int step = jmax (18, rowHeightPixels / 6);
        scrollOffset = jlimit (0, maxOffset, scrollOffset - int (wheel.deltaY * float (step * 4)));
        repaint (area);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0, 18, 43));
        paintPlotArea (g);

        g.setColour (findColour (ThemeColours::componentBackground));
        g.fillRect (optionsBarBounds);

        paintOptionsText (g);
    }

    void comboBoxChanged (ComboBox* comboBox) override
    {
        if (processor == nullptr)
            return;

        if (comboBox == rowHeightSelector.get())
        {
            processor->setVisualizerLayoutOptions (rowHeightSelector->getSelectedId(), numColumns, plotTypeId);
        }
        else if (comboBox == columnNumberSelector.get())
        {
            processor->setVisualizerLayoutOptions (rowHeightPixels, columnNumberSelector->getSelectedId(), plotTypeId);
        }
        else if (comboBox == plotTypeSelector.get())
        {
            processor->setVisualizerLayoutOptions (rowHeightPixels, numColumns, plotTypeSelector->getSelectedId());
        }

        refresh();
    }

    void buttonClicked (Button* button) override
    {
        if (processor == nullptr)
            return;

        if (button == clearPlotsButton.get())
        {
            processor->clearPsthData();
            refresh();
            return;
        }

        if (button != electrodeSelectButton.get())
            return;

        constexpr int allId = 1;
        constexpr int noneId = 2;
        constexpr int selectAllShownId = 3;
        constexpr int electrodeBaseId = 100;

        PopupMenu menu;
        menu.addItem (allId, "All channels", true, ! noneSelected && selectedElectrodes.isEmpty());
        menu.addItem (noneId, "None", true, noneSelected);

        if (! availableElectrodes.isEmpty())
            menu.addItem (selectAllShownId,
                          "Select All Shown",
                          true,
                          ! noneSelected && areAllShownElectrodesSelected());

        if (! availableElectrodes.isEmpty())
        {
            menu.addSeparator();

            for (int i = 0; i < availableElectrodes.size(); ++i)
            {
                const auto& electrode = availableElectrodes[i];
                const bool isSelected = ! noneSelected && findSelectedElectrodeIndex (electrode) >= 0;
                menu.addItem (electrodeBaseId + i, electrode, true, isSelected);
            }
        }

        const int result = menu.show();
        if (result == 0)
            return;

        if (result == allId)
        {
            noneSelected = false;
            selectedElectrodes.clear();
        }
        else if (result == noneId)
        {
            noneSelected = true;
            selectedElectrodes.clear();
        }
        else if (result == selectAllShownId)
        {
            noneSelected = false;
            selectedElectrodes = availableElectrodes;
        }
        else
        {
            noneSelected = false;
            const int index = result - electrodeBaseId;

            if (isPositiveAndBelow (index, availableElectrodes.size()))
            {
                const auto& electrode = availableElectrodes[index];
                const int selectedIndex = findSelectedElectrodeIndex (electrode);
                if (selectedIndex >= 0)
                    selectedElectrodes.remove (selectedIndex);
                else
                    selectedElectrodes.addIfNotAlreadyThere (electrode);
            }
        }

        applyElectrodeSelectionToProcessor();
    }

    void textEditorReturnKeyPressed (TextEditor& editor) override
    {
        ignoreUnused (editor);
        commitWindowEditors();
    }

    void textEditorFocusLost (TextEditor& editor) override
    {
        ignoreUnused (editor);
        commitWindowEditors();
    }

    void textEditorEscapeKeyPressed (TextEditor& editor) override
    {
        ignoreUnused (editor);
        syncTextEditors();
        repaint();
    }

private:
    int findSelectedElectrodeIndex (const String& electrodeName) const
    {
        for (int i = 0; i < selectedElectrodes.size(); ++i)
        {
            if (selectedElectrodes[i].equalsIgnoreCase (electrodeName))
                return i;
        }

        return -1;
    }

    bool areAllShownElectrodesSelected() const
    {
        if (availableElectrodes.isEmpty())
            return false;

        for (const auto& electrode : availableElectrodes)
        {
            if (findSelectedElectrodeIndex (electrode) < 0)
                return false;
        }

        return true;
    }

    void commitWindowEditors()
    {
        if (processor == nullptr || ignoreTextEditorCallback)
            return;

        const int newPreMs = jlimit (10, 5000, preMsEditor->getText().getIntValue());
        const int newPostMs = jlimit (10, 5000, postMsEditor->getText().getIntValue());
        const int newBinMs = jlimit (1, 1000, binSizeEditor->getText().getIntValue());
        const int newMaxTrials = jlimit (1, 1000, maxTrialsEditor->getText().getIntValue());

        processor->setPsthWindowOptions (newPreMs, newPostMs, newBinMs);
        processor->setMaxRasterTrials (newMaxTrials);
        refresh();
    }

    void syncComboSelections()
    {
        if (rowHeightSelector->getSelectedId() != rowHeightPixels)
            rowHeightSelector->setSelectedId (rowHeightPixels, dontSendNotification);

        if (columnNumberSelector->getSelectedId() != numColumns)
            columnNumberSelector->setSelectedId (numColumns, dontSendNotification);

        if (plotTypeSelector->getSelectedId() != plotTypeId)
            plotTypeSelector->setSelectedId (plotTypeId, dontSendNotification);
    }

    void syncTextEditors()
    {
        ignoreTextEditorCallback = true;

        if (! preMsEditor->hasKeyboardFocus (true))
            preMsEditor->setText (String (preMs), dontSendNotification);

        if (! postMsEditor->hasKeyboardFocus (true))
            postMsEditor->setText (String (postMs), dontSendNotification);

        if (! binSizeEditor->hasKeyboardFocus (true))
            binSizeEditor->setText (String (binSizeMs), dontSendNotification);

        if (! maxTrialsEditor->hasKeyboardFocus (true))
            maxTrialsEditor->setText (String (maxRasterTrials), dontSendNotification);

        ignoreTextEditorCallback = false;
    }

    void syncElectrodeSelectionFromSpec()
    {
        selectedElectrodes.clearQuick();
        noneSelected = false;

        const String spec = electrodeFilterSpec.trim();
        if (spec == "__none__")
        {
            noneSelected = true;
            return;
        }

        if (spec.isEmpty() || spec.equalsIgnoreCase ("all") || spec == "*")
            return;

        selectedElectrodes.addTokens (spec, ",;", "");
        selectedElectrodes.trim();
        selectedElectrodes.removeEmptyStrings();
    }

    void refreshElectrodeSummaryText()
    {
        if (noneSelected)
        {
            electrodeSelectionSummary = "None";
            electrodeSelectButton->setTooltip ("Showing no channels");
            return;
        }

        if (selectedElectrodes.isEmpty())
        {
            electrodeSelectionSummary = "All";
            electrodeSelectButton->setTooltip ("Showing all channels");
            return;
        }

        if (selectedElectrodes.size() == 1)
            electrodeSelectionSummary = selectedElectrodes[0];
        else
            electrodeSelectionSummary = String (selectedElectrodes.size()) + " selected";

        electrodeSelectButton->setTooltip ("Selected: " + selectedElectrodes.joinIntoString (", "));
    }

    void applyElectrodeSelectionToProcessor()
    {
        if (processor == nullptr)
            return;

        const String spec = noneSelected ? "__none__" : selectedElectrodes.joinIntoString (", ");
        processor->setElectrodeFilterSpec (spec);
        refresh();
    }

    void paintOptionsText (Graphics& g)
    {
        g.setColour (findColour (ThemeColours::defaultText));
        g.setFont (FontOptions ("Inter", "Regular", 15.0f));

        const int labelY = optionsBarBounds.getY() + 4;
        g.setFont (FontOptions (11.0f));
        g.drawText ("Channels", electrodeSelectButton->getX(), labelY, electrodeSelectButton->getWidth(), 10, Justification::centredLeft, false);
        g.drawText ("Clear", clearPlotsButton->getX(), labelY, clearPlotsButton->getWidth(), 10, Justification::centred, false);
        g.drawText (electrodeSelectionSummary,
                electrodeSelectButton->getRight() + 6,
            optionsBarBounds.getY() + 24,
                jmax (30, rowHeightSelector->getX() - electrodeSelectButton->getRight() - 12),
                14,
                Justification::centredLeft,
                true);
        g.drawText ("Row Height", rowHeightSelector->getX(), labelY, rowHeightSelector->getWidth(), 10, Justification::centred, false);
        g.drawText ("Num Cols", columnNumberSelector->getX(), labelY, columnNumberSelector->getWidth(), 10, Justification::centred, false);
        g.drawText ("Plot Type", plotTypeSelector->getX(), labelY, plotTypeSelector->getWidth(), 10, Justification::centred, false);
        g.drawText ("Pre MS", preMsEditor->getX(), labelY, preMsEditor->getWidth(), 10, Justification::centred, false);
        g.drawText ("Post MS", postMsEditor->getX(), labelY, postMsEditor->getWidth(), 10, Justification::centred, false);
        g.drawText ("Bin MS", binSizeEditor->getX(), labelY, binSizeEditor->getWidth(), 10, Justification::centred, false);
        g.drawText ("Max Trials", maxTrialsEditor->getX(), labelY, maxTrialsEditor->getWidth(), 10, Justification::centred, false);

        g.drawText ("Scroll: mouse wheel", plotTypeSelector->getX() + 4, optionsBarBounds.getY() + optionsBarBounds.getHeight() - 14, 150, 11, Justification::centredLeft, false);
    }

    void paintPlotArea (Graphics& g)
    {
        auto area = getLocalBounds().withBottom (optionsBarBounds.getY()).reduced (10);

        if (area.getWidth() <= 0 || area.getHeight() <= 0)
            return;

        const int gridGap = 8;
        const int columns = jmax (1, numColumns);
        const int cellWidth = jmax (220, (area.getWidth() - gridGap * (columns - 1)) / columns);
        const int cellHeight = jmax (120, rowHeightPixels);
        const bool plotHistogram = (plotTypeId == 1 || plotTypeId == 3);
        const bool plotRaster = (plotTypeId == 2 || plotTypeId == 3 || plotTypeId == 5);
        const bool plotLine = (plotTypeId == 4 || plotTypeId == 5);

        const int rows = (int (conditionSnapshots.size()) + columns - 1) / columns;
        contentHeight = rows * cellHeight + jmax (0, rows - 1) * gridGap;
        const int yOffset = area.getY() - scrollOffset;

        for (int i = 0; i < int (conditionSnapshots.size()); ++i)
        {
            const int row = i / columns;
            const int col = i % columns;

            Rectangle<int> panel (area.getX() + col * (cellWidth + gridGap),
                                  yOffset + row * (cellHeight + gridGap),
                                  cellWidth,
                                  cellHeight);

            if (panel.getBottom() < area.getY() || panel.getY() > area.getBottom())
                continue;

            drawConditionPanel (g, panel.reduced (2), conditionSnapshots[size_t (i)], plotHistogram, plotRaster, plotLine);
        }
    }

    void drawConditionPanel (Graphics& g,
                             Rectangle<int> panel,
                             const PyramidPSTH::ConditionSnapshot& snapshot,
                             bool plotHistogram,
                             bool plotRaster,
                             bool plotLine)
    {
        g.setColour (findColour (ThemeColours::componentBackground));
        g.fillRoundedRectangle (panel.toFloat(), 4.0f);
        g.setColour (Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (panel.toFloat(), 4.0f, 1.0f);

        auto titleArea = panel.removeFromTop (20);
        g.setColour (findColour (ThemeColours::defaultText));
        g.setFont (FontOptions (13.0f));
        const String panelTitle = snapshot.panelLabel.isNotEmpty() ? snapshot.panelLabel : snapshot.conditionName;
        g.drawText (panelTitle, titleArea.removeFromLeft (jmax (80, titleArea.getWidth() / 2)), Justification::centredLeft);
        g.setFont (FontOptions (11.0f));
        g.drawText ("matched=" + String (snapshot.matchedTrialCount), titleArea, Justification::centredRight);

        drawOverlaidPlot (g,
                          panel.reduced (6, 4),
                          snapshot.ratesHz,
                          snapshot.rasterTrials,
                          plotHistogram,
                          plotRaster,
                          plotLine);
    }

    void drawOverlaidPlot (Graphics& g,
                           Rectangle<int> area,
                           const Array<float>& rates,
                           const std::vector<std::vector<float>>& rasterTrials,
                           bool drawHistogram,
                           bool drawRaster,
                           bool drawLine)
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRect (area);

        const int axisBottom = area.getBottom() - 14;
        const int axisLeft = area.getX() + 36;
        const int axisRight = area.getRight() - 6;
        const int axisTop = area.getY() + 6;
        const int axisWidth = jmax (1, axisRight - axisLeft);
        const int axisHeight = jmax (1, axisBottom - axisTop);

        g.setColour (Colours::white.withAlpha (0.45f));
        g.drawLine (float (axisLeft), float (axisBottom), float (axisRight), float (axisBottom));
        g.drawLine (float (axisLeft), float (axisBottom), float (axisLeft), float (axisTop));

        const float zeroX = float (axisLeft) + ((0.0f - startMs) / (endMs - startMs)) * float (axisWidth);
        g.setColour (Colours::orange.withAlpha (0.8f));
        g.drawLine (zeroX, float (axisTop), zeroX, float (axisBottom), 1.0f);

        float maxRate = 1.0f;
        for (auto v : rates)
            maxRate = jmax (maxRate, v);

        if (! rates.isEmpty())
        {
            const float barWidth = float (axisWidth) / float (rates.size());

            if (drawHistogram)
            {
                g.setColour (Colour (0xff3fa7ff).withAlpha (drawRaster ? 0.55f : 0.95f));
                for (int i = 0; i < rates.size(); ++i)
                {
                    const float normalized = rates[i] / maxRate;
                    const float h = normalized * float (axisHeight);
                    const float x = float (axisLeft) + float (i) * barWidth;
                    g.fillRect (Rectangle<float> (x, float (axisBottom) - h, jmax (1.0f, barWidth - 1.0f), h));
                }
            }

            if (drawLine)
            {
                g.setColour (Colour (0xff7fd3ff));
                for (int i = 0; i < rates.size() - 1; ++i)
                {
                    const float x1 = float (axisLeft) + (float (i) + 0.5f) * barWidth;
                    const float x2 = float (axisLeft) + (float (i + 1) + 0.5f) * barWidth;
                    const float y1 = float (axisBottom) - (rates[i] / maxRate) * float (axisHeight);
                    const float y2 = float (axisBottom) - (rates[i + 1] / maxRate) * float (axisHeight);
                    g.drawLine (x1, y1, x2, y2, 2.0f);
                }
            }
        }

        if (drawRaster && ! rasterTrials.empty())
        {
            const int nRows = int (rasterTrials.size());
            g.setColour (Colours::white.withAlpha (0.85f));
            for (int row = 0; row < nRows; ++row)
            {
                const auto& spikes = rasterTrials[size_t (row)];
                const float y = float (axisTop) + (float (row) + 0.5f) * float (axisHeight) / float (jmax (1, nRows));

                for (auto relMs : spikes)
                {
                    const float x = float (axisLeft) + ((relMs - startMs) / (endMs - startMs)) * float (axisWidth);
                    g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
                }
            }
        }

        g.setColour (findColour (ThemeColours::defaultText));
        g.setFont (FontOptions (10.0f));
        if (drawHistogram && drawRaster)
            g.drawText ("PSTH + Raster", area.getX() + 4, area.getY() + 2, 80, 12, Justification::centredLeft);
        else if (drawHistogram)
            g.drawText ("PSTH", area.getX() + 4, area.getY() + 2, 48, 12, Justification::centredLeft);
        else if (drawLine && drawRaster)
            g.drawText ("Line + Raster", area.getX() + 4, area.getY() + 2, 80, 12, Justification::centredLeft);
        else if (drawLine)
            g.drawText ("Line", area.getX() + 4, area.getY() + 2, 48, 12, Justification::centredLeft);
        else
            g.drawText ("Raster", area.getX() + 4, area.getY() + 2, 48, 12, Justification::centredLeft);
        g.drawText (String (startMs, 0), axisLeft - 20, axisBottom + 1, 40, 12, Justification::centred);
        g.drawText ("0", int (zeroX) - 10, axisBottom + 1, 20, 12, Justification::centred);
        g.drawText (String (endMs, 0), axisRight - 20, axisBottom + 1, 40, 12, Justification::centred);
    }

private:
    PyramidPSTH* processor = nullptr;

    std::unique_ptr<ComboBox> rowHeightSelector;
    std::unique_ptr<ComboBox> columnNumberSelector;
    std::unique_ptr<ComboBox> plotTypeSelector;
    std::unique_ptr<UtilityButton> electrodeSelectButton;
    std::unique_ptr<UtilityButton> clearPlotsButton;
    std::unique_ptr<TextEditor> preMsEditor;
    std::unique_ptr<TextEditor> postMsEditor;
    std::unique_ptr<TextEditor> binSizeEditor;
    std::unique_ptr<TextEditor> maxTrialsEditor;
    std::vector<PyramidPSTH::ConditionSnapshot> conditionSnapshots;
    StringArray availableElectrodes;
    StringArray selectedElectrodes;
    bool noneSelected = false;
    String electrodeSelectionSummary = "All";

    Rectangle<int> optionsBarBounds;

    float startMs = -200.0f;
    float endMs = 600.0f;
    float binMs = 20.0f;

    int rowHeightPixels = 180;
    int numColumns = 1;
    int plotTypeId = 3;
    int preMs = 200;
    int postMs = 600;
    int binSizeMs = 20;
    int maxRasterTrials = 60;
    String electrodeFilterSpec;
    bool ignoreTextEditorCallback = false;
    int scrollOffset = 0;
    int contentHeight = 0;
};

PyramidPSTHEditor::PyramidPSTHEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "PSTH", 760)
{
    desiredWidth = 760;

    const int leftX = 24;
    const int leftWidth = 250;
    const int gapX = 286;
    const int gapWidth = 136;
    const int rightX = 430;
    const int rightWidth = 306;
    const int buttonRowY = 54;

    debugLabel = std::make_unique<Label> ("debug_label", "Status / Debug");
    debugLabel->setBounds (rightX, 6, rightWidth, 20);
    debugLabel->setJustificationType (Justification::centredLeft);
    debugLabel->setFont (FontOptions (15.0f, Font::plain));
    addAndMakeVisible (debugLabel.get());

    addSelectedStreamParameterEditor (Parameter::PROCESSOR_SCOPE, "ttl_stream", leftX, 29);
    getParameterEditor ("ttl_stream")->setBounds (leftX, 29, leftWidth, getParameterEditor ("ttl_stream")->getHeight());

    addTtlLineParameterEditor (Parameter::STREAM_SCOPE, "trial_line", leftX, 54);
    getParameterEditor ("trial_line")->setBounds (leftX, 54, leftWidth, getParameterEditor ("trial_line")->getHeight());

    addTtlLineParameterEditor (Parameter::STREAM_SCOPE, "trigger_line", leftX, 79);
    getParameterEditor ("trigger_line")->setBounds (leftX, 79, leftWidth, getParameterEditor ("trigger_line")->getHeight());

    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "pre_trial_buffer_ms", leftX, 104);
    getParameterEditor ("pre_trial_buffer_ms")->setBounds (leftX, 104, leftWidth, getParameterEditor ("pre_trial_buffer_ms")->getHeight());

    loadRulesButton = std::make_unique<UtilityButton> ("Load CSV Rules");
    loadRulesButton->setBounds (gapX, 29, gapWidth, 24);
    loadRulesButton->addListener (this);
    addAndMakeVisible (loadRulesButton.get());

    clearRulesButton = std::make_unique<UtilityButton> ("Clear Rules");
    clearRulesButton->setBounds (gapX, 54, gapWidth, 24);
    clearRulesButton->addListener (this);
    addAndMakeVisible (clearRulesButton.get());

    alignModeButton = std::make_unique<UtilityButton> ("Align Code");
    alignModeButton->setBounds (gapX, 79, gapWidth, 24);
    alignModeButton->addListener (this);
    addAndMakeVisible (alignModeButton.get());

    configureButton = std::make_unique<UtilityButton> ("Configure");
    configureButton->setBounds (gapX, 104, gapWidth, 24);
    configureButton->addListener (this);
    addAndMakeVisible (configureButton.get());

    statusBox = std::make_unique<TextEditor> ("status_box");
    statusBox->setBounds (rightX, 36, rightWidth, 170);
    statusBox->setReadOnly (true);
    statusBox->setMultiLine (true);
    statusBox->setReturnKeyStartsNewLine (true);
    statusBox->setScrollbarsShown (true);
    statusBox->setCaretVisible (false);
    statusBox->setPopupMenuEnabled (true);
    statusBox->setText ("No rules loaded.", dontSendNotification);
    addAndMakeVisible (statusBox.get());

    startTimerHz (2);
}

PyramidPSTHEditor::~PyramidPSTHEditor()
{
    stopTimer();
}

void PyramidPSTHEditor::buttonClicked (Button* button)
{
    auto* processor = getPyramidProcessor();

    if (processor == nullptr)
        return;

    if (button == loadRulesButton.get())
    {
        FileChooser chooser ("Choose one or more ecode CSV files", File(), "*.csv", true);

        if (chooser.browseForMultipleFilesToOpen())
        {
            Array<File> files = chooser.getResults();

            String status;
            processor->loadRulesFromFiles (files, status);
            applyManualRulesToProcessor (processor->getSelectedConditionName());
            statusBox->setText (formatStatusForDisplay (status), dontSendNotification);
        }
    }
    else if (button == clearRulesButton.get())
    {
        userConditions.clearQuick();
        filterEnabled = false;
        filterCodeKey = "name";
        filterExpectedValue.clear();
        processor->clearRules();
        statusBox->setText ("Rules/conditions cleared.", dontSendNotification);
    }
    else if (button == configureButton.get())
    {
        openConfigureDialog();
    }
    else if (button == alignModeButton.get())
    {
        openAlignmentDialog();
    }
}

void PyramidPSTHEditor::timerCallback()
{
    auto* processor = getPyramidProcessor();

    if (processor == nullptr)
        return;

    String rulesText = processor->hasRulesLoaded() ? "Rules loaded" : "No rules loaded";

    rulesText += " | conditions=" + String (userConditions.size());

    if (filterEnabled)
        rulesText += " | filter=" + filterCodeKey + "=" + filterExpectedValue;

    if (processor->getAlignmentMode() == PyramidPSTH::AlignmentMode::eventCode)
    {
        const String alignEvent = processor->getAlignmentEventConditionName();
        rulesText += " | align=event:" + (alignEvent.isNotEmpty() ? alignEvent : String ("none"));
    }
    else
    {
        rulesText += " | align=ttl";
    }

    statusBox->setText (formatStatusForDisplay (processor->getStatusText()), dontSendNotification);
}

void PyramidPSTHEditor::resized()
{
    VisualizerEditor::resized();
}

Visualizer* PyramidPSTHEditor::createNewCanvas()
{
    return new PyramidPSTHVisualizer (getPyramidProcessor());
}

PyramidPSTH* PyramidPSTHEditor::getPyramidProcessor() const
{
    return dynamic_cast<PyramidPSTH*> (getProcessor());
}

String PyramidPSTHEditor::formatStatusForDisplay (const String& rawStatus) const
{
    StringArray chunks;
    chunks.addTokens (rawStatus, "|", "");
    chunks.trim();

    if (chunks.size() <= 1)
        return rawStatus;

    String formatted;

    for (int i = 0; i < chunks.size(); ++i)
    {
        formatted += chunks[i] + "\n";
    }

    return formatted.trimEnd();
}

void PyramidPSTHEditor::openConfigureDialog()
{
    auto* processor = getPyramidProcessor();
    if (processor == nullptr)
        return;

    String preferredCondition = processor->getSelectedConditionName();

    // Build list of existing condition names
    StringArray existingConditionNames;
    for (const auto& condition : userConditions)
        existingConditionNames.add (condition.conditionName);

    // STEP 1: Selection dialog
    AlertWindow selectWindow ("Configure Conditions",
                             "Select an action:",
                             AlertWindow::NoIcon);

    if (!existingConditionNames.isEmpty())
    {
        selectWindow.addComboBox ("condition_select", existingConditionNames, "Edit Existing Condition");
    }

    selectWindow.addButton ("Add New Condition", 1);
    selectWindow.addButton ("Edit Selected", 2);
    selectWindow.addButton ("Set Filter", 3);
    selectWindow.addButton ("Done", 4);

    const int selectResult = selectWindow.runModalLoop();

    String selectedConditionName;
    if (selectResult == 2 && !existingConditionNames.isEmpty())
    {
        if (auto* comboBox = selectWindow.getComboBoxComponent ("condition_select"))
        {
            selectedConditionName = existingConditionNames[comboBox->getSelectedItemIndex()];
        }
    }

    // STEP 2: Edit condition dialog (if add or edit selected)
    if (selectResult == 1 || (selectResult == 2 && !selectedConditionName.isEmpty()))
    {
        String editName = selectedConditionName;
        String editCodeKey = "name";
        String editValue;

        // If editing, load current values
        if (!selectedConditionName.isEmpty())
        {
            for (const auto& condition : userConditions)
            {
                if (condition.conditionName == selectedConditionName)
                {
                    editCodeKey = condition.codeKey;
                    editValue = condition.expectedValue;
                    break;
                }
            }
        }

        AlertWindow editWindow ("Condition Details",
                              selectResult == 1 ? "Enter new condition:" : "Edit condition:",
                              AlertWindow::NoIcon);

        editWindow.addTextEditor ("condition_name", editName, "Condition Name");
        editWindow.addTextEditor ("code_key", editCodeKey, "Event Code Key");
        editWindow.addTextEditor ("expected_value", editValue, "Expected Value");

        editWindow.addButton ("Save", 1);
        if (selectResult == 2)
            editWindow.addButton ("Delete", 2);
        editWindow.addButton ("Cancel", 3);

        const int editResult = editWindow.runModalLoop();

        if (editResult == 1)
        {
            const String newName = editWindow.getTextEditorContents ("condition_name").trim();
            const String newCode = editWindow.getTextEditorContents ("code_key").trim();
            const String newValue = editWindow.getTextEditorContents ("expected_value").trim();

            if (newName.isNotEmpty() && newCode.isNotEmpty() && newValue.isNotEmpty())
            {
                // Remove old condition if editing
                if (!selectedConditionName.isEmpty())
                {
                    for (int i = 0; i < userConditions.size(); ++i)
                    {
                        if (userConditions[i].conditionName == selectedConditionName)
                        {
                            userConditions.remove (i);
                            break;
                        }
                    }
                }

                // Add updated condition
                userConditions.add ({newName, newCode, newValue});
                preferredCondition = newName;
                applyManualRulesToProcessor (preferredCondition);
            }
        }
        else if (editResult == 2)
        {
            // Delete condition
            for (int i = 0; i < userConditions.size(); ++i)
            {
                if (userConditions[i].conditionName == selectedConditionName)
                {
                    userConditions.remove (i);
                    break;
                }
            }
            applyManualRulesToProcessor (String());
        }
    }

    // STEP 3: Filter dialog (if requested)
    if (selectResult == 3)
    {
        AlertWindow filterWindow ("Filter Settings",
                                "Optionally gate all trials with a filter condition:",
                                AlertWindow::NoIcon);

        filterWindow.addTextEditor ("filter_code_key", filterCodeKey, "Filter Event Code (optional)");
        filterWindow.addTextEditor ("filter_value", filterExpectedValue, "Filter Value (optional)");

        filterWindow.addButton ("Apply Filter", 1);
        filterWindow.addButton ("Clear Filter", 2);
        filterWindow.addButton ("Cancel", 3);

        const int filterResult = filterWindow.runModalLoop();

        if (filterResult == 1)
        {
            filterCodeKey = filterWindow.getTextEditorContents ("filter_code_key").trim();
            filterExpectedValue = filterWindow.getTextEditorContents ("filter_value").trim();
            filterEnabled = filterCodeKey.isNotEmpty() && filterExpectedValue.isNotEmpty();
            applyManualRulesToProcessor (preferredCondition);
        }
        else if (filterResult == 2)
        {
            filterCodeKey = "name";
            filterExpectedValue.clear();
            filterEnabled = false;
            applyManualRulesToProcessor (preferredCondition);
        }
    }
}

void PyramidPSTHEditor::openAlignmentDialog()
{
    auto* processor = getPyramidProcessor();
    if (processor == nullptr)
        return;

    const auto availableRuleConditions = processor->getRuleConditionNames();

    AlertWindow window ("Alignment Mode",
                        "Select TTL or a rules-CSV event code condition for alignment.\n"
                        "Event-code alignment has no fallback: trials without a valid event are omitted.",
                        AlertWindow::NoIcon);

    StringArray modeChoices;
    modeChoices.add ("TTL Trigger Line");
    for (const auto& condition : availableRuleConditions)
        modeChoices.add (condition);

    window.addComboBox ("alignment_mode", modeChoices, "Alignment Mode");
    window.addButton ("Apply", 1);
    window.addButton ("Cancel", 2);

    const int result = window.runModalLoop();

    if (result == 1)
    {
        if (auto* comboBox = window.getComboBoxComponent ("alignment_mode"))
        {
            const String selection = modeChoices[comboBox->getSelectedItemIndex()];
            if (selection == "TTL Trigger Line")
            {
                processor->setAlignmentMode (PyramidPSTH::AlignmentMode::ttl);
            }
            else
            {
                processor->setAlignmentMode (PyramidPSTH::AlignmentMode::eventCode);
                processor->setAlignmentEventConditionName (selection);
            }
        }
    }
}

void PyramidPSTHEditor::applyManualRulesToProcessor (const String& preferredCondition)
{
    auto* processor = getPyramidProcessor();

    if (processor == nullptr)
        return;

    Array<PyramidRuleEngine::Rule> rules;
    Array<String> conditionNames;

    for (const auto& condition : userConditions)
    {
        const String conditionName = condition.conditionName.trim();
        const String codeKey = condition.codeKey.trim();
        const String expectedValue = condition.expectedValue.trim();

        if (conditionName.isEmpty() || codeKey.isEmpty() || expectedValue.isEmpty())
            continue;

        PyramidRuleEngine::Rule rule;
        rule.conditionName = conditionName;
        rule.codeKey = codeKey;
        rule.codeType = PyramidRuleEngine::CodeType::value;
        rule.op = PyramidRuleEngine::Operator::equals;
        rule.expectedValue = expectedValue;
        rule.lookbackMs = 1500;
        rules.add (rule);

        if (! conditionNames.contains (conditionName))
            conditionNames.add (conditionName);
    }

    String filterConditionName;
    if (filterEnabled)
    {
        const String key = filterCodeKey.trim();
        const String value = filterExpectedValue.trim();

        if (key.isNotEmpty() && value.isNotEmpty())
        {
            filterConditionName = "__manual_filter__";

            PyramidRuleEngine::Rule filterRule;
            filterRule.conditionName = filterConditionName;
            filterRule.codeKey = key;
            filterRule.codeType = PyramidRuleEngine::CodeType::value;
            filterRule.op = PyramidRuleEngine::Operator::equals;
            filterRule.expectedValue = value;
            filterRule.lookbackMs = 1500;
            rules.add (filterRule);
        }
    }

    processor->setManualConditionRules (rules);
    processor->setConfiguredConditionNames (conditionNames);
    processor->setTrialFilterState (filterConditionName.isNotEmpty(), filterConditionName);

    String selected = preferredCondition.trim();

    if (selected.isEmpty() || ! conditionNames.contains (selected))
        selected = conditionNames.isEmpty() ? String ("All Trials") : conditionNames[0];

    processor->setSelectedConditionName (selected);
}

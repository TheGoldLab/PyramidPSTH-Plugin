#ifndef PYRAMID_PSTH_EDITOR_H_
#define PYRAMID_PSTH_EDITOR_H_

#include "PyramidRuleEngine.h"

#include <VisualizerEditorHeaders.h>

class PyramidPSTH;
class Visualizer;

class ConfigureDialog : public DialogWindow,
                        public ComboBox::Listener
{
public:
    struct UserCondition
    {
        String conditionName;
        String codeKey;
        String expectedValue;
    };

    ConfigureDialog (Array<UserCondition>& conditions, 
                     bool& filterEnabled,
                     String& filterCodeKey,
                     String& filterExpectedValue,
                     const String& preferredCondition);

    void comboBoxChanged (ComboBox* comboBox) override;
    void updateFieldsFromCondition (const String& conditionName);

    Array<UserCondition>& userConditions;
    bool& filterEnabled;
    String& filterCodeKey;
    String& filterExpectedValue;
    String selectedConditionName;
    String selectedCodeKey = "name";
    String selectedExpectedValue;

    std::unique_ptr<ComboBox> conditionCombo;
    std::unique_ptr<TextEditor> conditionNameEditor;
    std::unique_ptr<TextEditor> codeKeyEditor;
    std::unique_ptr<TextEditor> expectedValueEditor;
    std::unique_ptr<TextEditor> filterCodeKeyEditor;
    std::unique_ptr<TextEditor> filterValueEditor;
    std::unique_ptr<UtilityButton> addButton;
    std::unique_ptr<UtilityButton> updateButton;
    std::unique_ptr<UtilityButton> deleteButton;
    std::unique_ptr<UtilityButton> setFilterButton;
    std::unique_ptr<UtilityButton> doneButton;
    std::unique_ptr<UtilityButton> clearAllButton;
    std::unique_ptr<UtilityButton> cancelButton;

    int resultCode = 0;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigureDialog);
};

class PyramidPSTHEditor : public VisualizerEditor,
                          public Button::Listener,
                          public Timer
{
public:
    PyramidPSTHEditor (GenericProcessor* parentNode);
    ~PyramidPSTHEditor() override;

    void buttonClicked (Button* button) override;
    void timerCallback() override;
    void resized() override;
    Visualizer* createNewCanvas() override;

private:
    struct UserCondition
    {
        String conditionName;
        String codeKey;
        String expectedValue;
    };

    void openConfigureDialog();
    void openAlignmentDialog();
    void applyManualRulesToProcessor (const String& preferredCondition);

    PyramidPSTH* getPyramidProcessor() const;
    String formatStatusForDisplay (const String& rawStatus) const;

    std::unique_ptr<UtilityButton> loadRulesButton;
    std::unique_ptr<UtilityButton> clearRulesButton;
    std::unique_ptr<UtilityButton> configureButton;
    std::unique_ptr<UtilityButton> alignModeButton;
    std::unique_ptr<Label> debugLabel;
    std::unique_ptr<TextEditor> statusBox;
    Array<UserCondition> userConditions;
    bool filterEnabled = false;
    String filterCodeKey = "name";
    String filterExpectedValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PyramidPSTHEditor);
};

#endif

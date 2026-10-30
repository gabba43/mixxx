#pragma once

#include <QList>
#include <QString>

#include "analyzer/plugins/analyzerplugin.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefbeatsdlg.h"
#include "preferences/usersettings.h"

class QWidget;

class DlgPrefBeats : public DlgPreferencePage, public Ui::DlgBeatsDlg {
    Q_OBJECT
  public:
    DlgPrefBeats(QWidget *parent, UserSettingsPointer _config);
    virtual ~DlgPrefBeats();

    QUrl helpUrl() const override;

  public slots:
    // Apply changes to widget
    void slotApply() override;
    void slotUpdate() override;
    void slotResetToDefaults() override;

  private slots:
    void pluginSelected(int i);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    void analyzerEnabled(Qt::CheckState state);
    void fastAnalysisEnabled(Qt::CheckState state);
    void slotReanalyzeChanged(Qt::CheckState state);
    void slotReanalyzeImportedChanged(Qt::CheckState state);
#else
    void analyzerEnabled(int i);
    void fastAnalysisEnabled(int i);
    void slotReanalyzeChanged(int value);
    void slotReanalyzeImportedChanged(int value);
#endif
    void tempoModeSelected(int index);
    void slotStemStrategyChanged(int index);

  private:
    void updateGui();

    BeatDetectionSettings m_bpmSettings;
    QList<mixxx::AnalyzerPluginInfo> m_availablePlugins;
    QString m_selectedAnalyzerId;
    bool m_bAnalyzerEnabled;
    mixxx::BeatGridMode m_tempoMode;
    bool m_bFastAnalysisEnabled;
    bool m_bReanalyze;
    bool m_bReanalyzeImported;
    BeatDetectionSettings::StemStrategy m_stemStrategy;
};
